/**
 * @file official_ota_manager.cpp
 * @brief Implémentation du gestionnaire OTA officiel (espnow_ota).
 */

#include "OTA/official_ota_manager.h"
#include "config/config.h"
#include "system/log_dual.h"
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_err.h>
#include <mbedtls/sha256.h>
#include <cstring>

#ifdef LEXACARE_USE_ESPNOW_OTA
#include "espnow_ota.h"
#include "espnow.h"
#define HAVE_ESPNOW_OTA 1
#else
#define HAVE_ESPNOW_OTA 0
#endif

static const char *TAG = "OFFICIAL_OTA";

#if HAVE_ESPNOW_OTA

static const esp_partition_t *s_ota_partition = nullptr;

static esp_err_t ota_data_cb(size_t src_offset, void *dst, size_t size)
{
    if (s_ota_partition == nullptr || dst == nullptr)
        return ESP_FAIL;
    return esp_partition_read(s_ota_partition, src_offset, dst, size) == ESP_OK ? ESP_OK : ESP_FAIL;
}

#endif

int official_ota_init(void)
{
#if HAVE_ESPNOW_OTA
    esp_err_t ret = espnow_storage_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "espnow_storage_init failed: %s", esp_err_to_name(ret));
        return 0;
    }
    ESP_LOGI(TAG, "espnow_storage_init OK");
    return 1;
#else
    return 1; /* pas d'échec si composant absent */
#endif
}

int official_ota_responder_start(void)
{
#if HAVE_ESPNOW_OTA
    espnow_ota_config_t config = {};
    config.skip_version_check = true;
    config.progress_report_interval = 10;
    esp_err_t ret = espnow_ota_responder_start(&config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "espnow_ota_responder_start failed: %s", esp_err_to_name(ret));
        return 0;
    }
    ESP_LOGI(TAG, "OTA responder started");
    return 1;
#else
    (void)0;
    ESP_LOGW(TAG, "espnow_ota not available (no component)");
    return 0;
#endif
}

static int compute_sha256_of_partition(const esp_partition_t *part, size_t size, uint8_t sha256[32])
{
    if (part == nullptr || sha256 == nullptr)
        return -1;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    const size_t chunk = 1024;
    uint8_t buf[chunk];
    size_t offset = 0;
    while (offset < size)
    {
        size_t to_read = (size - offset) > chunk ? chunk : (size - offset);
        if (esp_partition_read(part, offset, buf, to_read) != ESP_OK)
        {
            mbedtls_sha256_free(&ctx);
            return -1;
        }
        mbedtls_sha256_update(&ctx, buf, to_read);
        offset += to_read;
    }
    mbedtls_sha256_finish(&ctx, sha256);
    mbedtls_sha256_free(&ctx);
    return 0;
}

int start_ota_initiator_distribution(size_t firmware_size, const char *sha256_hex)
{
#if HAVE_ESPNOW_OTA
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(nullptr);
    if (update_part == nullptr)
    {
        ESP_LOGE(TAG, "No OTA update partition");
        return 0;
    }
    s_ota_partition = update_part;

    uint8_t sha256_bin[32];
    if (sha256_hex != nullptr)
    {
        size_t len = strlen(sha256_hex);
        if (len != 64)
        {
            ESP_LOGE(TAG, "sha256_hex must be 64 chars");
            return 0;
        }
        for (int i = 0; i < 32; i++)
        {
            char a = sha256_hex[i * 2], b = sha256_hex[i * 2 + 1];
            int ha = (a >= '0' && a <= '9') ? (a - '0') : (a >= 'a' && a <= 'f') ? (a - 'a' + 10) : (a >= 'A' && a <= 'F') ? (a - 'A' + 10) : -1;
            int hb = (b >= '0' && b <= '9') ? (b - '0') : (b >= 'a' && b <= 'f') ? (b - 'a' + 10) : (b >= 'A' && b <= 'F') ? (b - 'A' + 10) : -1;
            if (ha < 0 || hb < 0)
            {
                ESP_LOGE(TAG, "Invalid hex in sha256_hex");
                return 0;
            }
            sha256_bin[i] = (uint8_t)((ha << 4) | hb);
        }
    }
    else
    {
        if (compute_sha256_of_partition(update_part, firmware_size, sha256_bin) != 0)
        {
            ESP_LOGE(TAG, "SHA-256 computation failed");
            return 0;
        }
    }

    espnow_ota_responder_t *info_list = nullptr;
    size_t num = 0;
    esp_err_t ret = espnow_ota_initiator_scan(&info_list, &num, pdMS_TO_TICKS(10000));
    if (ret != ESP_OK || num == 0)
    {
        ESP_LOGW(TAG, "No responders found (scan ret=%s, num=%u)", esp_err_to_name(ret), (unsigned)num);
        espnow_ota_initiator_scan_result_free();
        return 0;
    }

    espnow_addr_t *addrs = (espnow_addr_t *)malloc(num * sizeof(espnow_addr_t));
    if (addrs == nullptr)
    {
        espnow_ota_initiator_scan_result_free();
        return 0;
    }
    for (size_t i = 0; i < num; i++)
        memcpy(addrs[i].mac, info_list[i].mac, 6);

    espnow_ota_result_t *result = nullptr;
    ret = espnow_ota_initiator_send(addrs, num, sha256_bin, firmware_size, ota_data_cb, &result);
    free(addrs);
    espnow_ota_initiator_scan_result_free();

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "espnow_ota_initiator_send failed: %s", esp_err_to_name(ret));
        return 0;
    }

    int ok = (result != nullptr && result->successed_num > 0) ? 1 : 0;
    if (result != nullptr)
    {
        ESP_LOGI(TAG, "OTA distribution: successed=%u unfinished=%u requested=%u",
                 (unsigned)result->successed_num, (unsigned)result->unfinished_num, (unsigned)result->requested_num);
        espnow_ota_initiator_result_free(result);
    }
    s_ota_partition = nullptr;
    return ok;
#else
    (void)firmware_size;
    (void)sha256_hex;
    ESP_LOGW(TAG, "espnow_ota not available");
    return 0;
#endif
}
