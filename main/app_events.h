/* app_events.h — types d'événements partagés entre task_audio, task_vision
 * et orchestrator.
 *
 * Format packed pour que la struct puisse être sérialisée direct vers le
 * mesh (mesh_send payload).
 */
#pragma once

#include <stdint.h>

/* ------------------------------------------------------------------
 * Event émis par task_audio après une inférence KWS
 * ------------------------------------------------------------------ */
typedef struct {
    uint16_t label_idx;       /* 0=_silence, 1=_unknown, 2=aide, ... */
    uint8_t  confidence_pct;  /* 0..100 */
    uint32_t timestamp_ms;    /* xTaskGetTickCount en ms */
} audio_event_t;

/* ------------------------------------------------------------------
 * Event émis par task_vision après une inférence chute
 * ------------------------------------------------------------------ */
typedef struct {
    uint16_t label_idx;       /* 0=debout, 1=chute, 2=assis, 3=absent */
    uint8_t  confidence_pct;
    uint32_t timestamp_ms;
} vision_event_t;

/* ------------------------------------------------------------------
 * Alerte émise par l'orchestrator après fusion fall+voice.
 * Packed pour envoi direct via mesh_send.
 * ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint16_t src_node_id;              /* qui a détecté */
    uint8_t  audio_label;              /* label KWS */
    uint8_t  audio_conf_pct;
    uint8_t  vision_label;             /* label fall */
    uint8_t  vision_conf_pct;
    uint32_t audio_timestamp_ms;
    uint32_t vision_timestamp_ms;
} fall_voice_alert_t;
