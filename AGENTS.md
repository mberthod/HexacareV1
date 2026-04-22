**AGENTS.md — kit unifié MBHREP ESP32**  
Contexte racine pour coder en binôme avec OpenCode + Gemma local sur les  
   
 projets ESP32 du groupe MBHREP. Ce kit regroupe les conventions communes et  
   
 route vers des agents spécialisés par domaine d'expertise (pas par projet).  
Projets couverts :  
- **mbh-firmware** (mbh-firmware/) — firmware unifié ESP32-S3 qui combine LexaCare V1 (TinyML audio + ToF, détection chute + KWS) et le stack mesh (ESP-NOW primaire + ESP-WIFI-MESH fallback). Cible déploiement multi-chambres EHPAD : chaque dispositif détecte localement et propage les alertes via le mesh vers un gateway root.  
- **mesh-prototype** (mesh-prototype/) — prototype standalone du stack mesh, pour valider le failover sans la complexité LexaCare. Toujours utile comme laboratoire d'évolution du mesh_manager.  
Extensible : tout nouveau projet ESP-IDF hérite automatiquement des règles  
   
 universelles, ajoute ses propres sections spécifiques dans les agents.  
**Stack commun**  
- **ESP-IDF 6.0+** via PlatformIO  
- **FreeRTOS** dual-core (ou single-core pour C3)  
- **C** pour firmware bas niveau,  **C++** uniquement pour composants qui utilisent TFLM  
- **Python 3.10+** pour les outils PC (training, bench, CLI)  
- **Git** pour tous les projets  
**Conventions universelles**  
**C / C++ / ESP-IDF**  
- Logging : ESP_LOGI/W/E(TAG, ...), jamais printf (exception : émission de lignes structurées préfixées BENCH: pour le banc de test).  
- Tâches : toujours xTaskCreatePinnedToCore() avec cœur explicite, stack en **bytes**.  
- Allocation > 4 KB : heap_caps_malloc(sz, MALLOC_CAP_SPIRAM). calloc/free pour le reste.  
- Communication : queues + event groups + mutex. **Pas** de volatile global partagé.  
- Codes retour : esp_err_t sur toute fonction publique. ESP_ERROR_CHECK() en init.  
- Handles opaques (typedef struct foo_s *foo_t) dans les headers publics.  
- Plage de priorités FreeRTOS : 1–5 pour l'app. Au-dessus = conflit avec les tâches IDF internes.  
**Python**  
- Typing complet, from __future__ import annotations, mypy --strict.  
- CLI via Typer, une commande par verbe. pathlib.Path partout.  
- pyproject.toml source de vérité. Scripts entrypoints dans [project.scripts].  
- Seeds fixés pour la reproductibilité (training ML). Metadata JSON à côté de chaque artefact.  
**Git**  
Préfixes de commit : fw:, kit:, tools:, docs:, fix:, test:. Un commit par changement logique.  
**Agents**  
Quatre agents par domaine d'expertise. Charge celui qui matche ta tâche :  
| | | |  
|-|-|-|  
| **Agent** | **Invoquer quand** | **Fichier** |   
| **firmware** | Toute modif C/C++ dans firmware/ (tous projets) | agents/firmware.md |   
| **python** | Toute modif Python dans tool/ (LexaCare) ou tools/ (mesh) | agents/python.md |   
| **tinyml-bridge** | Export modèle, MFCC validation, ops resolver (LexaCare uniquement) | agents/tinyml-bridge.md |   
| **reference-oracle** | Question factuelle sur HW, pinout, protocole, limites — read-only | agents/reference-oracle.md |   
   
Règle d'usage : **un prompt = une tâche bornée**. Exemple efficace :  
*Charge * *agents/firmware.md* * et * *skills/freertos-task/SKILL.md* *. Ajoute une*  
 *  
 tâche * *task_telemetry* * dans * *firmware/main/* * qui consomme * *audio_q* * et*  
 *  
 * *vision_q* * et log le taux d'événements par minute. Pinne-la sur Core 1 à*  
 *  
 priorité 2, stack 4 KB. Respecte les conventions de * *AGENTS.md* *.*  
**Projets**  
**mbh-firmware (LexaCare TinyML + mesh, unifié)**  
Arborescence :  
mbh-firmware/  
 ├── README.md  
 └── firmware/  
     ├── platformio.ini              envs dont `production`, `production_usb_telemetry` (full + flux USB LXCS/LXCL/JSON pour lexa_live_monitor.py), capture_*, debug_mfcc  
     ├── sdkconfig.defaults          PSRAM octal, WiFi APSTA, mesh enabled  
     ├── partitions.csv              factory + dual OTA + littlefs  
     ├── main/  
     │   ├── app_main.c              init NVS + WiFi + LexaCare + mesh stacks  
     │   ├── app_config.h            node_id, PMK, canal, paramètres app  
     │   ├── lexa_config.h           GPIO, MFCC, arenas TFLM, fréquences bus  
     │   ├── app_events.h            audio_event_t, vision_event_t, fall_voice_alert_t  
     │   ├── task_audio.c            Core 0 prio 5 : I2S → MFCC → TFLM audio  
     │   ├── task_vision.c           Core 1 prio 5 : ToF → TFLM vision  
     │   ├── orchestrator.c          Core 1 prio 3 : fusion + mesh_send() alertes  
     │   └── models/                 .h générés par exporter Python  
     └── components/  
         ├── mesh_manager/           FSM failover, API mesh_send/recv  
         ├── tr_espnow/              transport primaire (impl complète)  
         ├── tr_wifimesh/            transport fallback (esp_mesh_*)  
         ├── i2s_stereo_mic/         driver I2S + ring PSRAM (à compléter)  
         ├── mfcc_dsp/               MFCC C++ ESP-DSP (à compléter)  
         ├── tflm_dual_runtime/      2 arenas TFLM isolées (à compléter)  
         ├── vl53l8cx_array/         ULD ST + multi-capteur (à compléter)  
         └── pca9555_io/             I2C @ 0x20 (à compléter)  
   
**Point d'intégration clé** : orchestrator.c corrèle fall+voice sur fenêtre  
   
 APP_FUSION_WINDOW_MS (200 ms) puis appelle mesh_send() en priorité  
   
 MESH_PRIO_CONTROL vers MESH_NODE_ROOT. C'est la liaison entre les deux  
   
 stacks qui était l'objectif du merge.  
**Build modes** :  
- production : tous les pipelines actifs, mesh up  
- capture_mode : streaming I2S+ToF brut sur USB-CDC, mesh désactivé (MBH_DISABLE_MESH=1), pas d'inférence — pour constituer des datasets  
- debug_mfcc : harness validation Python↔C, mesh désactivé  
**Déploiement multi-nodes** : chaque dispositif a son APP_NODE_ID unique  
   
 (1..65534) et APP_IS_ROOT=0. Un seul node a APP_IS_ROOT=1 (le gateway).  
   
 Tous partagent même APP_ESPNOW_PMK et APP_WIFI_CHANNEL.  
**Knowledge / skills / prompts spécifiques** : à fusionner depuis lexacare-kit/  
   
 et mesh-prototype/kit/ dans mbh-firmware/kit/ au prochain refactor.  
**Hors scope V1** : connectivité cloud du gateway (MQTT, HTTPS), BLE provisioning,  
   
 secure boot + flash encryption, multi-langue KWS, ESP-NOW Long Range mode,  
   
 reconciliation channel sur MESH_EVENT_CHANNEL_SWITCH.  
**mesh-prototype (laboratoire mesh standalone)**  
Arborescence :  
mesh-prototype/  
 ├── firmware/                       même structure que mbh-firmware mais sans LexaCare  
 │   └── main/app_main.c             juste un heartbeat + emission BENCH:{...}  
 └── tools/mesh_bench/               banc de test multi-node Python (Typer + Rich)  
   
**Usage** : valider une évolution du mesh_manager, du tr_espnow ou du  
   
 tr_wifimesh en isolation, sans le bruit du firmware LexaCare. Une fois  
   
 validé, copier les composants modifiés dans mbh-firmware/components/.  
**Décisions d'architecture figées** (héritées par mbh-firmware) :  
- Un seul radio WiFi → ESP-NOW et WIFI-MESH partagent le même canal.  
- **Non-LR mode** : 802.11b/g/n standard pour la coexistence WIFI-MESH.  
- **Tree routing maison** pour ESP-NOW : HELLO + parent selection par score.  
- **API unifiée**mesh_send(dst, payload, prio) / mesh_recv(callback).  
- **Failover trigger** : perte ≥ 20 % sur fenêtre 10 s avec ≥ 20 samples. Retour Primary après 3 probes consécutifs OK.  
**Philosophie d'usage avec Gemma 3 27B**  
Gemma 3 27B (~17 Go quantisé Q5_K_M, ou ~14 Go Q4) tourne sur le workstation  
   
 i9 / RTX 4070 via Ollama. C'est un modèle dense capable, supérieur en  
   
 raisonnement aux 4B/12B et confortable sur du contexte ~64K tokens.  
Comparé à Gemma 4B (qui demandait du découpage micro-tâche systématique),  
   
 27B peut :  
- **Charger plusieurs agents + skills + fichiers source** en même temps sans s'effondrer (contexte ~32K confortable, jusqu'à 64K avec patience).  
- **Raisonner sur des refactors transverses** (toucher 3-4 fichiers cohérents en un prompt) plutôt que d'exiger un découpage en N micro-prompts.  
- **Inférer une intention** sur des prompts informels — moins besoin de specs ultra-détaillées, mais le contexte (AGENTS.md + skill pertinent) reste critique pour éviter qu'il invente des APIs.  
**Ce qui reste vrai** :  
- **Charger le contexte explicitement** — Gemma 27B ne devine pas où sont tes fichiers. Référence AGENTS.md + le ou les agents pertinents en début de session.  
- **Skills et knowledge restent utiles** — pas pour compenser une faiblesse du modèle, mais pour fournir les  **valeurs canoniques** (constantes, GPIO, paramètres MFCC) qui ne doivent jamais être inventées.  
- **Bascule sur Claude** pour : décisions d'architecture initiale, debug profond multi-couches, code review critique avant commit. Gemma exécute, Claude conçoit.  
**Pratique recommandée** :  
Charge AGENTS.md + agents/firmware.md + skills/freertos-task/SKILL.md  
 + firmware/main/orchestrator.c.  
 Ajoute un timer FreeRTOS qui appelle gpio_set_level(LEXA_ALERT_GPIO, 0)  
 2s après chaque alerte. Respecte les conventions du AGENTS.md.  
   
Une demande comme ça (3-4 fichiers de contexte + 1 task bornée) marche  
   
 bien sur 27B. À éviter : "implémente tout l'orchestrator" sans contexte —  
   
 même 27B inventera des appels d'API qui n'existent pas.  
**Ressources externes**  
Si l'URL manque dans ces deux fichiers, demander à Mathieu de compléter  
   
 **Documentation de** ** Réference**  
   
Consulter `/home/mathieu/Bureau/Lexacare/knowledge/esp-idf-v6.0-docs` avant toute réponse touchant l'API ESP-IDF.  
Fichier d'entrée : `/home/mathieu/Bureau/Lexacare/knowledge/esp-idf-v6.0-docs/README.md`.  
Cible du projet : **ESP32-S3** (substituer `{IDF_TARGET_NAME}` → `ESP32-S3`).  
   
