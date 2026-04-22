# Documentation du code applicatif — mbh-firmware

Ce document décrit le **flux runtime** du répertoire `main/` et les **macros de mode** qui le conditionnent. Pour la **matrice complète des environnements PlatformIO**, l’**architecture détaillée** et le **matériel cible**, voir `README.md`. L’état d’implémentation des composants et les risques mesh : `ETAT_CODE.md`.

Les constantes matérielles (GPIO, MFCC, bus) sont dans `lexa_config.h`. Les seuils applicatifs, l’identité mesh et les flags de build sont dans `app_config.h`.

---

## Point d’entrée : `app_main.c`

### Ordre d’initialisation (mode « stack LexaCare » complet)

1. GPIO NCS ToF : sorties HIGH avant tout autre périphérique (`init_strapping_pins`).
2. NVS, `esp_netif`, event loop par défaut.
3. Drivers capteurs et ML selon le mode (voir tableau ci-dessous).
4. `mesh_manager_init` si `MBH_DISABLE_MESH` vaut 0.
5. Création des tâches selon le mode.

En `MBH_MODE_FULL` avec `MBH_USB_TELEMETRY_STREAM`, les niveaux de log globaux sont forcés à `ESP_LOG_ERROR` sur la plupart des tags pour limiter le bruit sur le même flux que la télémétrie binaire.

---

## Modes de build : une macro `MBH_MODE_*` active

La contrainte compile-time est dans `app_config.h` : exactement un parmi les modes suivants doit valoir `1`.

| Mode | Init matériel principal | Tâches démarrées |
|------|-------------------------|------------------|
| `MBH_MODE_FULL` | PCA9555, board capteurs, ToF SPI, TFLM dual, I2S ; option `usb_telemetry_init` si `MBH_USB_TELEMETRY_STREAM` | `task_audio`, `task_vision`, `orchestrator` ; `usb_telemetry_start` si flux USB |
| `MBH_MODE_CAPTURE_AUDIO` | I2S uniquement | `task_capture` (streaming USB / formats capture) |
| `MBH_MODE_CAPTURE_LIDAR` | PCA9555, ToF SPI | `task_capture` |
| `MBH_MODE_MODEL_AUDIO` | Même init matérielle que le full (branche `#else` de `app_main`) : PCA9555, ToF, TFLM, I2S ; pas de `usb_telemetry_init` (réservé à `MBH_MODE_FULL`) | `task_audio` |
| `MBH_MODE_MODEL_LIDAR` | Idem branche `#else` | `task_vision` |
| `MBH_MODE_MODEL_BOTH` | Idem branche `#else` | `task_audio`, `task_vision`, `orchestrator` |
| `MBH_MODE_MESH_ONLY` | pas d’init LexaCare | `task_mesh_bench` |
| `MBH_MODE_TEST_MESH` | pas d’init LexaCare | `task_mesh_bench` |
| `MBH_MODE_DEBUG_MFCC` | I2S uniquement | `task_mfcc_debug` |

Les environnements PlatformIO qui posent ces macros sont listés dans `platformio.ini` (commentaires inclus pour les flux USB et outils associés).

`mesh_manager_init` s’exécute pour tout binaire où `MBH_DISABLE_MESH` vaut 0, y compris les modes `MBH_MODE_MODEL_*`, sauf si l’environnement de build force explicitement le mesh désactivé.

---

## `task_audio.c` — cœur 0, priorité 5

- Entrée : `task_audio_entry` ; démarrage : `task_audio_start`.
- File globale : `audio_event_t` vers `audio_event_q` (consommée par `orchestrator`).
- Fenêtre PCM : `AUDIO_WINDOW_SAMPLES` (= 16000) échantillons mono 16 bits ; période d’inférence : 500 ms (`INFER_PERIOD_MS`).
- Chaîne : lecture ring I2S (`i2s_stereo_mic_read_latest` ou, si `MBH_USB_TELEMETRY_STREAM`, lecture interleaved + canal L vers MFCC + enqueue télémétrie PCM) → `mfcc_compute` → `tflm_dual_infer_audio`.
- Émission d’événement si `label_idx >= 0` et `confidence_pct >= APP_AUDIO_CONF_MIN_PCT` (défaut 70 dans `app_config.h`).
- Watchdog : tâche enregistrée puis reset dans la boucle.

---

## `task_vision.c` — cœur 1, priorité 5

- Entrée : `task_vision_entry` ; démarrage : `task_vision_start`.
- File globale : `vision_event_q` avec `vision_event_t`.
- Dimensions de trame : macros `VL53L8CX_ARRAY_FRAME_W` / `H` du composant ToF ; période 100 ms (10 Hz).
- Chaîne : `vl53l8cx_array_read_frame` → normalisation interne dans le buffer float → `tflm_dual_infer_vision` ; option `MBH_SERIAL_ASCII_TOF_FRAME` pour émission ASCII ; option `MBH_USB_TELEMETRY_STREAM` pour poster la frame à la télémétrie USB.
- Émission si confiance ≥ `APP_VISION_CONF_MIN_PCT` (défaut 80). L’orchestrator filtre en plus sur l’index de label « chute » (voir ci-dessous).

---

## `orchestrator.c` — cœur 1, priorité 3

- Consomme `audio_event_q` et `vision_event_q` (réception non bloquante dans la boucle principale du fichier).
- Garde les derniers événements « intéressants » : audio label index `AUDIO_LABEL_AIDE` (= 2 par défaut dans le source), vision label index `VISION_LABEL_CHUTE` (= 1).
- Fusion : si les deux derniers événements qualifiés ont des horodatages à ≤ `APP_FUSION_WINDOW_MS` (200 ms par défaut), appel `emit_alert`.
- Alerte : GPIO `LEXA_ALERT_GPIO` à 1 ; si mesh non désactivé, `mesh_send` vers `MESH_NODE_ROOT` avec payload `fall_voice_alert_t`, priorité `MESH_PRIO_CONTROL`, drapeau ACK requis.
- Cooldown : `APP_ALERT_COOLDOWN_SEC` (10 s par défaut) entre deux alertes.

---

## Types d’événements : `app_events.h`

- `audio_event_t` : `label_idx`, `confidence_pct`, `timestamp_ms`.
- `vision_event_t` : même structure, sémantique des labels documentée en commentaire dans le header.
- `fall_voice_alert_t` : struct packée pour envoi mesh (node source, labels et confidences audio/vision, timestamps).

---

## Flux USB et exclusivités (`app_config.h`)

- `MBH_USB_TELEMETRY_STREAM` : réservé à `MBH_MODE_FULL` ; multiplexage pour outil hôte `tools/lexa_live_monitor.py` (voir commentaires dans `app_config.h` et en-têtes associés).
- `MBH_SERIAL_ASCII_TOF_FRAME` : lignes `FRAME:` sur stdout ; incompatible avec `MBH_USB_TELEMETRY_STREAM` (erreur de préprocesseur).
- Modes capture stdout : `MBH_CAPTURE_BINARY_TO_STDOUT`, `MBH_CAPTURE_RAW_PCM_TO_STDOUT`, `MBH_CAPTURE_ASCII_SERIAL_PLOTTER` — au plus un actif (garde-fou `#error` dans `app_config.h`).

---

## Fichiers complémentaires dans `main/`

| Fichier | Rôle succinct |
|---------|----------------|
| `task_capture.c` | Modes capture audio ou lidar vers USB (magics / formats selon flags). |
| `task_mfcc_debug.c` | Mode `MBH_MODE_DEBUG_MFCC` : harness MFCC, sorties type bench. |
| `task_mesh_bench.c` | Modes mesh-only / test mesh : heartbeat et trafic d’essai. |
| `task_usb_telemetry.c` | Initialisation et tâche de streaming USB en mode télémétrie (symboles `usb_telemetry_*`). |
| `task_helpers.h` | Déclarations des points d’entrée des tâches ci-dessus pour `app_main`. |

---

## Lecture recommandée

1. `app_main.c` pour la vérité runtime sur l’ordre d’init et les branches `#if`.
2. `app_config.h` pour les seuils et incompatibilités de macros.
3. `ETAT_CODE.md` pour le détail des stubs, risques mesh secondaire et gotchas matériels.
