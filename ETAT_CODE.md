# ETAT DU CODE — mbh-firmware
> Dernière mise à jour : 2026-04-21 (synthèse post-plan : env `production`,
> tâches capture/MFCC/mesh bench, correction `tr_wifimesh.c`, doc failover).
> Lire ce fichier EN ENTIER avant de toucher au code.

---

## 1. Structure du projet

```
firmware/
├── platformio.ini              envs : production, capture_*, model_*, mesh_only, test_mesh, debug_mfcc
├── sdkconfig.defaults          PSRAM octal, WiFi, Mesh (voir §3.1)
├── partitions.csv              dual OTA + NVS + littlefs
├── CMakeLists.txt              boilerplate IDF (3 lignes, ne pas modifier)
├── main/
│   ├── CMakeLists.txt          déclare les SRCS et les REQUIRES
│   ├── app_main.c              point d'entrée, séquence de boot (sous-ensembles par mode)
│   ├── app_config.h            flags de build, paramètres app (seuils, cooldown)
│   ├── lexa_config.h           GPIO, MFCC, arenas TFLM, fréquences bus
│   ├── app_events.h            audio_event_t, vision_event_t, fall_voice_alert_t
│   ├── task_helpers.h          déclarations task_capture / mfcc debug / mesh bench
│   ├── task_audio.c            Core 0 prio 5 : I2S → MFCC → TFLM audio
│   ├── task_vision.c           Core 1 prio 5 : ToF → TFLM vision
│   ├── task_capture.c          streaming dataset (stdout / USB-Serial-JTAG)
│   ├── task_mfcc_debug.c       harness MFCC → logs tag `BENCH`
│   ├── task_mesh_bench.c       heartbeat BENCH + envoi test_mesh
│   └── orchestrator.c          Core 1 prio 3 : fusion fall+voice → mesh_send
└── components/
    ├── mesh_manager/           FSM failover ESP-NOW ↔ WIFI-MESH (implémenté)
    ├── tr_espnow/              transport ESP-NOW (implémenté)
    ├── tr_wifimesh/            transport WIFI-MESH (**STUB** — voir §2)
    ├── i2s_stereo_mic/         driver I2S (STUB — voir §3.2)
    ├── mfcc_dsp/               MFCC C (STUB — voir §3.3)
    ├── tflm_dual_runtime/      2 interpréteurs TFLM (STUB — voir §3.4)
    ├── vl53l8cx_array/         4× VL53L8CX SPI (STUB — voir §3.5)
    └── pca9555_io/             I2C expander (STUB — voir §3.6)
```

---

## 2. Compilation — `tr_wifimesh` (historique et état actuel)

**Résolu :** `components/tr_wifimesh/CMakeLists.txt` ne référence plus un
composant CMake inexistant `mesh`. Le transport WiFi-mesh est un **STUB**
(`tr_wifimesh_send` → `ESP_ERR_NOT_SUPPORTED`) documenté dans
`components/tr_wifimesh/include/tr_wifimesh.h` (stratégie de failover et
chemin vers une impl `esp_mesh_*`).

**Correction de sécurité :** une ancienne version de `tr_wifimesh.c` dupliquait
par erreur des symboles de `mesh_manager.c` (`mesh_manager_status`, etc.) —
cela a été supprimé pour éviter les erreurs de linkage.

**Risque runtime :** si la FSM bascule sur `MESH_TRANSPORT_WIFIMESH`, les
envois passant par `tr_wifimesh_send` **échouent** tant que le stub n'est pas
remplacé par une impl réelle.

---

## 3. PROBLÈMES NON-BLOQUANTS (fonctionnalité capteurs / ML encore partielle)

### 3.2 — STUB : `i2s_stereo_mic`

**Fichier :** `components/i2s_stereo_mic/src/i2s_stereo_mic.c`

**État actuel :** Retourne du silence (`memset(out, 0, ...)`). L'API est correcte
et stable (`i2s_stereo_mic_init`, `i2s_stereo_mic_read_latest`).

**Ce qu'il faut implémenter :**
1. Init I2S RX (port `I2S_NUM_0`) en mode stéréo 16 kHz 16 bits, PDM ou standard
   selon les micros utilisés. Sur LexaCare V1 : standard I2S (pas PDM).
2. Ring buffer circulaire en PSRAM (128 KB = ~2 sec de signal stéréo).
3. `i2s_stereo_mic_read_latest` doit copier les N derniers samples MONO
   (downmix L+R / 2) depuis le ring buffer.
4. En IDF 5.x, utiliser l'API `esp_driver_i2s` : `i2s_new_channel`,
   `i2s_channel_init_std_mode`, `i2s_channel_enable`, `i2s_channel_read`.

**Gotcha critique :**
- Les samples I2S sont en 32 bits sign-extended. Extraire le vrai int16 par `>> 16`.
- Voir `knowledge/gotchas.md` § "Micros I2S — canal L vs R".
- BCLK=GPIO5, WS=GPIO6, DIN=GPIO7 (voir `lexa_config.h`).

**CMakeLists.txt du composant :** actuellement `REQUIRES driver`.
En IDF 5.x, remplacer par `REQUIRES esp_driver_i2s esp_ringbuf`.

---

### 3.3 — STUB : `mfcc_dsp`

**Fichier :** `components/mfcc_dsp/src/mfcc_dsp.c`

**État actuel :** Retourne un vecteur de zéros. Pas de `mfcc_dsp_init()` dans
l'API (normal — la version actuelle n'en a pas, contrairement à l'ancienne).

**Ce qu'il faut implémenter :**
Pipeline DSP en C, bit-équivalente avec librosa Python :
1. Pre-emphasis : `y[n] = x[n] - 0.97 * x[n-1]` (LEXA_MFCC_PREEMPH = 0.97f)
2. Fenêtrage Hamming sur N_FFT=512 samples
3. FFT via ESP-DSP (`dsps_fft2r_fc32`) — la bibliothèque `esp-dsp` est déjà
   dans les dépendances managed_components (voir `dependencies.lock`).
4. Puissance spectrale → banc de filtres mel (N_MEL=40, fmin=20 Hz, fmax=8000 Hz)
5. Log des énergies mel
6. DCT-II → N_COEFF=13 coefficients

**Paramètres canoniques (ne pas modifier sans valider) :**
```c
LEXA_MFCC_SAMPLE_RATE_HZ = 16000
LEXA_MFCC_N_FFT          = 512
LEXA_MFCC_HOP            = 256
LEXA_MFCC_N_MEL          = 40
LEXA_MFCC_N_COEFF        = 13
LEXA_MFCC_FMIN_HZ        = 20
LEXA_MFCC_FMAX_HZ        = 8000
LEXA_MFCC_PREEMPH        = 0.97f
```

**Gotcha :** La dérive Python↔C MFCC est le bug n°1 de ce projet. Voir
`knowledge/gotchas.md` § "Dérive MFCC Python ↔ C". Ne pas implémenter sans
avoir une procédure de validation.

---

### 3.4 — STUB : `tflm_dual_runtime`

**Fichiers :** `components/tflm_dual_runtime/src/tflm_dual_runtime.c`

**État actuel :** Retourne label 0 avec confiance 30% (sous tous les seuils).
Pas de vrais interpréteurs TFLM, pas d'arenas.

**Ce qu'il faut implémenter :**
Ce composant DOIT être en C++ (TFLM n'a pas d'API C pure).
Renommer en `tflm_dual_runtime.cpp`.

1. Deux arenas PSRAM :
   ```c
   static uint8_t *s_arena_audio  = NULL;  // LEXA_TFLM_ARENA_AUDIO_BYTES = 128 KB
   static uint8_t *s_arena_vision = NULL;  // LEXA_TFLM_ARENA_VISION_BYTES = 64 KB
   // Allouer via heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
   ```
2. Deux `tflite::MicroInterpreter` distincts, un par tâche.
3. Charger les modèles depuis `main/models/model_audio.h` et `model_vision.h`
   (générés par l'outil Python — voir `agents/tinyml-bridge.md`).
4. `tflm_dual_runtime_init()` : allouer arenas + `AllocateTensors()`.
5. `tflm_dual_infer_audio()` : copier MFCC dans le tensor input, `Invoke()`,
   lire softmax output → argmax + confiance.

**Gotcha :** `MicroInterpreter` n'est pas thread-safe. `tflm_dual_infer_audio`
est appelé UNIQUEMENT depuis `task_audio` (Core 0), `tflm_dual_infer_vision`
UNIQUEMENT depuis `task_vision` (Core 1). Ne jamais croiser.

**CMakeLists.txt du composant :** ajouter `espressif__esp-tflite-micro` dans
les REQUIRES (composant managed). Voir comment le référencer dans
`idf_component_register`.

---

### 3.5 — STUB : `vl53l8cx_array`

**Fichier :** `components/vl53l8cx_array/src/vl53l8cx_array.c`

**État actuel :** Retourne une mire synthétique 16×8 floats. L'API est stable.

**Ce qu'il faut implémenter :**
1. Init bus SPI via `esp_driver_spi` (IDF 5.x), host SPI2_HOST ou SPI3_HOST.
   CLK=GPIO4, MOSI=GPIO15, MISO=GPIO21, freq=1 MHz (limite TXB0108PW).
2. Init séquentielle de chaque capteur via LPn (PCA9555) et NCS GPIO :
   - Activer LPn[i] via `pca9555_write_output`
   - Attendre 10 ms
   - Envoyer les registres d'init VL53L8CX via SPI (driver ULD ST)
3. `vl53l8cx_array_read_frame` : polling des 4 capteurs, assemblage 16×8,
   normalisation float [0,1] (distance / 4000 mm, invalid = 0.0f).

**Gotcha :** SPI à 1 MHz MAX à cause du TXB0108PW. Au-dessus : CRC errors
silencieux, VL53L8CX retourne 0x00 ou 0xFF. Voir `knowledge/gotchas.md`.

**CMakeLists.txt :** `REQUIRES esp_driver_spi driver` (pca9555_io est un
PRIV_REQUIRES).

---

### 3.6 — STUB : `pca9555_io`

**Fichier :** `components/pca9555_io/src/pca9555_io.c`

**État actuel :** Retourne `ESP_OK` sans rien faire.

**Ce qu'il faut implémenter :**
Communication I2C avec le PCA9555 à l'adresse 0x20.
En IDF 5.x, utiliser `esp_driver_i2c` : `i2c_master_bus_create`,
`i2c_master_bus_add_device`, `i2c_master_transmit`, `i2c_master_receive`.

**Registres PCA9555 :**
- `0x02` (Output Port 0), `0x03` (Output Port 1) : écriture état GPIO
- `0x00` (Input Port 0), `0x01` (Input Port 1) : lecture état GPIO
- `0x06` (Configuration 0), `0x07` (Configuration 1) : 0=output, 1=input

**API actuelle manquante :** Il faut ajouter une fonction pour les LPn ToF.
L'`app_main.c` actuel ne l'appelle pas (il init PCA puis gère les LPn dans
`vl53l8cx_array`). L'API `pca9555_write_output(uint8_t values)` opère sur le
port 0 (bits 0..7). Documenter quel bit correspond à quel LPn.

**CMakeLists.txt :** `REQUIRES esp_driver_i2c`.

---

## 4. INCOHÉRENCES MINEURES

### 4.1 — `partitions.csv` : en-tête

L’en-tête pointe désormais vers **mbh-firmware**. La table des partitions
reste la référence (dual OTA + NVS + littlefs).

**Attention :** La table dans `knowledge/memory-budget.md` §Flash diffère de
`partitions.csv`. Le `partitions.csv` du firmware est la référence, pas le
document. Mettre à jour le doc si nécessaire.

### 4.2 — `main/CMakeLists.txt` inclut les composants mesh unconditionnellement

Même en `capture_mode` et `debug_mfcc` (qui ont `MBH_DISABLE_MESH=1`),
le `main/CMakeLists.txt` déclare :
```cmake
REQUIRES mesh_manager tr_espnow tr_wifimesh
```
Ces composants sont compilés mais leurs `#if !MBH_DISABLE_MESH` dans le code C
empêchent leur initialisation au runtime. C'est acceptable pour l'instant si
les composants compilent.

### 4.3 — `orchestrator.c` : GPIO LED alerte

**Corrigé :** `gpio_config_t` pour `LEXA_ALERT_GPIO` initialise explicitement
tous les champs ; `gpio_config` est sous `ESP_ERROR_CHECK`. Le paramètre
inutilisé de `check_fusion` a été retiré.

### 4.4 — `models/` vide

`firmware/main/models/` ne contient que `.gitkeep`. Les fichiers `model_audio.h`
et `model_vision.h` sont nécessaires pour l'implémentation réelle de
`tflm_dual_runtime`. Ils sont générés par les outils Python (voir
`agents/tinyml-bridge.md`). Hors scope du firmware pur.

### 4.5 — Modes capture / MFCC / mesh bench

**Implémenté :**
- `task_capture.c` : `capture_audio` → trames binaires sur **stdout** (magics
  `LXCA` / `LXCL`, voir commentaires en tête de fichier). `capture_lidar` →
  grille 16×8 float32 LE.
- `task_mfcc_debug.c` : une fenêtre I2S → `mfcc_compute` → ligne JSON
  préfixée `BENCH:` (tag log `BENCH`) une fois par seconde.
- `task_mesh_bench.c` : `mesh_only` → logs `BENCH:` périodiques ;
  `test_mesh` → idem + `mesh_send` broadcast DATA léger.

**Note :** le flux capture binaire peut se mélanger aux logs ESP sur le même
port série ; filtrer côté hôte ou désactiver le niveau de log verbeux.

---

## 5. ÉTAT DES COMPOSANTS — RÉSUMÉ

| Composant | État | Priorité implémentation |
|-----------|------|------------------------|
| `mesh_manager` | Implémenté | — |
| `tr_espnow` | Implémenté | — |
| `tr_wifimesh` | STUB (send NOT_SUPPORTED) | Haut (vraie stack esp_mesh) |
| `pca9555_io` | STUB | Moyen (nécessaire hardware) |
| `i2s_stereo_mic` | STUB | Haut (pipeline audio) |
| `mfcc_dsp` | STUB | Haut (pipeline audio) |
| `tflm_dual_runtime` | STUB | Haut (inférence) |
| `vl53l8cx_array` | STUB | Haut (pipeline vision) |

---

## 6. ORDRE D'IMPLÉMENTATION PRIORITAIRE (hardware → ML)

Ordre conservé pour maximiser la valeur par étape (dépendances matérielles) :

1. **`pca9555_io`** (I2C réel) — LPn ToF, IO board.
2. **`i2s_stereo_mic`** — RX + ring PSRAM ; valider avec `capture_audio` + hôte.
3. **`vl53l8cx_array`** — SPI 1 MHz + ULD ST ; valider avec `capture_lidar`.
4. **`mfcc_dsp`** — bit-équivalence librosa ; valider avec `debug_mfcc` + outil Python.
5. **`tflm_dual_runtime`** (C++, arenas PSRAM) — après `main/models/*.h`.
6. **`tr_wifimesh`** — remplacer le stub par `esp_mesh_*` quand l’IDF / la cible
   et le menuconfig le permettent ; sinon documenter l’environnement de build
   officiel (IDF natif hors PIO si nécessaire).

Les tâches `task_capture`, `task_mfcc_debug`, `task_mesh_bench` servent déjà
de banc d’intégration pour les étapes 2–4 et le mesh ESP-NOW.

---

## 7. FLAGS DE BUILD — ÉTAT CORRECT

Les flags sont cohérents entre `platformio.ini` et `app_config.h` / `app_main.c`.

| Environnement PlatformIO | Flags définis | Comportement |
|--------------------------|---------------|--------------|
| `production` | `MBH_MODE_FULL=1` | audio + vision + orchestrateur + mesh |
| `capture_audio` | `MBH_MODE_CAPTURE_AUDIO=1`, `MBH_DISABLE_MESH=1` | I2S minimal + `task_capture` (stdout) |
| `capture_lidar` | `MBH_MODE_CAPTURE_LIDAR=1`, `MBH_DISABLE_MESH=1` | ToF minimal + `task_capture` |
| `model_*` / `model_both` | `MBH_MODE_MODEL_*` | inférence seule (mesh désactivé sauf both + flags) |
| `mesh_only` | `MBH_MODE_MESH_ONLY=1` | pas d’init capteurs ; mesh + `task_mesh_bench` |
| `test_mesh` | `MBH_MODE_TEST_MESH=1` | mesh + bench + envois broadcast |
| `debug_mfcc` | `MBH_MODE_DEBUG_MFCC=1`, `MBH_DISABLE_MESH=1` | I2S minimal + `task_mfcc_debug` |

**Note :** `app_config.h` fournit des valeurs par défaut à 0 pour tous les
flags manquants, et génère un `#error` si aucun mode n'est défini. C'est
le bon comportement. **`APP_IS_ROOT` vaut 0 par défaut** : pour le gateway
root en prod, définir explicitement `-DAPP_IS_ROOT=1` (et un `APP_NODE_ID`
unique) dans `platformio.ini` ou l’équivalent CMake.

---

## 8. CONVENTIONS À RESPECTER (pour Gemma)

Tout le code de ce projet suit les conventions de `AGENTS.md` à la racine.
Résumé rapide :

- Logging : `ESP_LOGI/W/E(TAG, ...)` exclusivement, jamais `printf` (exception
  documentée AGENTS : lignes `BENCH:` ; le mode `capture_*` utilise `fwrite` sur
  `stdout` pour données binaires, pas pour du texte de debug)
- `static const char *TAG = "nom_composant";` en tête de fichier
- `xTaskCreatePinnedToCore()` obligatoire (jamais `xTaskCreate`)
- Stack en **bytes** (ex: 8192 = 8 KB)
- Buffers > 4 KB : `heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`
- Retours : `esp_err_t` sur toutes les fonctions publiques
- `#pragma once` sur tous les headers
- snake_case pour variables/fonctions, UPPER_CASE pour macros
- Commentaires en français
- Doxygen sur toutes les fonctions publiques
