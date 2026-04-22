/* mfcc_dsp.c — STUB. Remplace par chaîne ESP-DSP : preemph → win → FFT → mel → log → DCT.
 * Validation bit-équivalence Python/C : voir skill mfcc-validation. */
#include <string.h>
#include "mfcc_dsp.h"
#include "esp_log.h"

#if !(defined(MBH_USB_TELEMETRY_STREAM) && MBH_USB_TELEMETRY_STREAM)
static const char *TAG = "mfcc";
#endif

esp_err_t mfcc_compute(const int16_t *input, size_t n_samples,
                       float *mfcc_out, size_t mfcc_elements)
{
    if (!input || !mfcc_out) return ESP_ERR_INVALID_ARG;
    /* Même une seule ligne sur stdout casse LXCS/LXCL si MBH_USB_TELEMETRY_STREAM. */
#if !(defined(MBH_USB_TELEMETRY_STREAM) && MBH_USB_TELEMETRY_STREAM)
    static int s_warn_once = 0;
    if (!s_warn_once) {
        ESP_LOGW(TAG, "STUB mfcc_compute (zeros). Implement ESP-DSP pipeline.");
        s_warn_once = 1;
    }
#endif
    /* Sortie nulle : le modèle TFLM verra du silence et sortira la classe par défaut */
    memset(mfcc_out, 0, mfcc_elements * sizeof(float));
    (void)n_samples;
    return ESP_OK;
}
