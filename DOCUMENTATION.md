# Lexacare V1 — Documentation utilisateur

Ce document décrit le système Lexacare V1 de manière simple : à quoi il sert, comment l’utiliser au quotidien et comment interpréter les voyants.

---

## 1. À quoi sert le système ?

Lexacare V1 est un ensemble de **cartes électroniques** qui communiquent entre elles sans fil (sans WiFi classique) et qui peuvent :

- **Envoyer des données** (simulation de capteurs : battement de cœur, température, batterie, etc.) d’une carte à l’autre.
- **Afficher ces données** sur un ordinateur lorsque une carte est branchée en USB (carte « passerelle »).
- **Recevoir des mises à jour du logiciel** de deux façons :
  - **Mise à jour de la carte branchée au PC** (passerelle uniquement).
  - **Mise à jour de toutes les autres cartes** via le réseau sans fil (mesh).

Il n’y a **pas de connexion Internet** : tout passe par le câble USB (pour la carte reliée au PC) et par le lien radio entre les cartes.

---

## 2. Les deux types de cartes

- **Carte « passerelle » (ROOT)**  
  C’est celle qui est **branchée au PC par USB**. Elle reçoit les données des autres cartes et les envoie vers le logiciel sur le PC. C’est aussi par elle que l’on envoie les mises à jour.  
  En pratique : c’est souvent la première carte que vous configurez ou celle que vous utilisez pour piloter le système.

- **Cartes « nœuds »**  
  Ce sont les **autres cartes**, non branchées au PC. Elles envoient leurs données vers la passerelle par radio et peuvent recevoir les mises à jour diffusées par la passerelle.

**Important :** Une seule carte doit être considérée comme passerelle (celle branchée au PC). Les autres sont des nœuds. Le rôle (passerelle ou nœud) est défini dans la configuration du logiciel embarqué.

---

## 3. Utilisation du logiciel sur PC (console Python)

Un programme sur ordinateur permet de **voir les données** des cartes et de **lancer les mises à jour**.

### 3.1 Installation

1. Installer **Python** sur le PC (version 3.7 ou plus récente).
2. Ouvrir un terminal dans le dossier du projet et exécuter :
   ```text
   pip install paho-mqtt pyserial
   ```
3. Lancer le programme :
   ```text
   python tools/lexacare_monitor.py
   ```

### 3.2 Connexion à la carte passerelle (port UART pour OTA et logs)

Sur l’ESP32-S3, la réception des données **depuis le PC vers la carte** (OTA, test liaison) ne fonctionne pas via l’USB natif. Il faut utiliser l’**UART sur broches**.

1. **Câblage pour le moniteur et l’OTA**  
   Connecter un **adaptateur USB-UART** (FTDI, CP2102, etc.) à la carte passerelle :
   - **Adaptateur TX** → **Broche 44 (RX)** de la carte
   - **Adaptateur RX** → **Broche 43 (TX)** de la carte
   - **GND** → **GND**
   - Réglage : **115200 bauds**.

2. Brancher l’adaptateur USB-UART au PC (la carte peut rester alimentée par son câble USB habituel).

3. Dans la fenêtre du programme :
   - Choisir le **port série** de l’adaptateur UART (ex. COM5, pas le port USB natif de la carte).
   - Cliquer sur **« Connecter Série »**.

4. Si la connexion réussit, les messages de la carte s’affichent dans la zone **« Log série »** et l’envoi OTA (boutons « Lancer OTA Série » / « Lancer OTA Mesh ») fonctionne.

### 3.3 Consulter les données et les logs

- **Tableau du haut** : une ligne par carte (nœud) vue par la passerelle (identifiant, dernière vue, batterie, chute %, température, BPM, version).
- **Logs script** (en bas à gauche) : messages du programme PC (connexion, envoi de mise à jour, etc.).
- **Log série** (en bas à droite) : tout ce que la carte passerelle envoie (logs de démarrage, trames, erreurs).
- Le bouton **« Pause »** permet de figer l’affichage des logs pour les lire tranquillement ; **« Reprendre »** relance l’affichage.

---

## 4. Mise à jour du logiciel (OTA)

Il existe **deux types de mise à jour**, selon ce que vous voulez mettre à jour.

### 4.1 Préparation commune

- Obtenir le fichier de mise à jour (fichier **.bin**).
- Dans le programme PC, cliquer sur **« Fichier .bin »** et sélectionner ce fichier.
- S’assurer que la **carte passerelle** est connectée (bouton **« Connecter Série »**).

### 4.2 Lancer OTA Série (mise à jour de la carte passerelle)

- **But :** mettre à jour **uniquement la carte passerelle** (celle connectée au PC via l’UART broches 44/43).
- **Prérequis :** le script doit être connecté au **port UART** (adaptateur sur RX44/TX43), pas au port USB natif.
- **Étapes :**
  1. Fichier .bin sélectionné, passerelle connectée.
  2. Cliquer sur **« Lancer OTA Série »**.
  3. Le programme envoie le nouveau logiciel à la carte par le port série.
  4. La carte affiche **violet** pendant la mise à jour.
  5. À la fin :
     - **Succès :** la LED **clignote 4 fois en vert**, puis la carte redémarre avec le nouveau logiciel.
     - **Erreur :** la LED **clignote 4 fois en rouge**, puis la carte redémarre en gardant l’ancien logiciel (pas de corruption).

À utiliser quand vous voulez uniquement mettre à jour la carte connectée au PC.

### 4.3 Lancer OTA Mesh (mise à jour de toutes les cartes par le réseau)

- **But :** mettre à jour **toutes les cartes du réseau** (y compris celles qui ne sont pas branchées au PC). La passerelle reçoit le fichier par USB puis le diffuse par radio aux autres cartes.
- **Étapes :**
  1. Fichier .bin sélectionné, passerelle connectée.
  2. Cliquer sur **« Lancer OTA Mesh »**.
  3. Le programme envoie le fichier à la **passerelle** par le port série.
  4. La passerelle diffuse le nouveau logiciel aux autres cartes par le réseau mesh.
  5. Sur **chaque carte** (passerelle et nœuds) :
   - LED **violette** pendant la réception et l’écriture.
  6. À la fin, sur chaque carte qui a reçu la mise à jour :
   - **Succès :** LED **4 clignotements verts**, puis redémarrage avec le nouveau logiciel.
   - **Erreur :** LED **4 clignotements rouges**, puis redémarrage avec l’ancien logiciel (sécurité).

À utiliser quand vous voulez que toutes les cartes aient la même version du logiciel.

**Conseil :** pendant une mise à jour mesh, les cartes ne s’envoient plus de données de capteurs ; tout le trafic est réservé à la mise à jour pour plus de fiabilité.

---

## 5. Signification des voyants LED

La carte possède une **LED RGB** (une seule couleur à la fois selon l’état).

| Couleur / Comportement | Signification |
|------------------------|----------------|
| **Vert** (fixe ou clignotant) | Carte en mode **passerelle** (branchée au PC), tout va bien. |
| **Ambre / Orange** (fixe ou clignotant) | Carte en mode **nœud** (non branchée au PC), tout va bien. |
| **Violet** | **Mise à jour en cours** (réception ou envoi du nouveau logiciel). |
| **Bleu** | La passerelle **diffuse** une mise à jour vers les autres cartes (OTA Mesh). |
| **4 clignotements verts** | Mise à jour **réussie** ; la carte va redémarrer avec le nouveau logiciel. |
| **4 clignotements rouges** | Mise à jour **en erreur** ; la carte va redémarrer en gardant l’ancien logiciel (évite la corruption). |
| **Rouge** fixe | **Erreur** au démarrage (par exemple échec d’initialisation du réseau). |

Le **clignotement** vert ou ambre en fonctionnement normal indique simplement que la carte est vivante (battement de cœur).

---

## 6. En cas de problème

### La carte ne répond pas après une mise à jour

- Si vous avez vu **4 clignotements rouges** : la mise à jour a échoué, la carte a redémarré avec l’ancien logiciel. Vous pouvez réessayer une mise à jour (même fichier ou autre).
- Si la carte ne s’allume plus du tout : vérifier l’alimentation et le câble USB. En dernier recours, une mise à jour par câble (OTA Série) avec un fichier .bin connu bon peut être tentée.

### Aucune donnée dans le tableau du programme PC

- Vérifier que la **carte passerelle** est bien branchée et que vous avez cliqué sur **« Connecter Série »**.
- Vérifier que le **bon port série** est sélectionné (liste déroulante, bouton « Rafraîchir » si besoin).
- S’assurer que les **autres cartes** (nœuds) sont sous tension et à portée radio de la passerelle.

### Les logs ne s’affichent pas

- Vérifier que la connexion série est établie (« Connecter Série »).
- Regarder dans les **deux** zones de log (script à gauche, série à droite).
- Utiliser **« Pause »** pour figer l’affichage et lire les derniers messages.

### Mise à jour mesh : certaines cartes ne se mettent pas à jour

- Les cartes doivent être **à portée** de la passerelle (ou se relayer entre elles).
- Réessayer **OTA Mesh** en gardant les cartes proches et sans déplacer les cartes pendant l’envoi.
- Vérifier dans les logs série que la passerelle indique bien l’envoi des blocs (chunks) sur le mesh.

### OTA Série / Mesh : le script envoie mais rien ne s’affiche côté ROOT

- Sur **ESP32-S3**, la réception PC → carte ne fonctionne **pas** via l’USB natif. Il faut connecter le script au **port UART** (adaptateur USB-UART sur **broches RX 44 / TX 43**, 115200 bauds).
- Vérifier que le port sélectionné dans le script est bien celui de l’**adaptateur UART** branché sur les broches 43/44, et non le port USB de la carte.
- Après connexion, vous devez voir les logs dans « Log série » et le message « En attente octet mode… » ; le bouton **« Test liaison (0xFF) »** doit faire apparaître « Octet mode reçu: 0xFF » côté ROOT.

---

## 7. Résumé rapide

- **Une carte** = passerelle (branchée au PC en USB). **Les autres** = nœuds (alimentés, à portée).
- **Programme PC** : connexion série, choix du fichier .bin, **« Lancer OTA Série »** (passerelle seule) ou **« Lancer OTA Mesh »** (toutes les cartes).
- **LED violette** = mise à jour en cours. **4× vert** = succès puis redémarrage. **4× rouge** = erreur puis redémarrage sans corrompre le logiciel.

---

## 8. Pour les techniciens (compilation, flash, structure)

### Compilation et envoi du logiciel sur une carte

À la racine du projet firmware (dossier LexacareV1) :

```bash
pio run -e esp32-s3-devkitc-1
```

Pour compiler et envoyer le firmware sur la carte (carte branchée en USB) :

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

Ou utiliser l’IDE PlatformIO : **Build** puis **Upload**.

### Carte utilisée

- **MCU :** ESP32-S3 (double cœur).
- **Carte de test :** ESP32-S3-DevKitC-1 ou carte custom Lexacare.
- **Communication :** pas de WiFi classique ; les cartes communiquent en **ESP-NOW** (protocole sans fil de type mesh). Les données et logs sortent sur le **port série USB** de la carte passerelle.

### Rôle des principaux dossiers / fichiers

- **main.cpp** : démarrage du logiciel, tâches (réception radio, envoi, passerelle série), boucle principale et gestion des LED.
- **config/config.h** : constantes (réseau, capteurs, mode passerelle ou nœud, etc.).
- **config/pins_lexacare.h** : affectation des broches (I2C, UART, LED, etc.).
- **comm/espnow_mesh** : gestion du réseau ESP-NOW (envoi / réception entre cartes).
- **comm/ota_manager** : réception des mises à jour (par mesh), écriture en flash, vérification MD5, succès ou échec.
- **sensors/sensor_sim** : simulation des données capteurs (cœur, température, batterie, etc.) pour les tests.
- **tools/lexacare_monitor.py** : programme PC (interface, connexion série, envoi des mises à jour OTA Série et OTA Mesh).

### Protocole de mise à jour (résumé technique)

- **OTA Série :** le PC envoie 1 octet (0x01) + en-tête 38 octets (taille, nombre de blocs, MD5) + blocs de 200 octets. La **passerelle** écrit en flash et redémarre.
- **OTA Mesh :** le PC envoie 1 octet (0x02) + même en-tête + mêmes blocs. La **passerelle** diffuse l’en-tête puis chaque bloc sur le mesh ; les nœuds reçoivent, écrivent en flash, vérifient le MD5, puis clignotent 4× vert (succès) ou 4× rouge (erreur) et redémarrent.

Le détail complet du protocole série (format binaire, octet de mode, en-tête, chunks, compatibilité avec `lexacare_monitor.py`) est décrit dans la section **9. Protocole série (commandes OTA)** ci-dessous.

---

## 9. Protocole série (commandes OTA)

Cette section précise le format des données envoyées par le PC vers la carte ROOT sur le port série pour les mises à jour OTA. Elle permet de garder la compatibilité avec `tools/lexacare_monitor.py` et d’implémenter un autre client (autre script, autre langage) sans ambiguïté.

### 9.1 Périmètre

- **Sens OTA :** uniquement **PC → ROOT**. Le ROOT reçoit les commandes OTA sur l’**UART (broches RX44/TX43)** et envoie les logs/JSON vers le PC sur ce même UART (et sur l’USB).
- **Débit :** 115200 bauds (défini dans `src/system/log_dual.h` et `tools/lexacare_monitor.py`).
- **Autres commandes :** aucune autre commande binaire n’est gérée côté firmware (pas de ping, pas de « get version » binaire). Les seules données binaires reçues par le ROOT sont la **séquence OTA** décrite ci-dessous.

### 9.2 Séquence OTA (PC → ROOT) – format binaire

Toute mise à jour envoyée par le PC doit respecter la séquence suivante, **sans caractère de fin de ligne ni séparateur** entre les blocs.

#### Ordre des octets (endianness)

Tous les champs multi-octets sont en **little-endian** (octet de poids faible en premier), comme dans `src/lexacare_protocol.h` (`OtaAdvPayload`) et dans le script Python (`struct.pack("<I", ...)`, `struct.pack("<H", ...)`).

#### Étape 1 : Octet de mode (1 octet)

| Valeur (hex) | Nom dans le script     | Comportement ROOT |
|--------------|------------------------|-------------------|
| `0x01`       | `OTA_MODE_SERIAL_ROOT` | Mise à jour **uniquement du ROOT** : le ROOT écrit le firmware en flash sur lui-même, puis redémarre (LED violet → 4× vert ou 4× rouge). |
| `0x02`       | `OTA_MODE_MESH`        | Mise à jour **par le mesh** : le ROOT reçoit les blocs puis les diffuse sur le réseau (OTA_ADV puis OTA_CHUNK). Les nœuds flashent. |

Toute autre valeur est ignorée par le firmware (log « Mode inconnu » et attente du prochain octet de mode).

#### Étape 2 : En-tête OTA (38 octets fixes)

Juste après l’octet de mode, le PC envoie **exactement 38 octets** dont la structure est alignée sur `OtaAdvPayload` (cf. `src/lexacare_protocol.h`) :

| Offset | Taille | Type (C)    | Nom          | Description |
|--------|--------|-------------|--------------|-------------|
| 0      | 4      | uint32_t    | totalSize    | Taille totale du fichier .bin en octets (little-endian). |
| 4      | 2      | uint16_t    | totalChunks  | Nombre de blocs de 200 octets (little-endian). Doit être `ceil(totalSize / 200)`. |
| 6      | 32     | uint8_t[32] | md5Hex       | MD5 du fichier .bin, encodé en **32 caractères ASCII hexadécimaux** (0-9, a-f), **sans** préfixe « 0x » ni séparateur. |

Constantes côté firmware : `OTA_ADV_PAYLOAD_SIZE == 38`, `OTA_CHUNK_DATA_SIZE == 200` (`src/lexacare_protocol.h`).  
Côté Python : `OTA_ADV_HEADER_SIZE = 38`, `OTA_CHUNK_SIZE = 200` (`tools/lexacare_monitor.py`).

Exemple de construction de l’en-tête côté Python :

```python
header = struct.pack("<I", size) + struct.pack("<H", total_chunks) + md5.encode("ascii")
```

Lecture côté firmware : `total_size` = octets 0–3 LE, `total_chunks` = octets 4–5 LE, MD5 = octets 6–37.

#### Étape 3 : Blocs de données (chunks)

- Après les 38 octets d’en-tête, le PC envoie **exactement `totalChunks` blocs** de **200 octets** chacun.
- Chaque bloc contient 200 octets de données du fichier .bin. Le **dernier bloc** peut contenir moins de 200 octets de données réelles ; le reste doit être **complété par des octets de bourrage** (par ex. `0xFF`) pour que le bloc envoyé fasse toujours 200 octets. Le firmware lit toujours 200 octets par bloc.

Exemple côté Python :

```python
chunk = data[start : start + OTA_CHUNK_SIZE]
if len(chunk) < OTA_CHUNK_SIZE:
    chunk = chunk + b"\xff" * (OTA_CHUNK_SIZE - len(chunk))
ser_reader.send_raw(chunk)
```

#### Résumé de la trame complète (PC → ROOT)

```text
[ 1 octet : mode (0x01 ou 0x02) ]
[ 38 octets : totalSize (4) + totalChunks (2) + md5Hex (32) ]
[ totalChunks × 200 octets : données du fichier .bin, dernier bloc complété à 200 octets ]
```

Aucun caractère de contrôle (CR, LF, etc.) ne doit être inséré entre ces trois parties. L’envoi est **continu** (par ex. `send_raw` en Python sans ajout de `\n`).

### 9.3 Réception côté ROOT (firmware)

La tâche `serialGatewayTask` dans `src/main.cpp` utilise une machine à états :

- **WAIT_MODE :** attend au moins 1 octet, lit l’octet de mode ; si 0x01 ou 0x02, passe à WAIT_HEADER ; sinon ignore et reste en WAIT_MODE.
- **WAIT_HEADER :** accumule exactement 38 octets dans un buffer, parse `total_size`, `total_chunks`, MD5.
  - **Si mode 0x01 :** flash ROOT (Update.begin → lecture de `total_chunks` blocs de 200 octets → Update.write → vérification MD5 → Update.end ou abort → 4× LED vert/rouge → reboot). Pas de passage en MESH_SEND_CHUNKS.
  - **Si mode 0x02 :** envoie OTA_ADV sur le mesh, puis passe en MESH_SEND_CHUNKS.
- **MESH_SEND_CHUNKS :** pour chaque bloc, attend au moins 200 octets sur Serial, lit 200 octets, construit OTA_CHUNK et l’envoie sur le mesh ; après `total_chunks` blocs, repasse en WAIT_MODE.

Le PC doit envoyer la séquence complète (mode + 38 + tous les chunks) **sans délai inutile** entre les blocs pour éviter timeouts ou décalages.

### 9.4 Logs du protocole série (préfixes)

Pour faciliter le débogage, les messages liés au protocole utilisent des préfixes clairs :

- **`[SERIE]`** : émis par le firmware (ROOT) et par le script Python. Décrit l’octet de mode reçu, l’en-tête (taille, chunks, MD5), les étapes (attente en-tête, flash ROOT, envoi mesh) et la progression des chunks.
- **`[OTA]`** : émis par le firmware (nœuds et ROOT). Annonce reçue par le mesh, chunks reçus, succès ou échec (MD5, Update.end).

En filtrant les lignes contenant `[SERIE]` ou `[OTA]` dans la fenêtre « Log série » du moniteur, on suit tout le déroulement OTA.

### 9.5 Sortie ROOT → PC (réception par le script)

Le ROOT n’envoie **pas** de réponse binaire structurée aux commandes OTA. Il envoie uniquement :

- des **lignes de log** (texte avec `\n` via `log_dual_println`) ;
- des **lignes JSON** (trames mesh converties en JSON pour le tableau du moniteur).

Le script Python lit avec `readline()` (timeout 1 s) et affiche tout dans la fenêtre « Log série » ; s’il détecte une ligne contenant `{` et `}`, il en extrait le JSON et met à jour le tableau des nœuds. Aucun protocole binaire n’est utilisé en sens ROOT → PC pour l’OTA.

### 9.6 Points de compatibilité lexacare_monitor.py ↔ firmware

À respecter pour garder la compatibilité :

| Élément           | Côté Python                                                  | Côté firmware                                     |
|-------------------|--------------------------------------------------------------|---------------------------------------------------|
| Baud rate         | 115200                                                       | 115200 (log_dual)                                 |
| Ordre d’envoi     | 1 (mode) + 38 (header) + N×200 (chunks)                      | Lecture stricte dans cet ordre                    |
| Endianness header | `struct.pack("<I", size)`, `struct.pack("<H", total_chunks)` | Lecture octets 0–3 LE, 4–5 LE                     |
| Taille bloc       | 200                                                          | `OTA_CHUNK_DATA_SIZE` = 200                       |
| Dernier bloc      | Complété à 200 octets (0xFF)                                 | Toujours lu 200 octets                            |
| MD5               | 32 caractères hex ASCII (sans 0x)                             | Comparaison sur 32 octets (offset 6 de l’en-tête) |
| Pas de séparateur | Pas de `\n` entre mode / header / chunks                     | Lecture binaire continue                          |

### 9.7 Vérification de compatibilité script Python ↔ firmware

Le script `tools/lexacare_monitor.py` a été vérifié point par point avec le firmware (main.cpp `serialGatewayTask` et lexacare_protocol.h) :

| Point | Firmware | Script Python | Statut |
|-------|----------|---------------|--------|
| Ordre des octets | Attend : 1 (mode) puis 38 (header) puis N×200 (chunks) | Envoie : `send_raw(bytes([mode]))` puis `send_raw(header)` puis boucle `send_raw(chunk)` | OK |
| Mode | 0x01 = OTA Série, 0x02 = OTA Mesh | `OTA_MODE_SERIAL_ROOT = 0x01`, `OTA_MODE_MESH = 0x02` | OK |
| totalSize | Octets 0–3 little-endian | `struct.pack("<I", size)` | OK |
| totalChunks | Octets 4–5 little-endian | `struct.pack("<H", total_chunks)` avec `(size + 199) // 200` | OK |
| MD5 | 32 octets ASCII hex (offset 6), comparaison `memcmp` | `hashlib.md5(data).hexdigest().encode("ascii")` (32 caractères minuscules) | OK |
| Taille header | 38 = 4+2+32 | `OTA_ADV_HEADER_SIZE = 38`, `len(header) == 38` | OK |
| Taille chunk | 200 octets lus à chaque fois | `OTA_CHUNK_SIZE = 200`, dernier bloc complété à 200 avec `0xFF` | OK |
| Baud rate | 115200 (log_dual) | `BAUD_SERIAL = 115200` à l’ouverture du port | OK |
| Pas de texte / séparateur | Lecture binaire stricte | `send_raw()` sans `\n` ni encodage texte | OK |

Le script est **compatible** avec le protocole firmware. Si l’OTA ne se déclenche pas (aucun log côté ROOT), la cause est à chercher côté liaison (port USB, câble, pilote) ou réception USB sur l’ESP32-S3 ; utiliser le bouton « Test liaison (0xFF) » pour vérifier que les octets envoyés par le PC arrivent bien au ROOT.

---

Cette documentation décrit le fonctionnement du système Lexacare V1 tel qu’implémenté (utilisation, LED, OTA Série / OTA Mesh, dépannage, protocole série détaillé, et éléments techniques pour la compilation et la structure du code).
