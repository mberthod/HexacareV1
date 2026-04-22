# Memory budget — LexaCare V1

ESP32-S3-WROOM-1 **N16R8** : 16 MB flash QIO, 8 MB PSRAM octal @ 80 MHz.

## Vue d'ensemble

| Ressource | Total | Budget V1 | Marge |
|-----------|-------|-----------|-------|
| DRAM interne (SRAM1 user heap) | ~320 KB | 180 KB | ~140 KB libre |
| IRAM interne | ~128 KB | ~80 KB (ESP-IDF core) | ~48 KB libre |
| PSRAM | 8192 KB | 400 KB | ~7.7 MB libre |
| Flash | 16384 KB | ~2.2 MB app + ~256 KB NVS | large |

## DRAM interne (critique — pas extensible)

Usage par composant estimé :

| Composant | DRAM | Notes |
|-----------|------|-------|
| ESP-IDF core (heap) | ~60 KB | LwIP désactivé en V1, sinon +80 KB |
| Stacks FreeRTOS | ~32 KB | 8 KB × 4 tâches (audio, vision, orch, supervisor) |
| Queues + mutex + event groups | ~4 KB | petites structures |
| TFLM runtime (hors arena) | ~20 KB | interpreter state × 2 |
| Driver I2S + DMA descriptors | ~12 KB | descriptors en DRAM obligatoire |
| Driver SPI + DMA | ~8 KB | idem |
| Divers (logs, constantes ro) | ~10 KB | |
| **Sous-total** | **~146 KB** | marge confortable sur 320 KB |

**Règle** : tout buffer > 4 KB doit aller en PSRAM (`MALLOC_CAP_SPIRAM`), pas
en DRAM.

## PSRAM (8 MB — large)

Usage par composant :

| Composant | PSRAM | Notes |
|-----------|-------|-------|
| Audio ring buffer | 128 KB | 2 sec × 16 kHz × stéréo × 2 bytes |
| MFCC workspace | 8 KB | FFT temp + mel + DCT |
| TFLM arena audio | 128 KB | `LEXA_TFLM_ARENA_AUDIO_KB` |
| TFLM arena vision | 64 KB | `LEXA_TFLM_ARENA_VISION_KB` |
| ToF frame buffers (4 capteurs) | 32 KB | 8×8 × 4 bytes × 4 capteurs × 16 frames |
| Capture mode USB-CDC buffer | 64 KB | uniquement en build capture_mode |
| **Sous-total** | **~424 KB** | < 6 % de 8 MB, énorme marge |

Les 7.5 MB restants sont disponibles pour V2 (BLE cache, historique long,
bufferisation offline MQTT, etc.).

## Flash (16 MB)

Partition actuelle (voir `firmware/partitions.csv`) :

```
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x200000,         # 2 MB
ota_0,    app,  ota_0,   0x210000, 0x200000,         # 2 MB
ota_1,    app,  ota_1,   0x410000, 0x200000,         # 2 MB
otadata,  data, ota,     0x610000, 0x2000,
storage,  data, littlefs,0x612000, 0x9EE000,         # ~10 MB reste
```

Note : en V1 sans OTA actif, on peut retirer ota_0/ota_1/otadata et donner
tout à `factory` + `storage`. À décider avant première release.

## Stack FreeRTOS par tâche

Tailles recommandées, à vérifier après build avec `uxTaskGetStackHighWaterMark()`.

| Tâche | Stack | Mesuré high-water (target) |
|-------|-------|----------------------------|
| `task_audio` | 8192 bytes | < 5500 bytes (utilisation 67 %) |
| `task_vision` | 8192 bytes | < 5500 bytes |
| `orchestrator` | 4096 bytes | < 2500 bytes |
| `task_supervisor` | 3072 bytes | < 1800 bytes |
| `task_capture` (capture mode) | 4096 bytes | < 3000 bytes |
| IDF internes (timer, ipc) | défaut IDF | ne pas toucher |

**Alerte** : si high-water dépasse 80 % du stack alloué en phase test,
augmenter de 2 KB. Plutôt gaspiller 2 KB de DRAM qu'avoir un stack overflow
en prod (silencieux et catastrophique).

Activer en debug : `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`.

## Arènes TFLM : dérivation

- **Audio KWS ConvNet** (~50k params Int8) : arena 96 KB minimum, budget 128 KB
- **Vision fall detector** (ToF 16×8 → FC) (~5k params Int8) : arena 48 KB minimum, budget 64 KB

Méthode de mesure : voir `skills/tflm-arena/SKILL.md` §Sizing arena.

## Budget temporel (latence, pour info)

Mesuré sur ESP32-S3 @ 240 MHz, PSRAM octal 80 MHz, `-O2` release :

| Opération | Latence typique | Marge pour 50 Hz (20 ms) |
|-----------|-----------------|---------------------------|
| MFCC 1 frame (25 ms audio) | ~2.8 ms | OK |
| Inférence KWS | ~1.5 ms | OK |
| Acquisition 4 ToF (résolution 8×8) | ~34 ms | un peu juste à 30 Hz |
| Inférence vision | ~1.2 ms | OK |

Conclusion : audio largement dans le budget 50 Hz, vision limite à 15 Hz avec
4 capteurs (acceptable pour détection chute, qui ne nécessite pas > 10 Hz).

## Ce qu'il faut vérifier après chaque changement de modèle

1. `pio run -s` → lire la section "Memory use" en fin de build
2. Loguer `esp_get_free_heap_size()` et `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` au démarrage
3. Après 10 min d'inférence, reloguer — si le free heap a chuté de > 8 KB, il y a un leak
4. `uxTaskGetStackHighWaterMark(NULL)` à la fin de chaque itération de boucle des tâches principales (en debug)
