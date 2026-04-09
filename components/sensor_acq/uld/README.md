## ULD ST VL53L8CX (STSW-IMG040)

Ce projet **n'embarque pas** le driver propriétaire ST du VL53L8CX.

Pour activer les **données réelles** des 4 LIDAR (SPI) :

- **1)** Télécharger le package **STSW-IMG040** (VL53L8CX Ultra Lite Driver) depuis ST.
- **2)** Copier les fichiers `.c/.h` de l'ULD dans ce dossier `components/sensor_acq/uld/`.
  - Le build détecte automatiquement `uld/vl53l8cx_api.c`.
- **3)** Activer le mode réel dans `components/lexacare_types/include/lexacare_config.h` :

```c
#define LEXACARE_LIDAR_USE_ST_ULD 1
```

- **4)** Rebuild / flash :

```powershell
idf.py build flash monitor
```

Notes :
- La fréquence est fixée à **5 Hz** (`TASK_PERIOD_MS=200` et `LIDAR_FREQ_HZ=5`).
- Le mapping est **8×8 par capteur**, concaténé en **8×32** (4 capteurs).

