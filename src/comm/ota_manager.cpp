/**
 * @file ota_manager.cpp
 * @brief OTA : NVS fw_ver, Update.h, MD5. Flux ESP-NOW (OTA_ADV/OTA_CHUNK binaires, 200 octets/chunk).
 * @details En mode ESP-NOW Flooding : ota_manager_on_ota_adv() et ota_manager_on_ota_chunk()
 * reçoivent les payloads binaires. Mutex s_ota_mutex protège Update.write et état.
 */

#include "ota_manager.h"
#include "config/config.h"
#include "lexacare_protocol.h"
#include "rtos/queues_events.h"
#include "system/log_dual.h"
#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <MD5Builder.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

#if LEXACARE_MESH_PAINLESS
#include "comm/mesh_handler.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#endif

static const char *TAG = "OTA_MGR";

#define NVS_NS "system"
#define NVS_KEY "fw_ver"
#define MD5_HEX_LEN 32

/** Mutex protégeant l'état OTA (s_ota_started, s_ota_received, s_ota_total, s_ota_version)
 *  et les opérations Update.write / MD5. Toute modification de cet état doit être sous mutex. */
static SemaphoreHandle_t s_ota_mutex = nullptr;
static bool s_ota_started = false;
static size_t s_ota_total = 0;
static size_t s_ota_received = 0;
static uint32_t s_ota_version = 0;
static char s_ota_md5_expected[MD5_HEX_LEN + 1] = {0};
static MD5Builder s_md5;
static bool s_md5_started = false;
static uint32_t s_last_adv_ms = 0;
static char s_adv_md5[MD5_HEX_LEN + 1] = {0};
static size_t s_adv_size = 0;
/** Pour OTA ESP-NOW : index du prochain chunk attendu (0-based). */
static uint16_t s_ota_next_chunk_index = 0;
static uint16_t s_ota_total_chunks = 0;
/** (ROOT) True quand le ROOT diffuse des chunks OTA sur le mesh → bloquer trames Data. */
static bool s_ota_broadcast_active = false;
/** (ROOT) True dès réception octet mode 0x01/0x02 jusqu’à fin OTA série → bloquer trames + sortie [MESH]. */
static bool s_serial_ota_receiving = false;

static uint32_t nvs_get_fw_version(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return CURRENT_FW_VERSION;
    uint32_t v = CURRENT_FW_VERSION;
    nvs_get_u32(h, NVS_KEY, &v);
    nvs_close(h);
    return v;
}

static void nvs_set_fw_version(uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u32(h, NVS_KEY, v);
    nvs_commit(h);
    nvs_close(h);
}

static int compute_running_partition_md5(char *md5_hex_out, size_t *size_out)
{
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (!part || !md5_hex_out || !size_out)
        return -1;
    *size_out = part->size;
    uint8_t buf[512];
    MD5Builder md5;
    md5.begin();
    size_t offset = 0;
    while (offset < part->size)
    {
        size_t to_read = (part->size - offset) > sizeof(buf) ? sizeof(buf) : (part->size - offset);
        if (esp_partition_read(part, offset, buf, to_read) != ESP_OK)
            return -1;
        md5.add(buf, to_read);
        offset += to_read;
    }
    md5.calculate();
    uint8_t digest[16];
    md5.getBytes(digest);
    for (int i = 0; i < 16; i++)
        sprintf(md5_hex_out + i * 2, "%02x", digest[i]);
    md5_hex_out[MD5_HEX_LEN] = '\0';
    return 0;
}

#if LEXACARE_MESH_PAINLESS
static void send_ota_adv_impl(void)
{
    if (s_adv_size == 0 || s_adv_md5[0] == '\0')
    {
        if (compute_running_partition_md5(s_adv_md5, &s_adv_size) != 0)
            return;
        if (s_adv_size > OTA_MAX_SIZE)
            return;
    }
    StaticJsonDocument<128> doc;
    doc["type"] = "OTA_ADV";
    doc["v"] = nvs_get_fw_version();
    doc["md5"] = s_adv_md5;
    doc["s"] = s_adv_size;
    char buf[160];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0)
        mesh_handler_send_broadcast_raw(buf);
}
#endif

void ota_manager_send_adv_now(void)
{
#if LEXACARE_MESH_PAINLESS
    send_ota_adv_impl();
#else
    (void)0;
#endif
}

uint32_t ota_manager_get_fw_version(void)
{
    return nvs_get_fw_version();
}

int ota_manager_is_ota_in_progress(void)
{
    return s_ota_started ? 1 : 0;
}

int ota_manager_is_broadcast_active(void)
{
    return s_ota_broadcast_active ? 1 : 0;
}

void ota_manager_set_broadcast_active(int active)
{
    s_ota_broadcast_active = (active != 0);
}

void ota_manager_set_serial_receiving(int active)
{
    s_serial_ota_receiving = (active != 0);
}

int ota_manager_is_serial_receiving(void)
{
    return s_serial_ota_receiving ? 1 : 0;
}

#if LEXACARE_MESH_PAINLESS
/** Traite OTA_ADV reçu (JSON) : démarre une session OTA. */
static void handle_ota_adv(uint32_t from, JsonObject &obj)
{
    uint32_t adv_v = obj["v"] | 0;
    const char *md5 = obj["md5"] | "";
    size_t size = obj["s"] | 0;
    uint32_t local_v = nvs_get_fw_version();
    if (adv_v <= local_v || size == 0 || size > OTA_MAX_SIZE || strlen(md5) != MD5_HEX_LEN)
        return;
    if (s_ota_mutex && xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return;
    if (s_ota_started)
    {
        if (s_ota_mutex)
            xSemaphoreGive(s_ota_mutex);
        return;
    }
    if (Update.begin(size, U_FLASH))
    {
        s_ota_started = true;
        s_ota_total = size;
        s_ota_received = 0;
        s_ota_version = adv_v;
        strncpy(s_ota_md5_expected, md5, MD5_HEX_LEN);
        s_ota_md5_expected[MD5_HEX_LEN] = '\0';
        s_md5.begin();
        s_md5_started = true;
        StaticJsonDocument<96> req;
        req["type"] = "OTA_REQ";
        req["v"] = adv_v;
        req["o"] = 0;
        char req_buf[96];
        size_t rn = serializeJson(req, req_buf, sizeof(req_buf));
        if (rn > 0)
            mesh_handler_send_to(from, req_buf);
    }
    if (s_ota_mutex)
        xSemaphoreGive(s_ota_mutex);
}

static void send_ota_chunk(uint32_t to, size_t offset)
{
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (!part || offset >= part->size)
        return;
    size_t to_send = OTA_CHUNK_SIZE;
    if (offset + to_send > part->size)
        to_send = part->size - offset;
    uint8_t buf[OTA_CHUNK_SIZE];
    if (esp_partition_read(part, offset, buf, to_send) != ESP_OK)
        return;
    size_t b64_len;
    mbedtls_base64_encode(NULL, 0, &b64_len, buf, to_send);
    char *b64 = (char *)malloc(b64_len + 1);
    if (!b64)
        return;
    if (mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &b64_len, buf, to_send) != 0)
    {
        free(b64);
        return;
    }
    b64[b64_len] = '\0';
    StaticJsonDocument<768> doc;
    doc["type"] = "OTA_CHUNK";
    doc["o"] = offset;
    doc["d"] = b64;
    char out[800];
    size_t n = serializeJson(doc, out, sizeof(out));
    free(b64);
    if (n > 0)
        mesh_handler_send_to(to, out);
}

static void handle_ota_req(uint32_t from, JsonObject &obj)
{
    uint32_t v = obj["v"] | 0;
    size_t offset = obj["o"] | 0;
    uint32_t local_v = nvs_get_fw_version();
    if (v != local_v)
        return;
    send_ota_chunk(from, offset);
}

/** Reçoit un chunk OTA, écrit en flash via Update.write, met à jour MD5. Section critique sous s_ota_mutex (Update.write + s_ota_received). */
static void handle_ota_chunk(uint32_t from, JsonObject &obj)
{
    (void)from;
    if (!s_ota_started)
        return;
    size_t offset = obj["o"] | 0;
    const char *b64 = obj["d"] | "";
    if (!b64[0])
        return;
    size_t b64_len = strlen(b64);
    size_t max_out = OTA_CHUNK_SIZE + 16;
    uint8_t *dec = (uint8_t *)malloc(max_out); /* Heap, pas stack, pour limiter la stack de loop() */
    if (!dec)
        return;
    size_t out_len;
    if (mbedtls_base64_decode(dec, max_out, &out_len, (const unsigned char *)b64, b64_len) != 0)
    {
        free(dec);
        return;
    }
    if (s_ota_mutex && xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        free(dec);
        return;
    }
    if (!s_ota_started || offset != s_ota_received || s_ota_received + out_len > s_ota_total)
    {
        if (s_ota_mutex)
            xSemaphoreGive(s_ota_mutex);
        free(dec);
        return;
    }
    if (Update.write(dec, out_len) != out_len)
    {
        if (s_ota_mutex)
            xSemaphoreGive(s_ota_mutex);
        free(dec);
        return;
    }
    s_md5.add(dec, out_len);
    s_ota_received += out_len;
    if (s_ota_mutex)
        xSemaphoreGive(s_ota_mutex);
    free(dec);
    if (s_ota_received >= s_ota_total)
    {
        s_md5.calculate();
        uint8_t digest[16];
        s_md5.getBytes(digest);
        s_md5_started = false;
        char computed[MD5_HEX_LEN + 1];
        for (int i = 0; i < 16; i++)
            sprintf(computed + i * 2, "%02x", digest[i]);
        computed[MD5_HEX_LEN] = '\0';
        if (strcasecmp(computed, s_ota_md5_expected) == 0 && Update.end(true))
        {
            nvs_set_fw_version(s_ota_version);
            if (g_system_events)
                xEventGroupSetBits(g_system_events, EVENT_OTA_READY);
            ESP_LOGI(TAG, "OTA OK, MD5 verifie. Reboot imminent.");
        }
        else
        {
            Update.abort();
            ESP_LOGE(TAG, "OTA MD5 mismatch ou Update.end echoue");
        }
        s_ota_started = false;
    }
    else
    {
        StaticJsonDocument<96> req;
        req["type"] = "OTA_REQ";
        req["v"] = s_ota_version;
        req["o"] = s_ota_received;
        char req_buf[96];
        size_t rn = serializeJson(req, req_buf, sizeof(req_buf));
        if (rn > 0)
            mesh_handler_send_to(from, req_buf);
    }
}
#endif /* LEXACARE_MESH_PAINLESS */

int ota_manager_init(void)
{
    if (s_ota_mutex == nullptr)
        s_ota_mutex = xSemaphoreCreateMutex();
    s_ota_started = false;
    s_ota_received = 0;
    s_adv_size = 0;
    s_adv_md5[0] = '\0';
    ESP_LOGI(TAG, "Init OK fw_ver=%lu", (unsigned long)nvs_get_fw_version());
    return 1;
}

void ota_manager_loop(void)
{
#if LEXACARE_MESH_PAINLESS
    if (!mesh_handler_is_root())
        return;
    uint32_t now = millis();
    if (now - s_last_adv_ms >= OTA_ADV_INTERVAL_MS)
    {
        s_last_adv_ms = now;
        send_ota_adv_impl();
    }
#else
    (void)0;
#endif
}

void ota_manager_on_message(uint32_t from, const char *msg, size_t len)
{
#if LEXACARE_MESH_PAINLESS
    if (len == 0 || msg[0] != '{')
        return;
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, msg, len) != DeserializationError::Ok)
        return;
    JsonObject obj = doc.as<JsonObject>();
    const char *type = doc["type"] | "";
    if (strcmp(type, "OTA_ADV") == 0)
        handle_ota_adv(from, obj);
    else if (strcmp(type, "OTA_REQ") == 0)
        handle_ota_req(from, obj);
    else if (strcmp(type, "OTA_CHUNK") == 0)
        handle_ota_chunk(from, obj);
#else
    (void)from;
    (void)msg;
    (void)len;
#endif
}

/* ---------- Flux ESP-NOW binaire (OTA_ADV + OTA_CHUNK 200 octets) ---------- */

/// @brief
/// @param payload
/// @param len
void ota_manager_on_ota_adv(const uint8_t *payload, size_t len)
{
    if (!payload || len < OTA_ADV_PAYLOAD_SIZE)
    {
        log_dual_println("[OTA] Annonce rejetée: payload invalide ou taille < 38 octets.");
        ESP_LOGW(TAG, "OTA_ADV rejete: payload NULL ou len=%u < %u", (unsigned)len, (unsigned)OTA_ADV_PAYLOAD_SIZE);
        return;
    }
    const OtaAdvPayload_t *adv = (const OtaAdvPayload_t *)payload;
    uint32_t totalSize = adv->totalSize;
    uint16_t totalChunks = adv->totalChunks;
    if (totalSize == 0 || totalSize > OTA_MAX_SIZE || totalChunks == 0)
    {
        log_dual_println("[OTA] Annonce rejetée: taille ou nombre de chunks invalide.");
        ESP_LOGW(TAG, "OTA_ADV rejete: size=%lu chunks=%u (max size %lu)", (unsigned long)totalSize, (unsigned)totalChunks, (unsigned long)OTA_MAX_SIZE);
        return;
    }

    if (s_ota_mutex && xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        log_dual_println("[OTA] Annonce: mutex occupé, ignoré.");
        ESP_LOGW(TAG, "OTA_ADV: mutex non pris");
        return;
    }
    if (s_ota_started)
    {
        if (s_ota_mutex)
            xSemaphoreGive(s_ota_mutex);
        ESP_LOGD(TAG, "OTA_ADV ignore (session deja en cours)");
        return;
    }
    if (Update.begin(totalSize, U_FLASH))
    {
        s_ota_started = true;
        s_ota_total = totalSize;
        s_ota_received = 0;
        s_ota_next_chunk_index = 0;
        s_ota_total_chunks = totalChunks;
        memcpy(s_ota_md5_expected, adv->md5Hex, MD5_HEX_LEN);
        s_ota_md5_expected[MD5_HEX_LEN] = '\0';
        s_md5.begin();
        s_md5_started = true;
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "[OTA] Annonce mesh reçue: size=%lu octets, %u chunks. Réception chunks...", (unsigned long)totalSize, (unsigned)totalChunks);
            log_dual_println(msg);
        }
        ESP_LOGI(TAG, "OTA_ADV: session demarree size=%lu chunks=%u", (unsigned long)totalSize, (unsigned)totalChunks);
    }
    else
    {
        log_dual_println("[OTA] Update.begin échoué (flash).");
        ESP_LOGE(TAG, "OTA_ADV: Update.begin(%lu) echoue", (unsigned long)totalSize);
    }
    if (s_ota_mutex)
        xSemaphoreGive(s_ota_mutex);
}

void ota_manager_on_ota_chunk(const uint8_t *payload, size_t len)
{
    if (!payload || len < OTA_CHUNK_PAYLOAD_SIZE)
    {
        ESP_LOGW(TAG, "OTA_CHUNK rejete: len=%u", (unsigned)len);
        return;
    }
    const OtaChunkPayload_t *chunk = (const OtaChunkPayload_t *)payload;
    uint16_t chunkIndex = chunk->chunkIndex;
    uint16_t totalChunks = chunk->totalChunks;
    if (totalChunks == 0)
    {
        ESP_LOGW(TAG, "OTA_CHUNK rejete: totalChunks=0");
        return;
    }

    if (s_ota_mutex && xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "OTA_CHUNK: mutex non pris");
        return;
    }
    if (!s_ota_started || s_ota_total_chunks != totalChunks || chunkIndex != s_ota_next_chunk_index)
    {
        if (s_ota_mutex)
            xSemaphoreGive(s_ota_mutex);
        ESP_LOGD(TAG, "OTA_CHUNK ignore: started=%d expect idx=%u got %u (total %u)",
                 s_ota_started ? 1 : 0, (unsigned)s_ota_next_chunk_index, (unsigned)chunkIndex, (unsigned)totalChunks);
        return;
    }
    /* Log progression tous les 100 chunks ou au premier / dernier */
    if (chunkIndex % 100 == 0 || chunkIndex == totalChunks - 1)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "[OTA] Chunk mesh reçu: %u/%u", (unsigned)(chunkIndex + 1), (unsigned)totalChunks);
        log_dual_println(msg);
    }
    size_t to_write = OTA_CHUNK_DATA_SIZE;
    if (s_ota_received + to_write > s_ota_total)
        to_write = s_ota_total - s_ota_received;
    uint8_t buf[OTA_CHUNK_DATA_SIZE];
    memcpy(buf, chunk->data, to_write);
    if (Update.write(buf, to_write) != to_write)
    {
        if (s_ota_mutex)
            xSemaphoreGive(s_ota_mutex);
        Update.abort();
        s_ota_started = false;
        return;
    }
    s_md5.add(buf, (uint16_t)to_write);
    s_ota_received += to_write;
    s_ota_next_chunk_index++;
    bool is_last = (chunkIndex == totalChunks - 1) || (s_ota_received >= s_ota_total);
    if (s_ota_mutex)
        xSemaphoreGive(s_ota_mutex);

    if (is_last)
    {
        s_md5.calculate();
        uint8_t digest[16];
        s_md5.getBytes(digest);
        s_md5_started = false;
        char computed[MD5_HEX_LEN + 1];
        for (int i = 0; i < 16; i++)
            sprintf(computed + i * 2, "%02x", digest[i]);
        computed[MD5_HEX_LEN] = '\0';
        if (strncasecmp(computed, s_ota_md5_expected, MD5_HEX_LEN) == 0 && Update.end(true))
        {
            nvs_set_fw_version(CURRENT_FW_VERSION);
            if (g_system_events)
                xEventGroupSetBits(g_system_events, EVENT_OTA_READY);
            log_dual_println("[OTA] Réception terminée. MD5 OK. 4× vert puis reboot.");
            ESP_LOGI(TAG, "OTA OK, MD5 verifie. 4 clignotements vert puis reboot.");
        }
        else
        {
            Update.abort();
            if (g_system_events)
                xEventGroupSetBits(g_system_events, EVENT_OTA_FAIL);
            log_dual_println("[OTA] Échec (MD5 ou Update.end). 4× rouge puis reboot ancien code.");
            ESP_LOGE(TAG, "OTA echec (MD5 ou Update.end). 4 clignotements rouge puis reboot ancien code.");
        }
        s_ota_started = false;
    }
}
