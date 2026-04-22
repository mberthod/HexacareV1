# mbh-firmware

Firmware ESP32-S3 unifiant le stack **LexaCare V1** (ToF multi-capteurs + pipeline audio TinyML + fusion d’événements) et le stack **mesh** (ESP-NOW primaire, bascule vers transport secondaire selon la FSM `mesh_manager`).

Cible type : déploiement multi-nodes (EHPAD) où chaque dispositif exécute l’inférence localement et propage les alertes vers un nœud root.

## Documentation associée

| Fichier | Contenu |
|---------|---------|
| `docs/CODE_PRINCIPAL.md` | Flux `main/` : init, files, orchestrateur, garde-fous macros |
| `ETAT_CODE.md` | État des stubs, risques runtime mesh secondaire, gotchas |
| `AGENTS.md` | Conventions kit MBHREP / ESP-IDF / Python |

## Matériel cible

- **SoC** : ESP32-S3 (référence devkit PlatformIO : `esp32-s3-devkitc-1`).
- **Mémoire** : flash 16 MB, PSRAM activée via `-DBOARD_HAS_PSRAM` dans les envs courants (`platformio.ini`).
- **Schéma applicatif** : pinout et constantes dans `main/lexa_config.h` (SPI ToF 2 MHz, trame ToF assemblée **32×8**, I2S 16 kHz stéréo, arenas TFLM **128 KB** audio / **64 KB** vision en PSRAM).

## Architecture logicielle

### Vue d’ensemble (mode `MBH_MODE_FULL`)

```
                    boot app_main
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
    strapping NCS    NVS + netif     mesh_manager (si mesh actif)
    ToF HIGH         event loop          │
          │               │               │
          └───────────────┴───────────────┘
                          │
          drivers LexaCare (PCA9555, ToF, TFLM, I2S, option USB télémétrie)
                          │
  Core 0 (APP_CPU)                    Core 1 (PRO_CPU)
  ┌─────────────────────┐            ┌─────────────────────┐
  │ task_audio          │            │ task_vision           │
  │ I2S → MFCC → TFLM   │            │ 4× VL53L8CX → frame   │
  │ prio 5, stack 8 KB  │            │ 32×8 → TFLM vision    │
  │ → audio_event_q     │            │ prio 5, stack 8 KB    │
  └──────────┬──────────┘            │ → vision_event_q      │
             │                        └──────────┬────────────┘
             │                                   │
             └─────────────────┬─────────────────┘
                               ▼
                    ┌─────────────────────┐
                    │ orchestrator      │ prio 3, stack 4 KB
                    │ fusion fenêtre ms   │
                    │ GPIO alerte + mesh  │
                    └──────────┬──────────┘
                               │
                    ┌──────────▼──────────┐
                    │ mesh_manager       │ tâche interne Core 0 prio 4
                    │ FSM transport      │
                    └──┬──────────────┬──┘
                       ▼              ▼
                 tr_espnow      tr_wifimesh
                 (primaire)     (fallback — voir `ETAT_CODE.md`)
                       └──────┬──────┘
                              ▼
                        WiFi / canal fixe
```

### Séquence d’initialisation (`app_main.c`)

1. **GPIO NCS ToF** : toutes les broches `LEXA_TOF_NCS_*` en sortie HIGH avant tout autre périphérique (strapping + CS SPI inactifs).
2. **NVS** : init + erase si pages pleines ou nouvelle version.
3. **`esp_netif_init`** puis **`esp_event_loop_create_default`**.
4. **Drivers** : branchement selon `MBH_MODE_*` (I2S seul, ToF seul, stack complet, ou aucun capteur pour les modes mesh-only / test_mesh).
5. **`mesh_manager_init`** si `MBH_DISABLE_MESH` vaut 0 à la compilation.
6. **Tâches** : création selon le même mode (voir § Modes d’exécution).

### Tâches applicatives (référence mode full)

| Tâche | Fichier | Cœur | Priorité FreeRTOS | Stack (octets) | Sortie principale |
|-------|---------|------|-------------------|------------------|-------------------|
| `task_audio` | `task_audio.c` | 0 | 5 | 8192 | `audio_event_q` (`audio_event_t`) |
| `task_vision` | `task_vision.c` | 1 | 5 | 8192 | `vision_event_q` (`vision_event_t`) |
| `orch` | `orchestrator.c` | 1 | 3 | 4096 | GPIO `LEXA_ALERT_GPIO`, `mesh_send()` si mesh actif |
| `mesh_manager` (interne) | `mesh_manager.c` | 0 | 4 | 4096 | Routage TX/RX, FSM failover |

Périodicité indicative dans le source : inférence audio **500 ms** par fenêtre ; boucle vision **100 ms** (10 Hz cible).

### Fusion et alertes (`orchestrator.c` + `app_config.h`)

- Fenêtre de corrélation audio + vision : **`APP_FUSION_WINDOW_MS`** (200 ms par défaut).
- Seuils minimum sur les événements avant envoi en file : **`APP_AUDIO_CONF_MIN_PCT`** (70), **`APP_VISION_CONF_MIN_PCT`** (80).
- L’orchestrator ne traite comme « intéressants » que certains indices de label (audio index **2** « aide », vision index **1** « chute » dans le source actuel — à aligner sur les modèles déployés).
- Cooldown entre alertes : **`APP_ALERT_COOLDOWN_SEC`** (10 s).
- Payload mesh : `fall_voice_alert_t` (`app_events.h`), priorité **`MESH_PRIO_CONTROL`**.

### Télémétrie USB (option)

- Macro **`MBH_USB_TELEMETRY_STREAM`** : uniquement avec **`MBH_MODE_FULL`** (erreur de préprocesseur sinon).
- Code : `task_usb_telemetry.c`, init conditionnelle dans `app_main.c` si `MBH_MODE_FULL && MBH_USB_TELEMETRY_STREAM`.
- Outil hôte typique : `tools/lexa_live_monitor.py` ; débit série élevé — fermer le moniteur série intégré pendant la capture. Référence config : `tools/lexa_live_config.example.json` (mention dans `platformio.ini`).

### Flux série ToF ASCII (option full)

- **`MBH_SERIAL_ASCII_TOF_FRAME`** : lignes distance mm sur stdout pour scripts type `tools/read_lexa_tof_frame.py`.
- **Incompatible** avec `MBH_USB_TELEMETRY_STREAM` (deux usages du même flux).

## Modes d’exécution (`MBH_MODE_*`)

Exactement **une** macro `MBH_MODE_*` doit valoir `1` (`app_config.h`, contrôle à la compilation).

| Macro | Rôle | Init capteurs / ML (résumé) | Tâches typiques |
|-------|------|------------------------------|-----------------|
| `MBH_MODE_FULL` | Production / intégration complète | PCA9555, board, ToF, TFLM, I2S ; USB télémétrie si flag dédié | `task_audio`, `task_vision`, `orchestrator` [+ USB] |
| `MBH_MODE_CAPTURE_AUDIO` | Dataset audio, pas d’inférence | I2S uniquement | `task_capture` |
| `MBH_MODE_CAPTURE_LIDAR` | Dataset ToF | PCA9555 + ToF | `task_capture` |
| `MBH_MODE_MODEL_AUDIO` | Bench inférence audio seule | Stack LexaCare complet (branche `#else` `app_main`) | `task_audio` |
| `MBH_MODE_MODEL_LIDAR` | Bench inférence vision seule | Idem | `task_vision` |
| `MBH_MODE_MODEL_BOTH` | Bench double pipeline + fusion | Idem | `task_audio`, `task_vision`, `orchestrator` |
| `MBH_MODE_MESH_ONLY` | Radio mesh sans capteurs LexaCare | Aucun driver LexaCare | `task_mesh_bench` |
| `MBH_MODE_TEST_MESH` | Mesh + trafic d’essai | Aucun driver LexaCare | `task_mesh_bench` |
| `MBH_MODE_DEBUG_MFCC` | Harness MFCC / BENCH | I2S uniquement | `task_mfcc_debug` |

`mesh_manager_init` n’est appelé que si **`MBH_DISABLE_MESH`** vaut **0** à la compilation (`app_main.c`). Les envs `model_*`, `capture_*`, `debug_mfcc` du `platformio.ini` actuel définissent **`MBH_DISABLE_MESH=1`** : pas d’init mesh. Les modes `model_*` conservent en revanche l’init matérielle LexaCare complète (branche `#else`), indépendamment du mesh.

## Environnements PlatformIO

**Répertoire de travail** : celui qui contient `platformio.ini` (dans ce dépôt : racine du projet firmware, pas un sous-dossier `firmware/`).

**Défaut courant** : `default_envs = capture_audio` dans `platformio.ini`.

**Plateforme** : `espressif32@6.9.0` (framework ESP-IDF embarqué par PlatformIO : voir commentaires dans `platformio.ini` sur la correspondance numéros PIO vs IDF).

### Table des environnements

| Environnement | Mode (`-D`) | Autres `build_flags` notables | Mesh | `monitor_speed` / filtres |
|---------------|-------------|-------------------------------|------|---------------------------|
| `production` | `MBH_MODE_FULL=1` | `BOARD_HAS_PSRAM` | Actif | 921600, decode + colorize |
| `production_usb_telemetry` | `MBH_MODE_FULL=1` | `MBH_USB_TELEMETRY_STREAM=1`, `BOARD_HAS_PSRAM` | Actif | 921600, decode sans colorize |
| `production_tof_frame_ascii` | `MBH_MODE_FULL=1` | `MBH_SERIAL_ASCII_TOF_FRAME=1`, `BOARD_HAS_PSRAM` | Actif | **115200**, decode sans colorize |
| `capture_audio` | `MBH_MODE_CAPTURE_AUDIO=1` | `BOARD_HAS_PSRAM`, `MBH_DISABLE_MESH=1` | Désactivé | 921600 |
| `capture_audio_plot` | idem capture audio | `MBH_CAPTURE_ASCII_SERIAL_PLOTTER=1` | Désactivé | 921600 |
| `capture_audio_host` | idem | `MBH_CAPTURE_BINARY_TO_STDOUT=1` | Désactivé | 921600 |
| `capture_audio_raw` | idem | `MBH_CAPTURE_RAW_PCM_TO_STDOUT=1` | Désactivé | 921600 |
| `capture_lidar` | `MBH_MODE_CAPTURE_LIDAR=1` | `BOARD_HAS_PSRAM`, `MBH_DISABLE_MESH=1` | Désactivé | 921600 |
| `capture_lidar_host` | idem capture lidar | `MBH_CAPTURE_BINARY_TO_STDOUT=1` | Désactivé | 921600 |
| `model_audio` | `MBH_MODE_MODEL_AUDIO=1` | `BOARD_HAS_PSRAM`, `MBH_DISABLE_MESH=1` | Désactivé | 921600 |
| `model_lidar` | `MBH_MODE_MODEL_LIDAR=1` | idem | Désactivé | 921600 |
| `model_both` | `MBH_MODE_MODEL_BOTH=1` | idem | Désactivé | 921600 |
| `mesh_only` | `MBH_MODE_MESH_ONLY=1` | `BOARD_HAS_PSRAM` | Actif | 921600 |
| `test_mesh` | `MBH_MODE_TEST_MESH=1` | `BOARD_HAS_PSRAM` | Actif | 921600 |
| `debug_mfcc` | `MBH_MODE_DEBUG_MFCC=1` | `BOARD_HAS_PSRAM`, `MBH_DISABLE_MESH=1` | Désactivé | 921600 |

### Macros de sortie USB (modes capture)

Au plus **une** sortie « stdout » parmi : `MBH_CAPTURE_BINARY_TO_STDOUT`, `MBH_CAPTURE_RAW_PCM_TO_STDOUT`, `MBH_CAPTURE_ASCII_SERIAL_PLOTTER` (`#error` dans `app_config.h` si violation).

Exemples d’outils (détails dans les commentaires de `platformio.ini` et les en-têtes) :

- Capture WAV depuis flux binaire : `python3 tools/record_lexa_audio.py --port … -o …`
- PCM brut : `record_lexa_audio.py --raw-pcm --skip-bytes …`

## Compilation et flash

```bash
pio run -e production -t upload
pio device monitor -e production
```

Changer `-e` selon la table ci-dessus. Pour la télémétrie USB binaire, éviter d’ouvrir le moniteur sur le même port que l’outil Python.

## Identité mesh et surcharges de build

Valeurs par défaut dans `main/app_config.h` : `APP_NODE_ID`, `APP_IS_ROOT`, `APP_WIFI_CHANNEL`, `APP_ESPNOW_PMK`.

Exemple de surcharge gateway (à adapter) :

```bash
pio run -e production -t upload --project-option="build_flags=-DAPP_IS_ROOT=1 -DAPP_NODE_ID=0x0001"
```

(Préférer une section `build_flags` dédiée dans un env personnalisé si la ligne devient lourde.)

## Arborescence utile du dépôt

```
.
├── platformio.ini
├── sdkconfig.defaults
├── partitions.csv
├── README.md
├── ETAT_CODE.md
├── AGENTS.md
├── docs/
│   └── CODE_PRINCIPAL.md
├── main/
│   ├── app_main.c
│   ├── app_config.h
│   ├── lexa_config.h
│   ├── app_events.h
│   ├── task_audio.c
│   ├── task_vision.c
│   ├── orchestrator.c
│   ├── task_capture.c
│   ├── task_usb_telemetry.c
│   ├── task_mfcc_debug.c
│   ├── task_mesh_bench.c
│   └── CMakeLists.txt
└── components/
    ├── mesh_manager/
    ├── tr_espnow/
    ├── tr_wifimesh/
    ├── i2s_stereo_mic/
    ├── mfcc_dsp/
    ├── tflm_dual_runtime/
    ├── vl53l8cx_array/
    └── pca9555_io/
```

L’état **stub / complet** des composants non-mesh est décrit dans **`ETAT_CODE.md`** (ne pas supposer un comportement métier sans lire ce fichier).

## Capacités résumées

- **Vision** : quatre VL53L8CX, SPI partagé, trame fusionnée **32×8** pour l’inférence (`vl53l8cx_array`).
- **Audio** : I2S stéréo 16 kHz, fenêtre 1 s, MFCC paramétrés dans `lexa_config.h`, inférence TFLM audio.
- **Fusion** : corrélation temporelle puis alerte locale + **`mesh_send`** vers `MESH_NODE_ROOT` en priorité contrôle.
- **Mesh** : ESP-NOW en primaire ; stratégie de perte et fallback documentées dans `mesh_manager` / `ETAT_CODE.md`.

## Hors scope firmware

Connectivité cloud du gateway, provisioning BLE avancé, politique produit côté serveur ou mobile : hors de ce dépôt.
