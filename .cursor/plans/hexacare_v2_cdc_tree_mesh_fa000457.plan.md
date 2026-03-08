---
name: Hexacare V2 CdC Tree Mesh
overview: "Alignement du firmware Lexacare V2 sur le Cahier des Charges (CdC) : machine d'état réseau complète, construction du réseau avec évaluation des parents (RSSI/Layer), self-healing avec ACK heartbeat et état ORPHAN, routage downstream, OTA en cascade parallèle, et structures de données explicites."
todos: []
isProject: false
---

# Plan d'implémentation – Hexacare V2 selon Cahier des Charges (Tree Mesh ESP-NOW)

## Contexte

Le firmware actuel ([routing_manager.cpp](LexacareV1/src/comm/routing_manager.cpp), [ota_tree_manager.cpp](LexacareV1/src/comm/ota_tree_manager.cpp), [espnow_mesh.cpp](LexacareV1/src/comm/espnow_mesh.cpp)) implémente déjà une base Tree Mesh (ROOT, Beacons, JOIN_REQ/ACK, Heartbeat, OTA store & forward, LED). Le CdC impose des états supplémentaires, une évaluation formelle des parents, des timeouts et ACK explicites, un routage downstream, et une propagation OTA parallèle. Ce plan décrit les écarts et les modifications à apporter.

---

## 1. Machine d'état réseau (State Machine)

**Actuel :** `STATE_SCANNING`, `STATE_JOINING`, `STATE_CONNECTED` dans [routing_manager.cpp](LexacareV1/src/comm/routing_manager.cpp) (l.31-36).

**CdC :** `STATE_INIT` → `STATE_SCANNING` → `STATE_EVALUATING` → `STATE_JOINING` → `STATE_CONNECTED` ; et `STATE_ORPHAN` en cas de perte du parent.

**Modifications :**

- Ajouter les états `STATE_INIT`, `STATE_EVALUATING`, `STATE_ORPHAN` dans l’enum et les transitions dans `routing_task` et `on_mesh_receive`.
- **STATE_INIT :** après `routing_init()` / `routing_set_root()` ; passage à `STATE_SCANNING` (ou directement `STATE_CONNECTED` pour le ROOT).
- **STATE_EVALUATING :** après une phase d’écoute des Beacons (ex. 2–3 s), tri des candidats (voir §2), puis passage à `STATE_JOINING` avec le meilleur parent.
- **STATE_ORPHAN :** entrée quand 3 heartbeats consécutifs sans ACK (voir §3) ; déclencher `routing_manager_orphan_recovery()` (retour à `STATE_SCANNING`, recherche d’un nouveau parent).
- Exposer l’état courant (ex. variable globale atomique ou getter) pour la tâche LED (`current_mesh_state`).

---

## 2. Tâche A – Construction du réseau (Network Building)

### 2.1 `routing_manager_broadcast_beacon()`

- **Actuel :** beacon envoyé dans la boucle `routing_task` toutes les 1 s (l.303-311).
- **CdC :** fréquence 2–5 s avec jitter aléatoire.
- **Faire :** extraire l’envoi beacon dans une fonction `routing_manager_broadcast_beacon()` ; dans `routing_task`, appeler cette fonction avec un intervalle de base (ex. 3 s) + jitter (ex. `esp_random() % 2000` ms). S’assurer que seuls les nœuds connectés ou ROOT envoient le beacon.

### 2.2 `routing_manager_scan_and_evaluate()`

- **Actuel :** un seul “meilleur” candidat (`s_best_parent`_*) mis à jour à chaque Beacon reçu.
- **CdC :** liste de candidats ; rejet si `enfants_actuels >= capacité_max` ou `Layer >= ma couche précédente` ; score = (RSSI × Poids_RSSI) + (Layer × Poids_Layer) ; préférer couche basse et RSSI > -80 dBm.
- **Faire :**
  - Introduire une structure `ParentCandidate` (mac, nodeId, layer, rssi, currentChildrenCount) et un tableau/vector de candidats (taille max ex. 10), rempli dans `on_mesh_receive` lors des MSG_BEACON en `STATE_SCANNING`.
  - Après une fenêtre d’écoute (dans `routing_task`, phase SCANNING), appeler `routing_manager_scan_and_evaluate()` : filtrer (capacité, layer), calculer le score, trier, choisir le meilleur.
  - Passer en `STATE_JOINING` avec la MAC du meilleur candidat (ou rester en SCANNING si aucun valide).

### 2.3 `routing_manager_join_parent(uint8_t* parent_mac)`

- **Actuel :** join effectué inline dans `routing_task` (envoi JOIN_REQ puis `vTaskDelay(1000)` en STATE_JOINING).
- **CdC :** fonction dédiée ; timer 1000 ms pour JOIN_ACK ; en cas d’échec, exclure ce candidat et réessayer avec le 2ᵉ.
- **Faire :**
  - Implémenter `routing_manager_join_parent(parent_mac)` : `esp_now_add_peer`, envoi MSG_JOIN_REQ, attente 1000 ms (event flag ou variable protégée par callback) pour MSG_JOIN_ACK.
  - Dans `routing_task`, en STATE_JOINING : appeler `routing_manager_join_parent(s_best_parent_mac)` ; si succès → CONNECTED ; si timeout, retirer le candidat de la liste, prendre le suivant (2ᵉ) et réessayer, sinon revenir à STATE_SCANNING.

### 2.4 `routing_manager_handle_join_request(uint8_t* child_mac, uint16_t child_id)`

- **Actuel :** logique dans `on_mesh_receive` (JOIN_REQ → update_child + routing_send_unicast(MSG_JOIN_ACK)).
- **CdC :** fonction nommée ; vérifier place disponible ; ajouter enfant en table ; ajouter peer ESP-NOW ; répondre MSG_JOIN_ACK avec couche assignée (Layer+1).
- **Faire :** extraire cette logique dans `routing_manager_handle_join_request(child_mac, child_id)` ; s’assurer que JOIN_ACK envoie bien la couche (déjà le cas via `routing_send_unicast` qui met `hdr->layer = s_my_layer`). Appeler cette fonction depuis `on_mesh_receive` quand msgType == MSG_JOIN_REQ.

---

## 3. Tâche B – Routage et redirection (Forwarding)

### 3.1 `routing_manager_forward_upstream(uint8_t* data, size_t len)`

- **Actuel :** envoi montant fait dans `dataTxTask` (main) et dans `espnow_mesh` (réception MSG_DATA → envoi au parent). Pas de décrémentation TTL.
- **CdC :** encapsuler payload et envoyer en unicast au parent ; décrémenter TTL ; si TTL == 0, détruire le paquet.
- **Faire :**
  - Ajouter `routing_manager_forward_upstream(data, len)` qui : lit ou reçoit un header (avec TTL) ; décrémente TTL ; si TTL > 0, envoie au parent (TreeMeshHeader + payload) avec le nouveau TTL ; sinon ne pas forward.
  - Dans [espnow_mesh.cpp](LexacareV1/src/comm/espnow_mesh.cpp), pour MSG_DATA reçu : ne pas recréer un header avec TTL=15 ; soit passer le header reçu (avec TTL-1) soit reconstruire le header en mettant `ttl = hdr->ttl - 1`. Appeler `routing_manager_forward_upstream` (ou équivalent prenant en compte le TTL) pour le relais. Pour l’émission initiale (dataTxTask), garder TTL initial (ex. 15).

### 3.2 `routing_manager_route_downstream(uint16_t target_id, uint8_t* data, size_t len)`

- **Actuel :** inexistant ; pas de routage vers un nœud cible depuis le ROOT.
- **CdC :** ROOT / intermédiaire envoie vers `target_id` : si cible parmi les enfants directs → unicast à cet enfant ; si parmi les sous-enfants → forward vers l’enfant intermédiaire concerné.
- **Faire :**
  - Définir une table de routage descendante : pour chaque enfant direct, savoir s’il est feuille ou relais (et éventuellement quels `node_id` sont dans son sous-arbre). Pour une première version “sans table de sous-arbres” : si `target_id` est un enfant direct, envoyer à cet enfant ; sinon, envoyer le paquet à tous les enfants (flood limité aux enfants) ; chaque nœud qui reçoit un paquet downstream avec `destNodeId != my_id` et `destNodeId != 0xFFFF` le transmet à ses enfants (ou à l’enfant concerné si on ajoute plus tard une table).
  - Ajouter dans le protocole un type de message “downstream” (ou réutiliser un champ `destNodeId` dans TreeMeshHeader déjà présent) et son traitement dans `on_mesh_receive` : si je suis la cible, traiter ; sinon appeler `routing_manager_route_downstream(target_id, data, len)` pour transmettre.
  - Implémenter `routing_manager_route_downstream(target_id, data, len)` : parcourir `children_table` ; si `target_id` trouvé, unicast à ce child ; sinon (option avancée) forward vers les enfants qui sont relais, ou flood vers tous les enfants avec TTL pour limiter la propagation.

---

## 4. Tâche C – Maintien du réseau et cicatrisation (Self-Healing)

### 4.1 `routing_manager_send_heartbeat()`

- **Actuel :** envoi HEARTBEAT toutes les 1 s dans la même boucle que le beacon (l.294-301).
- **CdC :** toutes les 10 s ; parent doit répondre par ACK_HEARTBEAT explicite ou ACK ESP-NOW.
- **Faire :**
  - Séparer fréquence beacon (2–5 s) et fréquence heartbeat (10 s) dans `routing_task`.
  - Appeler `routing_manager_send_heartbeat()` depuis la tâche : envoi MSG_HEARTBEAT au parent. Ajouter dans le protocole un message `MSG_ACK_HEARTBEAT` (ou réutiliser un type existant). Parent : à réception de MSG_HEARTBEAT, répondre par MSG_ACK_HEARTBEAT (unicast à l’enfant) et mettre à jour `last_heartbeat_timestamp` de cet enfant.

### 4.2 `routing_manager_check_timeouts()`

- **Actuel :** `cleanup_children()` avec CHILD_TIMEOUT_MS = 15 s.
- **CdC :** parent : timeout 30 s sans heartbeat → supprimer enfant de la table et `esp_now_del_peer` ; enfant : 3 heartbeats consécutifs sans ACK → passer en STATE_ORPHAN.
- **Faire :**
  - Passer CHILD_TIMEOUT_MS à 30 s (ou constante configurable).
  - Côté enfant : compter les envois de heartbeat sans réception d’ACK (variable `s_heartbeat_fail_count`) ; à 3, passer en STATE_ORPHAN et déclencher la récupération.
  - Dans `routing_manager_check_timeouts()` (appelée depuis `routing_task`) : côté parent, conserver la logique actuelle de nettoyage des enfants (avec 30 s) et appeler `esp_now_del_peer` pour l’enfant retiré ; côté enfant, vérifier `s_heartbeat_fail_count` et passer en ORPHAN si >= 3.

### 4.3 `routing_manager_orphan_recovery()`

- **Actuel :** pas d’état ORPHAN ni de récupération dédiée.
- **CdC :** enfant passe en STATE_SCANNING, trouve un nouveau parent via Beacons, envoie JOIN_REQ ; les sous-enfants conservent leur parent actuel mais “layer” mis à jour en cascade (message de sync).
- **Faire :**
  - En STATE_ORPHAN, appeler `routing_manager_orphan_recovery()` : réinitialiser parent, passer à STATE_SCANNING, lancer une nouvelle phase d’écoute + évaluation + join (réutiliser la même logique que le join initial). Pour la “mise à jour en cascade des layers” des sous-enfants : phase 1 optionnelle (message dédié “layer update” diffusé aux enfants avec nouvelle couche) ; phase 2 possible si nécessaire pour 1000+ nœuds.

---

## 5. Tâche D – OTA en cascade (Store and Forward)

### 5.1 `ota_tree_manager_receive_chunk()`

- **Actuel :** réception par `ota_tree_on_uart_chunk` (ROOT) et `handle_ota_chunk` (mesh) ; écriture via `esp_partition_write`. Pas de réponse explicite OTA_CHUNK_OK au parent/Python.
- **CdC :** répondre au parent (ou au script Python si ROOT) par OTA_CHUNK_OK.
- **Faire :**
  - Définir un message ou un mécanisme OTA_CHUNK_ACK (ou OTA_CHUNK_OK). En réception d’un chunk (UART ou mesh), après écriture réussie, envoyer cet ACK : depuis un nœud enfant → unicast au parent ; depuis le ROOT (réception UART) → envoyer sur Serial vers le PC (ex. ligne de texte "OTA_CHUNK_OK" ou binaire selon protocole Python).

### 5.2 `ota_tree_manager_propagate()` et parallélisme

- **Actuel :** ROOT envoie OTA à ses enfants un par un ; quand tous ont fini, ROOT redémarre. Un intermédiaire ne propage pas à ses propres enfants avant d’avoir fini.
- **CdC :** dès qu’un enfant de couche 1 a reçu 100 %, il commence à propager à ses enfants (couche 2), libérant le ROOT pour le prochain enfant de couche 1.
- **Faire :**
  - Quand un nœud (ROOT ou intermédiaire) a terminé de recevoir tout le firmware (état passant à “prêt à distribuer”), il entre en mode “serveur OTA” pour ses enfants : il peut envoyer OTA_ADV et répondre OTA_REQ / OTA_CHUNK à ses enfants.
  - Dans `ota_tree_task` : pour un nœud qui n’est pas ROOT et qui vient de finir la réception mesh (s_state = OTA_DISTRIBUTING), avant de redémarrer, faire une phase de distribution vers ses propres enfants (même logique que le ROOT : liste `routing_get_children()`, envoi OTA_ADV puis réponses aux OTA_REQ, attente OTA_DONE par enfant). Ainsi, un intermédiaire sert ses enfants en parallèle du ROOT qui sert les siens.
  - Option : permettre au ROOT de ne pas attendre la fin de distribution de tous les enfants avant de passer au suivant (dès qu’un enfant a fini, passer au prochain enfant du ROOT) ; les intermédiaires font de même. Déjà partiellement le cas ; vérifier que les intermédiaires propagent bien dès qu’ils ont 100 %.

---

## 6. Tâche E – Intégration matérielle (Capteurs & LED)

### 6.1 `data_collection_task()`

- **Actuel :** [sensor_sim](LexacareV1/src/sensors/sensor_sim.cpp) (ou tâches Core 1) produit des trames et les met dans `g_queue_espnow_tx` ; `dataTxTask` (main) dépile et envoie (ROOT → Serial, autre → parent).
- **CdC :** “data_collection_task” lit capteurs, formate DataPayload, met en file ; le moteur de routage (Core 0) dépile et fait `routing_manager_forward_upstream()`.
- **Faire :** renommer ou documenter la tâche de collecte (sensor_sim ou tasks_core1) comme “data_collection_task” ; s’assurer que le seul chemin d’envoi montant soit bien `routing_manager_forward_upstream()` (ou l’équivalent actuel qui envoie au parent). Si on introduit `routing_manager_forward_upstream()` en §3.1, faire en sorte que dataTxTask et le relais dans espnow_mesh l’utilisent pour l’uplink.

### 6.2 `neopixel_status_task()` (LED)

- **Actuel :** [led_manager](LexacareV1/src/system/led_manager.cpp) avec LedState (OFF, SCANNING, CONNECTED, ROOT, OTA, ERROR) et `led_manager_task` avec période 50 ms.
- **CdC :** BLEU clignotant rapide = SCANNING ; VERT pulsation = CONNECTED ; VIOLET fixe = ROOT ; ORANGE/ROUGE flash = OTA ; lecture d’une variable atomique `current_mesh_state` ; pas de `delay()` bloquant, utiliser `vTaskDelayUntil`.
- **Faire :** alimenter `led_manager_set_state()` à partir de l’état réseau (STATE_SCANNING → LED_STATE_SCANNING, STATE_CONNECTED → LED_STATE_CONNECTED, etc.) ; ajouter un état LED pour OTA (déjà LED_STATE_OTA). S’assurer que `led_manager_task` utilise `vTaskDelayUntil` pour la période (vérifier le code actuel). Exposer `current_mesh_state` (état routing) de manière thread-safe pour la LED.

---

## 7. Structures de données

- **CdC :** `MeshChildNode_t` (mac_address, node_id, last_heartbeat_timestamp, is_active) et `children_table[MAX_CHILDREN_PER_NODE]`.
- **Actuel :** `ChildInfo` (nodeId, mac[6], lastSeen) et `std::vector<ChildInfo> s_children` dans routing_manager.
- **Faire :** soit renommer `ChildInfo` en `MeshChildNode_t` et ajouter `is_active` (ou dériver “actif” de lastSeen vs timeout) ; soit introduire `MeshChildNode_t` dans [mesh_tree_protocol.h](LexacareV1/include/comm/mesh_tree_protocol.h) (ou routing_manager.h) et l’utiliser comme type des entrées de la table. Garder une table de taille fixe `MAX_CHILDREN_PER_NODE` (20) pour cohérence avec le CdC et mémoire déterministe, ou conserver le vector avec une taille max 20. Lors du timeout, marquer `is_active = false` et retirer le peer ESP-NOW avant de supprimer ou réutiliser l’entrée.

---

## 8. Fichiers impactés (résumé)


| Fichier                                                               | Modifications principales                                                                                                                                                                                                                                                                                      |
| --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [routing_manager.cpp](LexacareV1/src/comm/routing_manager.cpp) / .h   | États INIT, EVALUATING, ORPHAN ; candidats parent + scoring ; `routing_manager_broadcast_beacon`, `scan_and_evaluate`, `join_parent`, `handle_join_request`, `forward_upstream`, `route_downstream`, `send_heartbeat`, `check_timeouts`, `orphan_recovery` ; ACK heartbeat ; timeout 30 s ; 3 échecs → ORPHAN. |
| [mesh_tree_protocol.h](LexacareV1/include/comm/mesh_tree_protocol.h)  | Ajouter MSG_ACK_HEARTBEAT si nouveau type ; optionnellement MeshChildNode_t.                                                                                                                                                                                                                                   |
| [espnow_mesh.cpp](LexacareV1/src/comm/espnow_mesh.cpp)                | Traitement MSG_DATA : décrémenter TTL et utiliser forward_upstream ; traiter paquets downstream (destNodeId) et appeler route_downstream.                                                                                                                                                                      |
| [ota_tree_manager.cpp](LexacareV1/src/comm/ota_tree_manager.cpp) / .h | Réponse OTA_CHUNK_OK (Serial pour ROOT, mesh pour enfant) ; propagation parallèle (intermédiaire distribue à ses enfants après réception complète).                                                                                                                                                            |
| [led_manager.cpp](LexacareV1/src/system/led_manager.cpp)              | Alimentation à partir de l’état mesh ; vTaskDelayUntil si pas déjà fait.                                                                                                                                                                                                                                       |
| [main.cpp](LexacareV1/src/main.cpp)                                   | S’assurer que dataTxTask utilise le flux avec TTL et forward_upstream si refactorisé.                                                                                                                                                                                                                          |


---

## 9. Ordre de mise en œuvre recommandé

1. **État et structures** : états INIT / EVALUATING / ORPHAN, MeshChildNode_t / table enfants, exposition de l’état pour la LED.
2. **Réseau (Tâche A)** : beacon avec jitter, liste de candidats + `scan_and_evaluate`, `join_parent` avec timeout et 2ᵉ candidat, `handle_join_request` extrait.
3. **Routage (Tâche B)** : TTL dans forward upstream ; `forward_upstream()` ; `route_downstream()` et traitement downstream dans espnow_mesh.
4. **Self-healing (Tâche C)** : heartbeat 10 s, MSG_ACK_HEARTBEAT, timeouts 30 s / 3 échecs, `orphan_recovery`.
5. **OTA (Tâche D)** : OTA_CHUNK_OK ; propagation parallèle par les intermédiaires.
6. **Tâche E** : alignement LED et data_collection (nommage / flux).

Ce plan respecte le CdC et réutilise au maximum le code existant en le complétant et en le découpant en fonctions nommées comme spécifié.