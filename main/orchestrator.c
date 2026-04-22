/* orchestrator.c — fusion fall+voice → alerte mesh
 *
 * C'est ICI que LexaCare et mesh se rencontrent. L'orchestrator :
 *   1. Consomme audio_event_q et vision_event_q
 *   2. Corrèle : fall détecté ET voice détectée (mot-clé "aide", etc.)
 *      dans une fenêtre glissante APP_FUSION_WINDOW_MS
 *   3. Si fusion OK : construit fall_voice_alert_t, appelle mesh_send()
 *      en priorité MESH_PRIO_CONTROL vers MESH_NODE_ROOT
 *   4. Cooldown APP_ALERT_COOLDOWN_SEC pour éviter le spam d'alertes
 *
 * Règle : une classe audio / vision est "intéressante" si label_idx matche
 * la whitelist ci-dessous. Par défaut :
 *   - audio : label "aide" (index 2 dans les labels par défaut)
 *   - vision : label "chute" (index 1)
 */
#include <string.h>
#include "app_config.h"
#include "app_events.h"
#include "mesh_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "lexa_config.h"

static const char *TAG = "orch";

/* Queues publiées par task_audio et task_vision */
extern QueueHandle_t audio_event_q;
extern QueueHandle_t vision_event_q;

/* Labels d'intérêt (à ajuster selon les modèles entraînés) */
#define AUDIO_LABEL_AIDE    2
#define VISION_LABEL_CHUTE  1

static audio_event_t  s_last_audio  = { .timestamp_ms = 0 };
static vision_event_t s_last_vision = { .timestamp_ms = 0 };
static uint32_t       s_last_alert_ms = 0;

static bool is_interesting_audio(const audio_event_t *e)
{
    return e->label_idx == AUDIO_LABEL_AIDE
        && e->confidence_pct >= APP_AUDIO_CONF_MIN_PCT;
}

static bool is_interesting_vision(const vision_event_t *e)
{
    return e->label_idx == VISION_LABEL_CHUTE
        && e->confidence_pct >= APP_VISION_CONF_MIN_PCT;
}

static void emit_alert(void)
{
    uint32_t now = xTaskGetTickCount() * 1000 / configTICK_RATE_HZ;
    if (now - s_last_alert_ms < APP_ALERT_COOLDOWN_SEC * 1000) {
        ESP_LOGD(TAG, "cooldown, skip alert");
        return;
    }
    s_last_alert_ms = now;

    ESP_LOGW(TAG, "ALERT fall+voice! audio=%u/%u%% vision=%u/%u%%",
             s_last_audio.label_idx, s_last_audio.confidence_pct,
             s_last_vision.label_idx, s_last_vision.confidence_pct);

    /* Feedback local : allumer LED alerte */
    gpio_set_level(LEXA_ALERT_GPIO, 1);

#if !MBH_DISABLE_MESH
    /* Propager via mesh en priorité CONTROL (jamais droppée) */
    fall_voice_alert_t alert = {
        .src_node_id        = APP_NODE_ID,
        .audio_label        = (uint8_t)s_last_audio.label_idx,
        .audio_conf_pct     = s_last_audio.confidence_pct,
        .vision_label       = (uint8_t)s_last_vision.label_idx,
        .vision_conf_pct    = s_last_vision.confidence_pct,
        .audio_timestamp_ms = s_last_audio.timestamp_ms,
        .vision_timestamp_ms = s_last_vision.timestamp_ms,
    };
    mesh_msg_t msg = {
        .dst_node_id = MESH_NODE_ROOT,
        .payload     = (const uint8_t *)&alert,
        .len         = sizeof(alert),
        .prio        = MESH_PRIO_CONTROL,
        .flags       = MESH_MSG_FLAG_ACK_REQ,
    };
    esp_err_t err = mesh_send(&msg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mesh_send alert failed: %s", esp_err_to_name(err));
    }
#endif
}

/* Appelée sur chaque event ; teste si une fusion est valide avec le
 * complémentaire le plus récent. */
static void check_fusion(void)
{
    if (s_last_audio.timestamp_ms == 0 || s_last_vision.timestamp_ms == 0) {
        return;  /* pas encore une paire */
    }
    if (!is_interesting_audio(&s_last_audio)) return;
    if (!is_interesting_vision(&s_last_vision)) return;

    uint32_t delta = (s_last_audio.timestamp_ms > s_last_vision.timestamp_ms)
                   ? s_last_audio.timestamp_ms - s_last_vision.timestamp_ms
                   : s_last_vision.timestamp_ms - s_last_audio.timestamp_ms;

    if (delta <= APP_FUSION_WINDOW_MS) {
        emit_alert();
    }
}

/* Callback mesh : reçoit des messages downstream (commands du root) */
static void on_mesh_rx(uint16_t src_node_id, const uint8_t *data, size_t len,
                       mesh_transport_t via)
{
    ESP_LOGI(TAG, "mesh RX from 0x%04X (%zu bytes, via %d)",
             src_node_id, len, via);
    /* TODO : parser les commandes — ex : calibration request, reset alert, etc.
     * Pour l'instant, on log juste. */
}

static void orchestrator_entry(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "started, fusion_window=%d ms, cooldown=%d s",
             APP_FUSION_WINDOW_MS, APP_ALERT_COOLDOWN_SEC);

    for (;;) {
        audio_event_t ae;
        vision_event_t ve;

        /* Vérifier les deux queues sans bloquer longtemps.
         * L'ordre n'importe pas — on garde à jour les "last" des deux. */
        if (xQueueReceive(audio_event_q, &ae, pdMS_TO_TICKS(50)) == pdPASS) {
            s_last_audio = ae;
            check_fusion();
        }
        if (xQueueReceive(vision_event_q, &ve, 0) == pdPASS) {
            s_last_vision = ve;
            check_fusion();
        }

        /* Extinction LED après 2 s */
        uint32_t now = xTaskGetTickCount() * 1000 / configTICK_RATE_HZ;
        if (s_last_alert_ms && now - s_last_alert_ms > 2000) {
            gpio_set_level(LEXA_ALERT_GPIO, 0);
        }
    }
}

void orchestrator_start(void)
{
    /* Config LED alerte */
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LEXA_ALERT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LEXA_ALERT_GPIO, 0);

#if !MBH_DISABLE_MESH
    mesh_recv_register(on_mesh_rx);
#endif

    BaseType_t ok = xTaskCreatePinnedToCore(
        orchestrator_entry, "orch", 4096, NULL, 3, NULL, 1);
    configASSERT(ok == pdPASS);
}
