# mbh-firmware

Firmware unifié ESP32-S3 combinant le stack **LexaCare V1** (détection
chute multi-capteur + reconnaissance vocale TinyML) et le stack
**mesh-prototype** (ESP-NOW primaire longue portée + ESP-WIFI-MESH fallback).

Cible type : déploiement multi-chambres en EHPAD où chaque dispositif
LexaCare détecte des événements localement et les propage via le mesh
vers un gateway central.

## Architecture globale

```
ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB PSRAM octal)

  Core 0 (APP_CPU)              Core 1 (PRO_CPU)
  ┌───────────────────┐         ┌───────────────────┐
  │ task_audio        │         │ task_vision       │
  │ I2S → MFCC → TFLM │         │ ToF SPI → TFLM    │
  │ (arena 128 KB)    │         │ (arena 64 KB)     │
  │ prio 5            │         │ prio 5            │
  └────────┬──────────┘         └────────┬──────────┘
           │                             │
           └──────────┬──────────────────┘
                      ▼
           ┌───────────────────┐
           │ orchestrator      │ Core 1 prio 3
           │ fall+voice fusion │
           │ → mesh_send()     │◄──┐
           └────────┬──────────┘   │
                    ▼              │
           ┌───────────────────────┴──┐
           │ mesh_manager             │ Core 0 prio 4
           │ FSM failover unifié      │
           └──┬────────────────────┬──┘
              ▼                    ▼
        ┌──────────┐         ┌──────────┐
        │tr_espnow │         │tr_wifimesh│
        │(primary) │         │(fallback)│
        └────┬─────┘         └────┬─────┘
             └────────┬───────────┘
                      ▼
                   WiFi radio
                 (canal fixé)
```

## Capacités

- **Détection chute** : 4× VL53L8CX ToF via SPI partagé (1 MHz max, limite TXB0108PW), assemblage frame 16×8, inférence TFLM vision
- **Reconnaissance vocale** : 2× micros I2S MEMS, préemphase + MFCC bit-équivalent Python/C, inférence TFLM audio (KWS)
- **Fusion événement** : orchestrator corrèle fall + voice sur fenêtre 200 ms → émission alerte
- **Propagation mesh** : alerte envoyée via `mesh_send()` vers `MESH_NODE_ROOT` (gateway) en priorité CONTROL (jamais droppée). ESP-NOW primaire longue portée, bascule WIFI-MESH si loss > 20 %.
- **Mode capture** : environment PlatformIO dédié qui streame audio + ToF brut sur USB-CDC pour la constitution de datasets (désactive l'inférence et le mesh)

## Structure

```
mbh-firmware/
├── README.md
├── firmware/
│   ├── platformio.ini              3 envs : production, capture_mode, debug_mfcc
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── app_main.c              init NVS + WiFi + LexaCare stack + mesh stack
│   │   ├── app_config.h            node_id, PMK, canal, GPIO, paramètres app
│   │   ├── lexa_config.h           MFCC, arenas TFLM, fréquences bus
│   │   ├── task_audio.c            I2S → MFCC → TFLM audio
│   │   ├── task_vision.c           ToF SPI → TFLM vision
│   │   ├── orchestrator.c          fusion fall+voice + mesh_send() alertes
│   │   ├── app_events.h            types d'événements partagés
│   │   └── models/                 .h générés par l'outil PC
│   └── components/
│       ├── mesh_manager/           FSM failover + API unifiée mesh_send/recv
│       ├── tr_espnow/              transport primaire (impl complète)
│       ├── tr_wifimesh/            transport fallback (impl complète esp_mesh_*)
│       ├── i2s_stereo_mic/         driver I2S + ring buffer PSRAM (stub)
│       ├── mfcc_dsp/               MFCC C++ ESP-DSP (stub)
│       ├── tflm_dual_runtime/      2 arenas TFLM isolées (stub)
│       ├── vl53l8cx_array/         ULD ST + logique 4 capteurs (stub)
│       └── pca9555_io/             I2C @ 0x20 contrôle LPn (stub)
```

Les composants marqués "impl complète" viennent du mesh-prototype et
compilent tel quel. Les composants "stub" ont l'API publique conforme à
ce que attend l'app, mais l'implémentation interne est minimaliste (logs
"not implemented" + valeurs simulées) — à brancher sur le code réel LexaCare
existant chez Mathieu.

## Build

```bash
cd firmware/
pio run -e production -t upload    # production : inférence + mesh actifs
pio run -e capture_mode -t upload  # streaming dataset, pas d'inférence ni mesh
pio device monitor
```

## Déploiement multi-nodes

Chaque dispositif a son `APP_NODE_ID` unique (1..65534) et `APP_IS_ROOT=0`.
Un seul node (le gateway qui a un uplink internet ou BLE) a `APP_IS_ROOT=1`.
Tous les nodes partagent le même `APP_ESPNOW_PMK` et `APP_WIFI_CHANNEL`.

Une alerte fall+voice détectée localement remonte au root via le mesh
(ESP-NOW si conditions radio OK, WIFI-MESH sinon). Le root fait ensuite
son business : BLE vers le smartphone du proche aidant, push notification
via backend cloud, allumage d'une lampe, etc. — hors scope de ce firmware.
