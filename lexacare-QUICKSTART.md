# QUICKSTART — LexaCare TinyML

Marche à suivre de zéro à un firmware qui infère des mots-clés + chute en local.

## 0. Prérequis

- **PlatformIO** installé (`pip install platformio` ou extension VSCode)
- **Python 3.10+**
- **Linux** (testé sur Ubuntu 24/25), avec droits sur `/dev/ttyACM*`
  ```bash
  sudo usermod -a -G dialout $USER
  ```

## 1. Installation de l'outil Python

```bash
cd lexacare-tinyml/tool
python -m venv .venv
source .venv/bin/activate
pip install -e .
```

Vérifie :
```bash
lexacare --help
python -m tests.test_mfcc_sanity   # doit passer les 3 tests
```

## 2. Premier build firmware (mode capture)

Le mode capture ne fait AUCUNE inférence — il streame les données brutes pour que le PC constitue le dataset.

```bash
cd ../firmware
pio run -e capture_mode           # compile
pio run -e capture_mode -t upload # flashe
```

Si la première compil échoue sur `esp-tflite-micro` ou `esp-dsp`, c'est normal — PlatformIO va fetcher les composants Espressif la première fois. Relance la compil.

## 3. Porter ton driver VL53L8CX

Va dans `firmware/components/vl53l8cx_array/`, lis le README, remplace `stub.c` par les 4 fichiers (`vl53l8cx_api.c` ULD ST + ton `platform.c` adapté + un `vl53l8cx_array.c` pour la logique multi-capteur). Update le `CMakeLists.txt` avec les vrais sources.

Tant que c'est pas fait, le firmware tourne avec le stub (mire synthétique) et le `task_capture_vision` envoie quand même des frames bidon — pratique pour tester la chaîne PC sans hardware complet.

## 4. Collecter ton premier dataset

Dans un terminal (firmware flashé en capture_mode, ESP32 branché) :

```bash
# Audio — tu prononces "aide, aide, aide..." pendant 30s
lexacare collect audio -l aide -d 30 -p /dev/ttyACM0

# Silence (classe indispensable en KWS)
lexacare collect audio -l _silence -d 30

# Parole non-mot-clé (classe "unknown")
lexacare collect audio -l _unknown -d 60

# ToF — tu te mets debout devant les capteurs
lexacare collect tof -l debout -d 30

# etc.
```

Recommandation KWS : **au moins 3 classes** (`_silence`, `_unknown`, ton mot-clé), idéalement 500+ exemples par classe. Plus = mieux.

## 5. Entraîner

```bash
cd tool
lexacare train audio --epochs 30
# → trained/audio_kws.tflite + audio_kws.labels.txt

lexacare train tof --epochs 40
# → trained/tof_fall.tflite + tof_fall.labels.txt
```

## 6. Exporter dans le firmware

```bash
lexacare export
# → firmware/main/models/audio_kws_int8.h
# → firmware/main/models/vision_fall_int8.h
```

## 7. Build inference mode + flash

```bash
cd ../firmware
pio run -e inference_mode -t upload
pio device monitor                  # voir les logs
```

Tu devrais voir :
```
I (xxx) lexa_main: LexaCare V1 — ELEC-CORE Projet 054
I (xxx) tflm_dual: audio model loaded, arena used=82432 / 131072
I (xxx) tflm_dual: vision model loaded, arena used=14336 / 65536
I (xxx) audio: MFCC 2843µs | inf 1247µs | label=0 conf=0.92
I (xxx) vision: frame 34221µs | label=0 conf=0.88
```

Si les tailles d'arenas `used` sont trop proches du max, augmente `LEXA_TFLM_ARENA_AUDIO_KB` / `LEXA_TFLM_ARENA_VISION_KB` dans `main/lexa_config.h`.

## 8. Validation MFCC Python ↔ ESP32 (optionnel mais crucial)

Si tes modèles tournent sans rien reconnaître malgré un bon taux de validation en Python, c'est presque toujours une dérive entre le MFCC Python (utilisé en training) et le MFCC C (utilisé à l'inférence).

TODO dans une prochaine release : ajouter un mode `debug-mfcc` au firmware qui dump un MFCC connu → compare avec `lexacare validate-mfcc`.

## 9. Dashboard (optionnel)

```bash
lexacare serve
# → http://localhost:8000
```

## Troubleshooting

| Symptôme | Cause probable | Fix |
|----------|---------------|-----|
| Boot loop / `Guru Meditation` au démarrage | PSRAM pas montée | Vérifier `CONFIG_SPIRAM_MODE_OCT=y` dans sdkconfig.defaults |
| `AllocateTensors failed` | Arena trop petite | Augmenter `LEXA_TFLM_ARENA_*_KB` dans lexa_config.h |
| `op not registered` | Modèle utilise un op non inclus | Ajouter l'op dans `resolver.Add...()` dans tflm_dual_runtime.cpp |
| Serial capture « CRC mismatch » fréquent | USB CDC saturé | Réduire chunk_frames dans task_capture.c, ou utiliser UART hard |
| IA marche en Python mais pas sur ESP32 | Dérive MFCC | Exécuter `lexacare validate-mfcc` |
| ToF toujours à 0 | Level-shifter TXB0108PW trop lent | Baisser `LEXA_TOF_SPI_FREQ_HZ` à 1MHz |
