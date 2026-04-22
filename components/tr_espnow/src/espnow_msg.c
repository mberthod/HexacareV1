/* espnow_msg.c — construction et parsing d'enveloppes
 */
#include <string.h>
#include "tr_espnow_priv.h"
#include "esp_log.h"

static const char *TAG = "espnow_msg";

esp_err_t env_build(mesh_envelope_t *hdr, uint8_t type, uint16_t src,
                    uint16_t dst, uint16_t msg_id, uint8_t ttl,
                    uint8_t payload_len)
{
    if (!hdr) return ESP_ERR_INVALID_ARG;
    hdr->magic       = MAGIC_BYTE;
    hdr->type        = type;
    hdr->ttl         = ttl;
    hdr->src_id      = src;
    hdr->dst_id      = dst;
    hdr->msg_id      = msg_id;
    hdr->flags       = 0;
    hdr->payload_len = payload_len;
    return ESP_OK;
}

bool env_parse(const uint8_t *buf, size_t buf_len,
               const mesh_envelope_t **hdr, const uint8_t **payload)
{
    if (!buf || buf_len < sizeof(mesh_envelope_t)) return false;
    const mesh_envelope_t *h = (const mesh_envelope_t *)buf;
    if (h->magic != MAGIC_BYTE) {
        ESP_LOGD(TAG, "bad magic 0x%02X", h->magic);
        return false;
    }
    if ((size_t)(sizeof(*h) + h->payload_len) > buf_len) {
        ESP_LOGD(TAG, "truncated: header says %u bytes payload, buf is %zu",
                 h->payload_len, buf_len);
        return false;
    }
    *hdr = h;
    *payload = (h->payload_len > 0) ? (buf + sizeof(*h)) : NULL;
    return true;
}
