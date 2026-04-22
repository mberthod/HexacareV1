# LexaCare TinyML — Équivalent local d'Edge Impulse

Outil complet pour entraîner et déployer des modèles TinyML sur ESP32-S3-WROOM-1 N16R8
pour le projet **LexaCare V1** (MBHREP / ELEC-CORE Projet 054).

Cible : 4× VL53L8CX (détection de chute) + 2× micros I2S (reconnaissance vocale).

---

## Architecture

```
lexacare-tinyml/
├── firmware/                   Projet ESP-IDF (PlatformIO)
│   ├── platformio.ini
│   ├── sdkconfig.defaults
│   ├── main/                   Tâches FreeRTOS dual-core
│   │   ├── app_main.c          Init + création des tâches
│   │   ├── task_audio.c        Core 0 : I2S + MFCC + inférence voix
│   │   ├── task_vision.c       Core 1 : ToF + inférence chute
│   │   └── orchestrator.c      Corrélation chute + audio → alerte
│   └── components/
│       ├── i2s_stereo_mic/     Driver I2S double micro + ring buffer PSRAM
│       ├── mfcc_dsp/           MFCC C++ via ESP-DSP (golden-ref librosa)
│       ├── tflm_dual_runtime/  2 arenas TFLM isolées en PSRAM
│       ├── vl53l8cx_array/     Ton driver SPI existant (à porter Arduino→IDF)
│       └── pca9555_io/         Contrôle LPn des ToF (I2C @ 0x20)
│
└── tool/                       Pipeline ML en Python (le "cerveau PC")
    ├── lexacare_cli.py         Point d'entrée CLI (Typer)
    ├── collector/              Capture dataset via série/WiFi
    ├── trainer/                Entraînement TF/Keras + golden MFCC
    ├── exporter/               .tflite Int8 → .h pour firmware
    └── webviewer/              Dashboard FastAPI (datasets + training)
```

## Le différentiel face à Edge Impulse / Gemini

| Brique | Edge Impulse | Gemini (conseil) | Cet outil |
|--------|--------------|-------------------|-----------|
| DSP audio (MFCC) | Boîte noire `ei_run_classifier` | "à coder vous-même" | `mfcc_dsp/` + golden ref Python validé bit-à-bit |
| Orchestration 2 IA | Non supporté | Décrit, pas livré | 2 arenas TFLM isolées, Core 0/1 pinnés |
| Hardware LexaCare | Pas de template | Advice générique | PCA9555 + TXB0108PW + 4× VL53L8CX SPI intégrés |
| Déploiement | Cloud dependency | Manuel | CLI locale, offline total |
| Datasets | Limité (plan gratuit) | - | Illimité, fichiers bruts |

## Workflow utilisateur

```bash
# 1. Installer l'outil
cd tool && pip install -e .

# 2. Flasher le firmware en mode "capture"
cd ../firmware && pio run -e capture_mode -t upload

# 3. Collecter un dataset (l'ESP32 streame par série)
lexacare collect audio --label "aide" --duration 60
lexacare collect tof --label "chute" --duration 30

# 4. Entraîner
lexacare train audio --model keyword_spotting
lexacare train tof --model fall_detection

# 5. Exporter les modèles en .h
lexacare export --target firmware/main/models/

# 6. Reflasher en mode "inference"
cd ../firmware && pio run -e inference_mode -t upload

# 7. Dashboard web (optionnel)
lexacare serve  # http://localhost:8000
```

## État des phases

- [x] **Phase 0** — Architecture et squelette firmware compilable
- [ ] **Phase 1** — MFCC C++ + golden ref + CLI collect (EN COURS)
- [ ] **Phase 2** — Pipeline training + export .h
- [ ] **Phase 3** — Web viewer + firmware_gen paramétrique
- [ ] **Phase 4** — Intégration ESP-DL (alternative à TFLM pour +30% perf)

## Hardware de référence (LexaCare V1)

| Signal | GPIO | Note |
|--------|------|------|
| ToF SPI CLK | 4 | Partagé entre les 4 VL53L8CX |
| ToF SPI MOSI | 15 | via TXB0108PW (level-shift 3.3V↔1.8V) |
| ToF SPI MISO | 21 | via TXB0108PW |
| ToF NCS 0..3 | 1, 2, 42, 41 | Attention GPIO1/2 = strapping pins |
| ToF LPn 0..3 | PCA9555 P0-P3 | I2C @ 0x20 |
| I2C SDA (PCA) | 11 | |
| I2C SCL (PCA) | 12 | |
| I2S BCLK | 5 | Micros stéréo |
| I2S WS | 6 | |
| I2S DIN | 7 | Data des 2 micros multiplexés L/R |

PSRAM : 8 MB Octal @ 80 MHz (critique pour ring buffer audio + arenas TFLM).
