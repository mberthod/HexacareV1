/**
 * @file ota_mesh.cpp
 * @brief OTA Random Access : chunkMap bitfield (625*8=5000 chunks), esp_partition_write par offset.
 * @details Pas d'Update.h. OTA_ADV initialise ; OTA_CHUNK écrit à offset chunkIndex*200.
 * Fin : MD5 partition comparé à l'attendu, puis esp_ota_set_boot_partition.
 */

#include "ota_mesh.h"
#include "config/config.h"
#include "lexacare_protocol.h"
#include "rtos/queues_events.h"
#include "system/log_dual.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <MD5Builder.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <freertos/task.h>

static const char *TAG = "OTA_MESH";

#define NVS_NS        "system"
#define NVS_KEY_FW    "fw_ver"
#define CHUNKMAP_BYTES 625
#define CHUNKMAP_BITS  (CHUNKMAP_BYTES * 8)
#define MD5_HEX_LEN    32

/** Bitfield : 1 bit par chunk (0..4999). */
static uint8_t s_chunk_map[CHUNKMAP_BYTES];
/** Partition cible pour l'écriture OTA. */
static const esp_partition_t *s_ota_partition = nullptr;
static uint32_t s_ota_total_size = 0;
static uint16_t s_ota_total_chunks = 0;
static char s_ota_md5_expected[MD5_HEX_LEN + 1];
static bool s_ota_started = false;

static inline int is_chunk_received(uint16_t index) {
    if (index >= CHUNKMAP_BITS) return 1;
    uint16_t byte_idx = index / 8;
    uint8_t bit_idx = index % 8;
    return (s_chunk_map[byte_idx] & (1u << bit_idx)) ? 1 : 0;
}

static inline void set_chunk_received(uint16_t index) {
    if (index >= CHUNKMAP_BITS) return;
    uint16_t byte_idx = index / 8;
    uint8_t bit_idx = index % 8;
    s_chunk_map[byte_idx] |= (1u << bit_idx);
}

static uint32_t nvs_get_fw_version(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return CURRENT_FW_VERSION;
    uint32_t v = CURRENT_FW_VERSION;
    nvs_get_u32(h, NVS_KEY_FW, &v);
    nvs_close(h);
    return v;
}

/** Calcule le MD5 de la partition en hex ASCII (32 car + 0). */
static int partition_md5_hex(const esp_partition_t *part, size_t size, char *hex_out) {
    if (!part || !hex_out || size > part->size) return -1;
    uint8_t buf[512];
    MD5Builder md5;
    md5.begin();
    size_t offset = 0;
    while (offset < size) {
        size_t to_read = (size - offset) > sizeof(buf) ? sizeof(buf) : (size - offset);
        if (esp_partition_read(part, offset, buf, to_read) != ESP_OK)
            return -1;
        md5.add(buf, to_read);
        offset += to_read;
    }
    md5.calculate();
    uint8_t digest[16];
    md5.getBytes(digest);
    for (int i = 0; i < 16; i++)
        sprintf(hex_out + i * 2, "%02x", digest[i]);
    hex_out[MD5_HEX_LEN] = '\0';
    return 0;
}

void ota_mesh_init(void) {
    memset(s_chunk_map, 0, sizeof(s_chunk_map));
    s_ota_partition = nullptr;
    s_ota_total_size = 0;
    s_ota_total_chunks = 0;
    s_ota_md5_expected[0] = '\0';
    s_ota_started = false;
}

void ota_mesh_on_ota_adv(const uint8_t *payload, size_t len) {
    if (len < OTA_ADV_PAYLOAD_SIZE) return;
    const OtaAdvPayload_t *adv = (const OtaAdvPayload_t *)payload;
    uint32_t total_size = adv->totalSize;
    uint16_t total_chunks = adv->totalChunks;
    if (total_chunks == 0 || total_chunks > CHUNKMAP_BITS) {
        log_dual_println("[OTA] Adv rejete: totalChunks invalide");
        return;
    }
    s_ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!s_ota_partition) {
        log_dual_println("[OTA] Adv: pas de partition update");
        return;
    }
    if (total_size > s_ota_partition->size) {
        log_dual_println("[OTA] Adv rejete: taille > partition");
        return;
    }
    memset(s_chunk_map, 0, sizeof(s_chunk_map));
    s_ota_total_size = total_size;
    s_ota_total_chunks = total_chunks;
    memcpy(s_ota_md5_expected, adv->md5Hex, MD5_HEX_LEN);
    s_ota_md5_expected[MD5_HEX_LEN] = '\0';
    s_ota_started = true;
    char buf[80];
    snprintf(buf, sizeof(buf), "[OTA] Adv OK: size=%lu chunks=%u", (unsigned long)total_size, (unsigned)total_chunks);
    log_dual_println(buf);
}

void ota_mesh_on_ota_chunk(const uint8_t *payload, size_t len) {
    if (!s_ota_started || !s_ota_partition || len < OTA_CHUNK_PAYLOAD_SIZE) return;
    const OtaChunkPayload_t *ch = (const OtaChunkPayload_t *)payload;
    uint16_t idx = ch->chunkIndex;
    if (idx >= s_ota_total_chunks) return;
    if (is_chunk_received(idx)) return; /* Déjà écrit, ignoré */

    size_t offset = (size_t)idx * OTA_CHUNK_DATA_SIZE;
    esp_err_t err = esp_partition_write(s_ota_partition, offset, ch->data, OTA_CHUNK_DATA_SIZE);
    if (err != ESP_OK) {
        char buf[48];
        snprintf(buf, sizeof(buf), "[OTA] Write chunk %u ECHEC", (unsigned)idx);
        log_dual_println(buf);
        return;
    }
    set_chunk_received(idx);

    /* Vérifier si tous les chunks sont reçus */
    for (uint16_t i = 0; i < s_ota_total_chunks; i++) {
        if (!is_chunk_received(i)) return;
    }

    /* Tous reçus : délai puis vérifier MD5 (éviter lecture cache stale après écriture, IDFGH-275). */
    log_dual_println("[OTA] Tous les chunks ecrits. Attente 200 ms puis verification MD5...");
    vTaskDelay(pdMS_TO_TICKS(200));  /* laisser écritures flash et cache se stabiliser */
    char computed[MD5_HEX_LEN + 1];
    if (partition_md5_hex(s_ota_partition, s_ota_total_size, computed) != 0) {
        log_dual_println("[OTA] ERREUR: MD5 compute ECHEC (lecture partition)");
        if (g_system_events)
            xEventGroupSetBits(g_system_events, EVENT_OTA_FAIL);
        s_ota_started = false;
        return;
    }
    if (memcmp(computed, s_ota_md5_expected, MD5_HEX_LEN) != 0) {
        char buf[120];
        snprintf(buf, sizeof(buf), "[OTA] ERREUR: MD5 invalide | attendu=%.32s | calcule=%.32s", s_ota_md5_expected, computed);
        log_dual_println(buf);
        log_dual_println("[OTA] -> Donnees flash differentes du fichier envoye (corruption ou mauvais .bin)");
#if OTA_SKIP_MD5_VERIFY
        log_dual_println("[OTA] OTA_SKIP_MD5_VERIFY=1 : boot partition quand meme (test).");
#else
        if (g_system_events)
            xEventGroupSetBits(g_system_events, EVENT_OTA_FAIL);
        s_ota_started = false;
        return;
#endif
    }
    if (!OTA_SKIP_MD5_VERIFY)
        log_dual_println("[OTA] MD5 OK. Definition partition de boot...");
    err = esp_ota_set_boot_partition(s_ota_partition);
    if (err != ESP_OK) {
        char buf[80];
        snprintf(buf, sizeof(buf), "[OTA] ERREUR: set_boot_partition ECHEC esp_err=0x%x (partition invalide?)", (unsigned)err);
        log_dual_println(buf);
        if (g_system_events)
            xEventGroupSetBits(g_system_events, EVENT_OTA_FAIL);
        s_ota_started = false;
        return;
    }
    log_dual_println("[OTA] OK MD5, boot partition fixee. Reboot.");
    if (g_system_events)
        xEventGroupSetBits(g_system_events, EVENT_OTA_READY);
    s_ota_started = false;
}

int ota_mesh_is_ota_in_progress(void) {
    return s_ota_started ? 1 : 0;
}

uint32_t ota_mesh_get_fw_version(void) {
    return nvs_get_fw_version();
}
