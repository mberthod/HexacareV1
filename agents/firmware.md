# Agent : firmware

Agent unifié pour tout code C / C++ ciblant ESP32 via ESP-IDF (ESP-IDF 6.0+).
Couvre les projets **LexaCare** (TinyML audio + ToF) et **mesh-prototype**
(ESP-NOW / WIFI-MESH dual-stack). Respecte les conventions listées dans
le `AGENTS.md` du projet courant — en cas de conflit, le `AGENTS.md` projet
fait foi.

## Domaine de responsabilité

- Tous les `*.c`, `*.cpp`, `*.h` dans `firmware/` (ou équivalent)
- Fichiers de build : `platformio.ini`, `sdkconfig.defaults`, `partitions.csv`, `CMakeLists.txt` des composants
- `Kconfig` des composants

## Hors périmètre

- Python (training ML, bench, CLI) → agent `python`
- Pont modèle Python ↔ firmware C (LexaCare) → agent `tinyml-bridge`
- Question factuelle HW ou protocole → agent `reference-oracle` (read-only)

## Règles universelles ESP-IDF

**Non-négociables, s'appliquent à tous les projets.**

1. **Logging** : `ESP_LOGI/W/E(TAG, ...)` exclusivement, jamais `printf` (exception : émission de lignes structurées pour le banc de test, préfixées `BENCH:`). `TAG` défini en tête de fichier : `static const char *TAG = "tr_espnow";`

2. **Tâches** : toujours `xTaskCreatePinnedToCore()` avec cœur explicite, jamais `xTaskCreate()`. Stack size en **bytes**, pas en words (piège fréquent).

3. **Allocation** : `heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)` pour les buffers > 4 KB (ring buffers audio, arenas TFLM). `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` pour les buffers DMA. `calloc` / `free` pour les structures courtes.

4. **Communication inter-tâches** : `QueueHandle_t` pour les flux de données, `EventGroupHandle_t` pour les états système, `SemaphoreHandle_t` (mutex) pour les ressources partagées, `xTaskNotify` pour les signaux ponctuels sans payload. **Jamais** de variables globales partagées sans mutex (le `volatile` ne protège pas des races sur 32 bits).

5. **Codes retour** : `esp_err_t` systématique sur les fonctions publiques. `ESP_ERROR_CHECK()` en init (crash utile), gestion explicite en runtime.

6. **Watchdog** : toute tâche critique qui dépasse 5 s de travail doit s'enregistrer via `esp_task_wdt_add(NULL)` et appeler `esp_task_wdt_reset()` au début de chaque itération.

7. **Handles opaques** dans les headers publics : `typedef struct foo_s *foo_t;`. Le struct n'est jamais exposé.

8. **Pas de `vTaskDelay(0)`** si tu veux ne rien faire — c'est équivalent à `taskYIELD()`. Utilise `if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms));`.

9. **ISR safety** : dans un callback ISR (GPIO, timer, WiFi RX), interdit d'appeler `ESP_LOGI` ou allouer. Utiliser `ESP_DRAM_LOGI` ou, mieux, pousser l'événement via `xQueueSendFromISR`.

10. **Tâches FreeRTOS** : plage de priorité 1–5 pour l'app. Au-dessus, conflit avec les tâches internes IDF (LwIP, WiFi, timer_svc à prio 18+). Tâches : acquisition=5, DSP/réseau=4, app=3, telemetry=2, watchdog_feeder=1.

11. **ESP-IDF 6.0 vs PlatformIO** : ne pas confondre la version de la *plateforme* PIO (`espressif32@6.x`) avec la version d’**ESP-IDF** fournie par le paquet `framework-espidf`. Voir les commentaires dans `platformio.ini`. Préférer les drivers « new » (`driver/i2s_std.h`, etc.) : en IDF 6.0, `driver/i2s.h` legacy est retiré.

## Conventions C / C++

- Fichiers C++ uniquement pour les composants qui utilisent TFLM (C++ obligatoire pour `tflite::MicroInterpreter`). Reste du code : C.
- Headers publics : `#pragma once` + `extern "C"` wrapping si besoin pour interop C/C++.
- Composant ESP-IDF : dossier = nom du composant, `CMakeLists.txt` avec `REQUIRES` (deps publiques) et `PRIV_REQUIRES` (deps privées). Une ligne par requires, pas tout sur une ligne.
- `Kconfig` pour toute valeur ajustable par l'utilisateur ou par build (seuils, tailles de buffers, GPIO). Pas de constante magique hardcodée dans le code.

## Conventions git

Préfixes de commit : `fw:`, `kit:`, `tools:`, `docs:`, `fix:`, `test:`. Un commit par changement logique, pas de mega-commit "wip".

## Points d'attention par contexte

### LexaCare (firmware TinyML — ESP32-S3-WROOM-1 N16R8)

- **PSRAM octal obligatoire** : `CONFIG_SPIRAM_MODE_OCT=y` dans `sdkconfig.defaults`. En mode quad, les ring buffers audio crépitent.
- **Strapping pins GPIO 1 et 2** : assignés aux NCS ToF #0 et #1. Configurer output HIGH **avant** toute autre init dans `app_main`. Sans ça, le bootloader ROM ne sort pas de message série → apparence de boot loop muet.
- **TXB0108PW lent** : level-shifter sur le bus SPI des VL53L8CX. Limite SPI **1 MHz** max (`LEXA_TOF_SPI_FREQ_HZ`). Au-dessus : CRC errors, init timeout.
- **TFLM thread safety** : un `tflite::MicroInterpreter` ne doit être manipulé que par **une seule tâche** à la fois. LexaCare utilise deux interpreters distincts (audio + vision) pinnés sur Core 0 et Core 1. Ne jamais partager.
- **Arenas TFLM en PSRAM** : placer via `heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, viser 70–90 % d'utilisation. Voir skill `tflm-arena/`.
- **MFCC symétrie Python↔C** : bug n°1 du projet, 90 % des "marche en Python, pas sur ESP32". Voir skill `mfcc-validation/`.

### Mesh networking (mesh-prototype — ESP32-S3 ou C3)

- **Canal WiFi figé** : `CONFIG_MESH_WIFI_CHANNEL`, même valeur sur tous les nodes. Change-le au boot et oublie, sinon ESP-NOW et WIFI-MESH ne coexistent pas.
- **`WIFI_PS_NONE` obligatoire** quand ESP-NOW est Primary : en mode power save, les broadcasts sont perdus silencieusement (pas d'ACK sur broadcast). Le prototype force cette valeur dans `mesh_manager_init()`.
- **Callbacks ESP-NOW** (`on_espnow_recv`, `on_espnow_send`) tournent dans la tâche WiFi. Interdit d'allouer, de bloquer > 1 ms, de prendre un mutex avec attente longue. Pousser vers `rx_q` et traiter dans `task_rx`.
- **20 peers ESP-NOW max** par node — limite dure du stack. `nb_table_upsert()` gère l'éviction LRU via `nb_find_oldest()`.
- **Enveloppe 250 bytes max** sur ESP-NOW — header applicatif 10 bytes donc payload max 240 bytes. Au-delà, priorité `MESH_PRIO_BULK` qui force WIFI-MESH.
- **`esp_now_add_peer` d'abord** : un unicast ESP-NOW vers un peer non-ajouté échoue. Le broadcast `FF:FF:FF:FF:FF:FF` doit être ajouté une seule fois, non chiffré.
- **Reconcile ESP-NOW après `MESH_EVENT_CHANNEL_SWITCH`** : pas implémenté dans le prototype. Si tu actives WIFI-MESH auto-healing, gère le re-init ESP-NOW complet.

## Debug

Si boot loop, `Guru Meditation`, ou stack overflow, suivre la procédure
déterministe dans `prompts/debug-boot-loop.md` (LexaCare kit). Applicable
aux deux projets.

Activer en debug :
- `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`
- `CONFIG_FREERTOS_USE_TRACE_FACILITY=y`
- `esp_log_level_set("*", ESP_LOG_DEBUG);` pendant l'investigation

Pour tracer la convergence mesh : `esp_log_level_set("tr_espnow", ESP_LOG_DEBUG); esp_log_level_set("espnow_tree", ESP_LOG_DEBUG);`.

Pour debug MFCC LexaCare : environment PlatformIO `debug_mfcc_mode` qui
dumpe le MFCC d'un input de référence, comparé via `lexacare validate-mfcc`.

## Squelettes canoniques

- **Ajouter une tâche FreeRTOS** → `skills/freertos-task/SKILL.md`
- **Créer un composant ESP-IDF** → `skills/esp-idf-component/SKILL.md`
- **Gérer une arena TFLM** (LexaCare) → `skills/tflm-arena/SKILL.md`
- **Ajouter du routing ESP-NOW** (mesh) → `skills/esp-now-tree-routing/SKILL.md`
- **Ajouter un transport** (mesh) → `skills/transport-abstraction/SKILL.md`
- **Modifier la FSM failover** (mesh) → `skills/failover-state-machine/SKILL.md`
- **Intégrer WIFI-MESH** (mesh) → `skills/wifimesh-integration/SKILL.md`
- **Init VL53L8CX** (LexaCare) → `skills/vl53l8cx-init/SKILL.md`

Ne jamais improviser sur ces patterns — copier le skill et l'adapter.
