/* app_config.h — configuration projet mbh-firmware
 *
 * Identité du node, clés, et paramètres top-level. Les paramètres
 * "techniques" (GPIO, MFCC, arenas) sont dans lexa_config.h.
 */
#pragma once

/* ------------------------------------------------------------------
 * Identité node (unique sur le réseau mesh)
 * ------------------------------------------------------------------ */
#ifndef APP_NODE_ID
#define APP_NODE_ID         0x0001
#endif

#ifndef APP_IS_ROOT
#define APP_IS_ROOT         0
#endif

/* ------------------------------------------------------------------
 * Mesh — sécurité ESP-NOW et canal WiFi
 * ------------------------------------------------------------------ */
#define APP_ESPNOW_PMK  { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18, \
                          0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90 }

#ifndef APP_WIFI_CHANNEL
#define APP_WIFI_CHANNEL    6
#endif

/* ------------------------------------------------------------------
 * Orchestrator — logique de fusion fall+voice
 * ------------------------------------------------------------------ */

/* Fenêtre de corrélation : fall ET voice détectés dans ce delta → alerte */
#define APP_FUSION_WINDOW_MS        200

/* Seuils de confiance minimale pour considérer une détection valide */
#define APP_AUDIO_CONF_MIN_PCT      70
#define APP_VISION_CONF_MIN_PCT     80

/* Cooldown post-alerte : pas de nouvelle alerte pendant N secondes */
#define APP_ALERT_COOLDOWN_SEC      10

/* ------------------------------------------------------------------
 * Compile-time mode flags (définis par platformio.ini via -D)
 * ------------------------------------------------------------------ */
/* Mode flags – choisissez exactement un mode */
#ifndef MBH_MODE_FULL
#define MBH_MODE_FULL         0
#endif
#ifndef MBH_MODE_CAPTURE_AUDIO
#define MBH_MODE_CAPTURE_AUDIO 0
#endif
#ifndef MBH_MODE_CAPTURE_LIDAR
#define MBH_MODE_CAPTURE_LIDAR 0
#endif
#ifndef MBH_MODE_MODEL_AUDIO
#define MBH_MODE_MODEL_AUDIO 0
#endif
#ifndef MBH_MODE_MODEL_LIDAR
#define MBH_MODE_MODEL_LIDAR 0
#endif
#ifndef MBH_MODE_MODEL_BOTH
#define MBH_MODE_MODEL_BOTH 0
#endif
#ifndef MBH_MODE_MESH_ONLY
#define MBH_MODE_MESH_ONLY   0
#endif
#ifndef MBH_MODE_TEST_MESH
#define MBH_MODE_TEST_MESH   0
#endif
#ifndef MBH_MODE_DEBUG_MFCC
#define MBH_MODE_DEBUG_MFCC   0
#endif
#ifndef MBH_DISABLE_MESH
#define MBH_DISABLE_MESH      0
#endif

/* Flux USB multiplexé (LXCS + LXCL + JSON) pour tools/lexa_live_monitor.py.
 * Uniquement avec MBH_MODE_FULL : une seule lecture I2S (task_audio) + une
 * acquisition ToF (task_vision) alimentent la télémétrie. */
#ifndef MBH_USB_TELEMETRY_STREAM
#define MBH_USB_TELEMETRY_STREAM 0
#endif

/* Lignes ASCII `FRAME:v0,...,v255` (mm) sur stdout — même format que
 * LEXACARE_ARDUINO + tools/read_lexa_tof_frame.py. Incompatible avec
 * MBH_USB_TELEMETRY_STREAM (LXCS/LXCL binaires sur le même flux). */
#ifndef MBH_SERIAL_ASCII_TOF_FRAME
#define MBH_SERIAL_ASCII_TOF_FRAME 0
#endif

#if MBH_USB_TELEMETRY_STREAM && MBH_SERIAL_ASCII_TOF_FRAME
#error "MBH_SERIAL_ASCII_TOF_FRAME est incompatible avec MBH_USB_TELEMETRY_STREAM"
#endif

#if MBH_SERIAL_ASCII_TOF_FRAME && MBH_CAPTURE_BINARY_TO_STDOUT
#error "MBH_SERIAL_ASCII_TOF_FRAME est incompatible avec MBH_CAPTURE_BINARY_TO_STDOUT"
#endif

/* Capture audio/lidar : envoyer le flux binaire (magics LXCA/LXCL) sur stdout.
 * Désactivé par défaut pour garder USB/ttyACM lisible (logs seuls). Activer
 * avec -DMBH_CAPTURE_BINARY_TO_STDOUT=1 (voir env capture_*_host dans platformio.ini). */
#ifndef MBH_CAPTURE_BINARY_TO_STDOUT
#define MBH_CAPTURE_BINARY_TO_STDOUT 0
#endif

/* PCM int16 continu sur stdout (sans entête LXCA). Pour enregistrer : record_lexa_audio.py --raw-pcm.
 * Exclusif avec MBH_CAPTURE_BINARY_TO_STDOUT et MBH_CAPTURE_ASCII_SERIAL_PLOTTER. */
#ifndef MBH_CAPTURE_RAW_PCM_TO_STDOUT
#define MBH_CAPTURE_RAW_PCM_TO_STDOUT 0
#endif

/* Mode capture audio : envoyer des entiers ASCII (une valeur / ligne) pour
 * Arduino IDE > Outils > Traceur série (ou Serial Studio, etc.). Exclu si
 * MBH_CAPTURE_BINARY_TO_STDOUT=1. Voir env capture_audio_plot. */
#ifndef MBH_CAPTURE_ASCII_SERIAL_PLOTTER
#define MBH_CAPTURE_ASCII_SERIAL_PLOTTER 0
#endif

/* Taille fenêtre : 1 ligne = moyenne(int16) sur N échantillons (~16000/N Hz affiché).
 * Plus N est petit, plus le tracé suit l’onde (mais débit série ↑). 64 ≈ 250 Hz. */
#ifndef MBH_CAPTURE_PLOT_DECIM
#define MBH_CAPTURE_PLOT_DECIM 64
#endif

/* 0 = traceur : moyenne int16 brute (recommandé). >0 multiplie la moyenne (debug seulement,
 * risque de valeurs « figées » si le PCM réel est quasi nul). */
#ifndef MBH_CAPTURE_PLOT_LINE_GAIN
#define MBH_CAPTURE_PLOT_LINE_GAIN 0
#endif
#ifndef MBH_CAPTURE_PLOT_LINE_CLAMP
#define MBH_CAPTURE_PLOT_LINE_CLAMP 3000000
#endif

#if (MBH_CAPTURE_BINARY_TO_STDOUT + MBH_CAPTURE_RAW_PCM_TO_STDOUT + MBH_CAPTURE_ASCII_SERIAL_PLOTTER) > 1
#error "Un seul mode capture USB : binaire LXCA, raw PCM, ou traceur ASCII"
#endif

#if ( (MBH_MODE_FULL + MBH_MODE_CAPTURE_AUDIO + MBH_MODE_CAPTURE_LIDAR + MBH_MODE_MODEL_AUDIO + MBH_MODE_MODEL_LIDAR + MBH_MODE_MODEL_BOTH + MBH_MODE_MESH_ONLY + MBH_MODE_TEST_MESH + MBH_MODE_DEBUG_MFCC) != 1 )
#error "Exactly one MBH_MODE_* macro must be set to 1"
#endif

#if MBH_USB_TELEMETRY_STREAM && !MBH_MODE_FULL
#error "MBH_USB_TELEMETRY_STREAM requiert MBH_MODE_FULL=1"
#endif
