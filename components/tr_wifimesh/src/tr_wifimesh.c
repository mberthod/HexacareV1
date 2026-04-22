/* tr_wifimesh.c — transport ESP-WIFI-MESH (STUB)
 *
 * Implémentation minimaliste pour permettre la compilation sans la stack 
 * esp_mesh de l'IDF. Cette version est un stub qui logue les actions et
 * retourne ESP_OK ou ESP_ERR_NOT_SUPPORTED.
 */

#include <stdlib.h>
#include <string.h>
#include "tr_wifimesh.h"
#include "esp_log.h"
#include "esp_err.h"

/* Redéfinition des types manquants pour le stub si nécessaire, 
 * bien que tr_wifimesh.h devrait suffire. */

static const char *TAG = "tr_wifimesh_stub";

/* Structure interne pour le stub */
struct tr_wifimesh_ctx_s {
    tr_wifimesh_config_t cfg;
    bool initialized;
    bool active;
};

/* --- API tr_wifimesh --- */

esp_err_t tr_wifimesh_init(const tr_wifimesh_config_t *cfg,
                           tr_wifimesh_ctx_t *out)
{
    if (!cfg || !out) return ESP_ERR_INVALID_ARG;
    
    struct tr_wifimesh_ctx_s *ctx = calloc(1, sizeof(struct tr_wifimesh_ctx_s));
    if (!ctx) return ESP_ERR_NO_MEM;
    
    memcpy(&ctx->cfg, cfg, sizeof(tr_wifimesh_config_t));
    ctx->initialized = true;
    ctx->active = false;
    
    *out = ctx;
    ESP_LOGI(TAG, "Stub: initialized (standby mode)");
    return ESP_OK;
}

esp_err_t tr_wifimesh_activate(tr_wifimesh_ctx_t ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    if (!ctx->initialized) return ESP_ERR_INVALID_STATE;
    
    ctx->active = true;
    ESP_LOGI(TAG, "Stub: activated (no-op)");
    return ESP_OK;
}

esp_err_t tr_wifimesh_deactivate(tr_wifimesh_ctx_t ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    
    ctx->active = false;
    ESP_LOGI(TAG, "Stub: deactivated (no-op)");
    return ESP_OK;
}

esp_err_t tr_wifimesh_send(tr_wifimesh_ctx_t ctx, uint16_t dst,
                           const uint8_t *data, size_t len)
{
    if (!ctx || !data) return ESP_ERR_INVALID_ARG;
    if (!ctx->active) return ESP_ERR_INVALID_STATE;
    
    ESP_LOGW(TAG, "Stub: send to 0x%04X (len=%zu) - NOT SUPPORTED", dst, len);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t tr_wifimesh_deinit(tr_wifimesh_ctx_t ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    
    if (ctx->active) {
        tr_wifimesh_deactivate(ctx);
    }
    
    ESP_LOGI(TAG, "Stub: deinitialized");
    free(ctx);
    return ESP_OK;
}
