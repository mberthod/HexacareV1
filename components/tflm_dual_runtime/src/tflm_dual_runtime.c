/* tflm_dual_runtime.c — STUB. Remplace par MicroInterpreter audio + vision
 * isolés (un par tâche), arenas PSRAM, models/\*.h générés par exporter Python.
 *
 * Le stub renvoie label "_silence" / "debout" avec confiance basse pour
 * exercer la pipeline sans déclencher d'alertes. */
#include "tflm_dual_runtime.h"
#include "esp_log.h"

static const char *TAG = "tflm";

esp_err_t tflm_dual_runtime_init(void)
{
    ESP_LOGW(TAG, "STUB init — arenas not allocated, no model loaded");
    return ESP_OK;
}

esp_err_t tflm_dual_infer_audio(const float *mfcc, size_t n_elements,
                                int32_t *label_out, uint8_t *conf_pct_out)
{
    if (!mfcc || !label_out || !conf_pct_out) return ESP_ERR_INVALID_ARG;
    (void)n_elements;
    *label_out    = 0;     /* _silence */
    *conf_pct_out = 30;    /* sous le seuil APP_AUDIO_CONF_MIN_PCT, jamais émis */
    return ESP_OK;
}

esp_err_t tflm_dual_infer_vision(const float *frame, size_t n_elements,
                                 int32_t *label_out, uint8_t *conf_pct_out)
{
    if (!frame || !label_out || !conf_pct_out) return ESP_ERR_INVALID_ARG;
    (void)n_elements;
    *label_out    = 0;     /* debout */
    *conf_pct_out = 40;    /* sous le seuil */
    return ESP_OK;
}
