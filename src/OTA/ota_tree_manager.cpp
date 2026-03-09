/**
 * @file ota_tree_manager.cpp
 * @brief Le Distributeur de Mises à Jour (OTA - Over The Air).
 * 
 * @details
 * Ce module est responsable de mettre à jour le logiciel de TOUS les boîtiers du réseau, sans fil.
 * C'est une opération délicate, comparable à mettre à jour 1000 smartphones en même temps sans Internet.
 * 
 * La stratégie utilisée est le "Store & Forward" (Stocker puis Transmettre) :
 * 
 * 1. **Réception (L'Éponge)** :
 *    - Le boîtier reçoit la mise à jour morceau par morceau (comme un puzzle).
 *    - Il stocke chaque pièce dans sa mémoire (Flash).
 *    - Il ne fait RIEN d'autre tant qu'il n'a pas le puzzle complet.
 * 
 * 2. **Vérification (Le Contrôleur)** :
 *    - Une fois complet, il vérifie que l'image est parfaite (Check MD5).
 * 
 * 3. **Distribution (Le Professeur)** :
 *    - AVANT de s'installer la mise à jour (et de redémarrer), il la transmet à ses "Enfants".
 *    - Il attend que ses enfants aient fini avant de penser à lui-même.
 * 
 * 4. **Installation (Le Reboot)** :
 *    - Une fois que tout le monde en dessous est servi, il s'installe la mise à jour et redémarre.
 */

#include "OTA/ota_tree_manager.h"
#include "config/config.h"
#include "mesh/routing_manager.h"
#include "system/led_manager.h"
#include "system/log_dual.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#if OTA_MESH_VERIFY_MD5
#include <MD5Builder.h>
#include <cstdlib>
#endif

static const char *TAG = "OTA_TREE";

enum OtaState
{
    OTA_IDLE,
    OTA_RECEIVING_UART,       // ROOT: Reçoit du PC (0x01 ou ancien 0x02)
    OTA_RECEIVING_MESH,       // CHILD: Reçoit du Parent (ancien PULL)
    OTA_DISTRIBUTING,         // SERVER: Distribue (ancien flux)
    OTA_FINISHED,             // Prêt à rebooter
    // OTA Mesh en vague (nouveau flux 0x02)
    OTA_MESH_ROOT_RECV,       // ROOT: reçoit chunks série, envoie aux enfants, attend CHUNK_ACK
    OTA_MESH_ROOT_REBOOT_WAIT, // ROOT: a envoyé REBOOT, attend REBOOT_ACK de tous
    OTA_MESH_CHILD_ACTIVE,    // Enfant: a reçu ENTER, reçoit CHUNKs, forward + ACK
    OTA_MESH_CHILD_REBOOT_WAIT // Enfant: a envoyé REBOOT aux siens, attend REBOOT_ACK puis reboot
};

static OtaState s_state = OTA_IDLE;
static const esp_partition_t *s_update_partition = nullptr;
static uint32_t s_total_size = 0;
static uint32_t s_written_size = 0;
static char s_expected_md5[33] = {0};

/** 0x01 = OTA Série (ROOT seul, pas de diffusion), 0x02 = OTA Mesh (ROOT puis diffusion). */
static uint8_t s_uart_ota_mode = 0;

// Côté enfant (DOWNLOADING / PULL) : chunk attendu et handle OTA
static uint16_t s_current_expected_chunk = 0;
static uint16_t s_total_chunks = 0;
static esp_ota_handle_t s_update_handle = 0;
static TickType_t s_last_chunk_tick = 0;
#define OTA_REQ_TIMEOUT_MS 500

// Pour la distribution (PROPAGATING)
static std::vector<ChildInfo> s_targets;
static size_t s_current_target_idx = 0;
static bool s_current_child_done = false;
static uint32_t s_last_req_time = 0;

// OTA Mesh en vague (ROOT) : chunk en cours, buffer, compteur ACKs
static uint8_t s_mesh_root_chunk_buf[OTA_CHUNK_DATA_SIZE];
static uint16_t s_mesh_root_waiting_chunk = 0xFFFF;  // chunk en attente d'ACKs
static size_t s_mesh_root_acks_count = 0;            // nombre d'ACKs reçus pour ce chunk
static uint16_t s_mesh_root_chunk_index = 0;         // index du chunk courant (pour écriture flash)
static TickType_t s_mesh_root_enter_ticks = 0;       // début timeout 15 min

// OTA Mesh en vague (enfant) : attente ACKs des enfants pour chunk courant
static size_t s_mesh_child_pending_acks = 0;
static uint16_t s_mesh_child_current_chunk = 0xFFFF;
static bool s_mesh_child_ack_received[MAX_CHILDREN_PER_NODE];
static uint8_t s_mesh_child_chunk_buf[OTA_CHUNK_DATA_SIZE];
static uint8_t s_mesh_child_chunk_size = 0;  // octets valides dans s_mesh_child_chunk_buf
static bool s_mesh_root_ack_received[MAX_CHILDREN_PER_NODE];  // ROOT : enfant i a envoyé CHUNK_ACK pour chunk courant
static size_t s_mesh_reboot_acks_count = 0;
static void (*s_mesh_done_cb)(void) = nullptr;
static TaskHandle_t s_mesh_suspend_handles[4] = {nullptr};
/** True quand le ROOT peut accepter le prochain chunk série (chunk courant propagé + écrit en flash). */
static volatile bool s_mesh_root_ready_for_next_chunk = true;

#if OTA_MESH_VERIFY_MD5
/** Vérifie que le MD5 de la partition correspond à s_expected_md5 (32 hex). Retourne true si OK. */
static bool verify_partition_md5(void) {
    if (!s_update_partition || s_total_size == 0) return false;
    MD5Builder md5;
    md5.begin();
    const size_t chunk_sz = 4096;
    uint8_t *buf = (uint8_t *)malloc(chunk_sz);
    if (!buf) return false;
    uint32_t off = 0;
    while (off < s_total_size) {
        size_t to_read = (s_total_size - off) > chunk_sz ? chunk_sz : (size_t)(s_total_size - off);
        if (esp_partition_read(s_update_partition, off, buf, to_read) != ESP_OK) {
            free(buf);
            return false;
        }
        md5.add(buf, to_read);
        off += to_read;
    }
    free(buf);
    md5.calculate();
    String computed_hex = md5.toString();
    /* Comparaison insensible à la casse avec s_expected_md5 (32 caractères). */
    bool ok = (computed_hex.length() >= 32 && strncasecmp(computed_hex.c_str(), s_expected_md5, 32) == 0);
    if (!ok)
        ESP_LOGE(TAG, "OTA mesh: MD5 partition invalide (image corrompue ou alteree).");
    return ok;
}
#endif

/** Envoie MSG_OTA_REQ(requested_chunk_index) au parent. */
static void send_ota_req(uint16_t chunk_index) {
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac)) return;
    OtaReqPayload req;
    req.requested_chunk_index = chunk_index;
    routing_send_unicast(parent_mac, MSG_OTA_REQ, (uint8_t *)&req, sizeof(req));
}

/**
 * @brief Initialisation du Gestionnaire de Mise à Jour.
 *
 * @details
 * Prépare le terrain pour recevoir un nouveau logiciel.
 * Il cherche dans la mémoire du boîtier (Flash) un espace libre ("Partition OTA")
 * où il pourra stocker le futur logiciel temporairement.
 */
void ota_tree_init(void) {
    s_state = OTA_IDLE;
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition)
    {
        ESP_LOGE(TAG, "Pas de partition OTA trouvée !");
    }
}

void ota_tree_set_uart_mode(uint8_t mode) {
    s_uart_ota_mode = (mode == 0x01 || mode == 0x02) ? mode : 0;
}

void ota_tree_start_propagation(uint32_t total_size, uint16_t total_chunks, const char *md5) {
    (void)total_size;
    (void)total_chunks;
    (void)md5;
    if (s_state != OTA_RECEIVING_UART) return;
    s_state = OTA_DISTRIBUTING;
    s_targets = routing_get_children();
    s_current_target_idx = 0;
    s_current_child_done = false;
    ESP_LOGI(TAG, "Propagation (legacy) demarree.");
}

void ota_tree_register_mesh_done_cb(void (*cb)(void)) {
    s_mesh_done_cb = cb;
}

void ota_tree_register_tasks_for_mesh_suspend(TaskHandle_t h1, TaskHandle_t h2, TaskHandle_t h3, TaskHandle_t h4) {
    s_mesh_suspend_handles[0] = h1;
    s_mesh_suspend_handles[1] = h2;
    s_mesh_suspend_handles[2] = h3;
    s_mesh_suspend_handles[3] = h4;
}

/**
 * @brief Réception d'un morceau de mise à jour depuis le PC (ROOT seulement).
 *
 * @details
 * Cette fonction est appelée quand le PC envoie un petit bout du fichier de mise à jour (Chunk) via le câble USB.
 * Le ROOT prend ce bout et l'écrit directement dans sa mémoire Flash.
 *
 * - Si c'est le premier bout : Il passe en mode "RÉCEPTION UART".
 * - À chaque bout : Il écrit et fait clignoter la LED.
 * - Quand tout est reçu : Il passe en mode "DISTRIBUTION" pour partager avec les autres.
 *
 * @param offset Où écrire ce bout dans le fichier total (position).
 * @param data Les données du bout.
 * @param len La taille du bout.
 * @param totalSize La taille totale du fichier final.
 * @param md5 La signature de sécurité du fichier.
 */
void ota_tree_on_uart_chunk(uint32_t offset, const uint8_t *data, uint16_t len, uint32_t totalSize, const char *md5) {
    /* ----- Mode 0x02 OTA Mesh en vague : ROOT envoie chunk aux enfants, écrit flash après ACKs ----- */
    if (s_uart_ota_mode == 0x02) {
        if (s_state == OTA_IDLE) {
            if (!s_update_partition) s_update_partition = esp_ota_get_next_update_partition(NULL);
            if (!s_update_partition) {
                ESP_LOGE(TAG, "[OTA_MESH] Pas de partition OTA.");
                led_manager_set_state(LED_STATE_ERROR);
                return;
            }
            s_total_size = totalSize;
            s_total_chunks = (uint16_t)((totalSize + OTA_CHUNK_DATA_SIZE - 1) / OTA_CHUNK_DATA_SIZE);
            strncpy(s_expected_md5, md5, 32);
            s_expected_md5[32] = '\0';
            esp_err_t err = esp_ota_begin(s_update_partition, s_total_size, &s_update_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "[OTA_MESH] esp_ota_begin ECHEC");
                led_manager_set_state(LED_STATE_ERROR);
                return;
            }
            s_targets = routing_get_children();
            s_mesh_root_enter_ticks = xTaskGetTickCount();
            OtaAdvPayload adv;
            adv.totalSize = s_total_size;
            adv.totalChunks = s_total_chunks;
            memcpy(adv.md5Hex, s_expected_md5, 32);
            for (size_t i = 0; i < s_targets.size(); i++)
                routing_send_unicast(s_targets[i].mac, MSG_OTA_MESH_ENTER, (uint8_t *)&adv, sizeof(adv));
            s_state = OTA_MESH_ROOT_RECV;
            s_mesh_root_ready_for_next_chunk = true;
            ESP_LOGI(TAG, "[OTA_MESH] ENTER envoye a %u enfant(s), timeout 15 min.", (unsigned)s_targets.size());
        }
        if (s_state != OTA_MESH_ROOT_RECV) return;
        uint16_t idx = (uint16_t)(offset / OTA_CHUNK_DATA_SIZE);
        /* Bloquer la lecture série du chunk suivant jusqu'à réception de tous les CHUNK_ACK. */
        if (s_targets.size() > 0)
            s_mesh_root_ready_for_next_chunk = false;
        memcpy(s_mesh_root_chunk_buf, data, len);
        s_mesh_root_chunk_index = idx;
        s_mesh_root_waiting_chunk = idx;
        s_mesh_root_acks_count = 0;
        memset(s_mesh_root_ack_received, 0, sizeof(s_mesh_root_ack_received));
        OtaChunkPayload payload;
        payload.chunk_index = idx;
        payload.chunk_size = (uint8_t)len;
        memcpy(payload.data, data, len);
        if (len < OTA_CHUNK_DATA_SIZE) memset(payload.data + len, 0, OTA_CHUNK_DATA_SIZE - len);
        size_t plen = (size_t)(sizeof(uint16_t) + sizeof(uint8_t) + len);
        for (size_t i = 0; i < s_targets.size(); i++)
            routing_send_unicast(s_targets[i].mac, MSG_OTA_CHUNK, (uint8_t *)&payload, (uint16_t)plen);
        led_flash_rx();
        if (s_targets.size() == 0) {
            uint16_t write_len = (idx + 1 == s_total_chunks) ? (uint16_t)(s_total_size - (uint32_t)idx * OTA_CHUNK_DATA_SIZE) : OTA_CHUNK_DATA_SIZE;
            if (write_len > OTA_CHUNK_DATA_SIZE) write_len = OTA_CHUNK_DATA_SIZE;
            esp_ota_write(s_update_handle, s_mesh_root_chunk_buf, write_len);
            char msg[64];
            snprintf(msg, sizeof(msg), "[OTA_MESH] CHUNK_PROPAGATED %u\r\n", (unsigned)idx);
            log_dual_printf(msg);
            s_mesh_root_ready_for_next_chunk = true;
            if (idx + 1 >= s_total_chunks) {
                esp_ota_end(s_update_handle);
                s_state = OTA_IDLE;
                s_update_partition = nullptr;
                led_manager_set_state(LED_STATE_ROOT);
                if (s_mesh_done_cb) s_mesh_done_cb();
            }
        }
        return;
    }

    /* ----- Mode 0x01 (OTA Série ROOT seul) ----- */
    if (s_state == OTA_IDLE)
    {
        ESP_LOGI(TAG, "[OTA_UART] Premier chunk: totalSize=%lu, partition=%p", (unsigned long)totalSize, (void *)s_update_partition);
        if (!s_update_partition) {
            ESP_LOGE(TAG, "[OTA_UART] ERREUR: s_update_partition NULL (ota_tree_init a echoue?)");
            led_manager_set_state(LED_STATE_ERROR);
            return;
        }
        s_total_size = totalSize;
        strncpy(s_expected_md5, md5, 32);
        s_expected_md5[32] = '\0';
        s_written_size = 0;
        esp_err_t err = esp_ota_begin(s_update_partition, s_total_size, &s_update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[OTA_UART] esp_ota_begin ECHEC err=0x%x (%d)", (unsigned)err, (int)err);
            led_manager_set_state(LED_STATE_ERROR);
            return;
        }
        ESP_LOGI(TAG, "[OTA_UART] esp_ota_begin OK, handle=%p", (void *)s_update_handle);
        s_state = OTA_RECEIVING_UART;
        led_manager_set_state(LED_STATE_OTA);
    }

    if (s_state != OTA_RECEIVING_UART)
        return;

    esp_err_t err = esp_ota_write(s_update_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[OTA_UART] esp_ota_write ECHEC offset=%lu len=%u err=0x%x", (unsigned long)offset, (unsigned)len, (unsigned)err);
        led_manager_set_state(LED_STATE_ERROR);
        return;
    }
    s_written_size += len;
    led_flash_rx();

    if (s_written_size >= s_total_size)
    {
        s_total_chunks = (uint16_t)((s_total_size + OTA_CHUNK_DATA_SIZE - 1) / OTA_CHUNK_DATA_SIZE);
        ESP_LOGI(TAG, "[OTA_UART] Dernier chunk recu. written=%lu total=%lu", (unsigned long)s_written_size, (unsigned long)s_total_size);

        if (s_uart_ota_mode == 0x01) {
            ESP_LOGI(TAG, "[OTA_UART] Mode 0x01: fin OTA + validation + reboot");
            esp_err_t err_end = esp_ota_end(s_update_handle);
            if (err_end != ESP_OK) {
                ESP_LOGE(TAG, "[OTA_UART] esp_ota_end ECHEC err=0x%x (%d)", (unsigned)err_end, (int)err_end);
                led_manager_set_state(LED_STATE_ERROR);
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
                return;
            }
            ESP_LOGI(TAG, "[OTA_UART] esp_ota_end OK");

            ESP_LOGI(TAG, "[OTA_UART] Partition addr=0x%lx size=%lu",
                     (unsigned long)s_update_partition->address,
                     (unsigned long)s_update_partition->size);

            if (!s_update_partition) {
                ESP_LOGE(TAG, "[OTA_UART] ERREUR: partition NULL avant set_boot");
                led_manager_set_state(LED_STATE_ERROR);
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
                return;
            }
            esp_err_t err_boot = esp_ota_set_boot_partition(s_update_partition);
            ESP_LOGI(TAG, "[OTA_UART] esp_ota_set_boot_partition => err=0x%x (%d)", (unsigned)err_boot, (int)err_boot);

            if (err_boot == ESP_OK) {
                ESP_LOGI(TAG, "[OTA_UART] Succes: 4x LED vert puis reboot");
                for (int i = 0; i < 4; i++) {
                    led_manager_set_state(LED_STATE_CONNECTED);
                    vTaskDelay(pdMS_TO_TICKS(150));
                    led_manager_set_state(LED_STATE_OTA_SERIAL);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                ESP_LOGI(TAG, "[OTA_UART] Redémarrage dans 1 s...");
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                ESP_LOGE(TAG, "[OTA_UART] ECHEC set_boot_partition: 0x%x (voir esp_err_t)", (unsigned)err_boot);
                ESP_LOGE(TAG, "[OTA_UART] LED rouge 3s puis reboot sur ancienne app");
                led_manager_set_state(LED_STATE_ERROR);
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            esp_restart();
        }
        /* 0x02 géré en début de fonction (flux en vague). */
        return;
    }
}

/**
 * @brief Réception d'une annonce de mise à jour (Publicité) - démarre le PULL.
 *
 * @details
 * Si IDLE : obtient la partition OTA, appelle esp_ota_begin, stocke total_chunks,
 * passe en DOWNLOADING et envoie la première requête MSG_OTA_REQ(0).
 */
/** Enfant reçoit OTA_MESH_ENTER : suspend tâches, LED rouge→bleu, forward aux enfants, esp_ota_begin. */
static void handle_ota_mesh_enter(const uint8_t *src_mac, const uint8_t *payload, uint16_t len) {
    if (!payload || len < (uint16_t)sizeof(OtaAdvPayload)) return;
    if (s_state != OTA_IDLE) return;
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac) || memcmp(src_mac, parent_mac, 6) != 0) return;
    OtaAdvPayload *adv = (OtaAdvPayload *)payload;
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) return;
    s_total_size = adv->totalSize;
    s_total_chunks = adv->totalChunks;
    memcpy(s_expected_md5, adv->md5Hex, 32);
    s_expected_md5[32] = '\0';
    for (int i = 0; i < 4 && s_mesh_suspend_handles[i]; i++) vTaskSuspend(s_mesh_suspend_handles[i]);
    led_manager_set_state(LED_STATE_OTA_MESH_CHILD);
    s_targets = routing_get_children();
    for (size_t i = 0; i < s_targets.size(); i++)
        routing_send_unicast(s_targets[i].mac, MSG_OTA_MESH_ENTER, (uint8_t *)adv, sizeof(OtaAdvPayload));
    if (esp_ota_begin(s_update_partition, s_total_size, &s_update_handle) != ESP_OK) return;
    s_current_expected_chunk = 0;
    s_written_size = 0;
    s_state = OTA_MESH_CHILD_ACTIVE;
    ESP_LOGI(TAG, "[OTA_MESH] ENTER recu, propagation aux %u enfant(s).", (unsigned)s_targets.size());
}

/**
 * ROOT reçoit CHUNK_ACK : quand tous les enfants directs ont ACKé, écriture flash + CHUNK_PROPAGATED n.
 * Un seul traitement par chunk (garde s_mesh_root_waiting_chunk == 0xFFFF après traitement).
 */
static void handle_ota_chunk_ack(const uint8_t *src_mac, const uint8_t *payload, uint16_t len) {
    if (s_state != OTA_MESH_ROOT_RECV || !payload || len < 2) return;
    /* Éviter double traitement : ce chunk a déjà été propagé. */
    if (s_mesh_root_waiting_chunk == 0xFFFF) return;
    OtaChunkAckPayload *ack = (OtaChunkAckPayload *)payload;
    if (ack->chunk_index != s_mesh_root_waiting_chunk) return;
    size_t i;
    for (i = 0; i < s_targets.size(); i++) if (memcmp(src_mac, s_targets[i].mac, 6) == 0) break;
    if (i >= s_targets.size() || (i < MAX_CHILDREN_PER_NODE && s_mesh_root_ack_received[i])) return;
    if (i < MAX_CHILDREN_PER_NODE) s_mesh_root_ack_received[i] = true;
    s_mesh_root_acks_count++;
    ESP_LOGD(TAG, "[OTA_MESH] CHUNK_ACK recu chunk %u de enfant %u (total %u/%u)", (unsigned)ack->chunk_index, (unsigned)i, (unsigned)s_mesh_root_acks_count, (unsigned)s_targets.size());
    if (s_mesh_root_acks_count < s_targets.size()) return;
    uint16_t write_len = (s_mesh_root_chunk_index + 1 == s_total_chunks) ? (uint16_t)(s_total_size - (uint32_t)s_mesh_root_chunk_index * OTA_CHUNK_DATA_SIZE) : OTA_CHUNK_DATA_SIZE;
    if (write_len > OTA_CHUNK_DATA_SIZE) write_len = OTA_CHUNK_DATA_SIZE;
    esp_ota_write(s_update_handle, s_mesh_root_chunk_buf, write_len);
    ESP_LOGD(TAG, "[OTA_MESH] CHUNK_PROPAGATED %u emis (flash ecrite)", (unsigned)s_mesh_root_chunk_index);
    char msg[64];
    snprintf(msg, sizeof(msg), "[OTA_MESH] CHUNK_PROPAGATED %u\r\n", (unsigned)s_mesh_root_chunk_index);
    log_dual_printf(msg);
    if (s_mesh_root_chunk_index + 1 >= s_total_chunks) {
        esp_ota_end(s_update_handle);
        for (size_t j = 0; j < s_targets.size(); j++)
            routing_send_unicast(s_targets[j].mac, MSG_OTA_MESH_REBOOT, nullptr, 0);
        s_state = OTA_MESH_ROOT_REBOOT_WAIT;
        s_mesh_reboot_acks_count = 0;
    }
    s_mesh_root_waiting_chunk = 0xFFFF;
    memset(s_mesh_root_ack_received, 0, sizeof(s_mesh_root_ack_received));
    s_mesh_root_ready_for_next_chunk = true;
}

/**
 * (ROOT, mode 0x02) Indique si le chunk courant a été propagé (tous CHUNK_ACK reçus, flash écrite).
 * La passerelle série doit attendre true avant de lire le chunk suivant pour respecter la règle
 * "1 chunk en vol" et garder la cohérence avec le script Python (envoi chunk i → attente CHUNK_PROPAGATED i).
 */
bool ota_tree_ready_for_next_chunk(void) {
    return s_mesh_root_ready_for_next_chunk;
}

/** Enfant reçoit OTA_CHUNK : forward aux enfants, attend CHUNK_ACK de chacun, écrit flash, envoie CHUNK_ACK au parent. */
static void handle_ota_chunk_mesh_child(const uint8_t *src_mac, const uint8_t *payload, uint16_t len) {
    if (s_state != OTA_MESH_CHILD_ACTIVE || !payload || len < (uint16_t)(sizeof(uint16_t)+sizeof(uint8_t))) return;
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac) || memcmp(src_mac, parent_mac, 6) != 0) return;
    OtaChunkPayload *chunk = (OtaChunkPayload *)payload;
    if (chunk->chunk_index != s_current_expected_chunk) return;
    s_mesh_child_current_chunk = chunk->chunk_index;
    uint8_t wlen = chunk->chunk_size; if (wlen > OTA_CHUNK_DATA_SIZE) wlen = OTA_CHUNK_DATA_SIZE;
    memcpy(s_mesh_child_chunk_buf, chunk->data, wlen);
    s_mesh_child_chunk_size = wlen;
    s_mesh_child_pending_acks = s_targets.size();
    memset(s_mesh_child_ack_received, 0, sizeof(s_mesh_child_ack_received));
    if (s_targets.size() > 0) {
        size_t plen = (size_t)(sizeof(uint16_t) + sizeof(uint8_t) + chunk->chunk_size);
        for (size_t i = 0; i < s_targets.size(); i++)
            routing_send_unicast(s_targets[i].mac, MSG_OTA_CHUNK, (uint8_t *)chunk, (uint16_t)plen);
    } else {
        s_mesh_child_pending_acks = 0;
    }
    if (s_mesh_child_pending_acks == 0) {
        esp_ota_write(s_update_handle, s_mesh_child_chunk_buf, s_mesh_child_chunk_size);
        s_written_size += s_mesh_child_chunk_size;
        s_current_expected_chunk++;
        OtaChunkAckPayload ack; ack.chunk_index = chunk->chunk_index;
        routing_send_unicast(parent_mac, MSG_OTA_CHUNK_ACK, (uint8_t *)&ack, sizeof(ack));
        led_flash_rx();
        /* Reboot uniquement après réception de MSG_OTA_MESH_REBOOT (pas ici). */
    }
}

/** Enfant reçoit CHUNK_ACK d'un de ses enfants : quand tous ont ACKé, écriture flash, CHUNK_ACK au parent. */
static void handle_ota_chunk_ack_child(const uint8_t *src_mac, const uint8_t *payload, uint16_t len) {
    if (s_state != OTA_MESH_CHILD_ACTIVE || !payload || len < 2) return;
    OtaChunkAckPayload *ack = (OtaChunkAckPayload *)payload;
    if (ack->chunk_index != s_mesh_child_current_chunk) return;
    size_t idx;
    for (idx = 0; idx < s_targets.size(); idx++) if (memcmp(src_mac, s_targets[idx].mac, 6) == 0) break;
    if (idx >= s_targets.size() || s_mesh_child_ack_received[idx]) return;
    s_mesh_child_ack_received[idx] = true;
    s_mesh_child_pending_acks--;
    if (s_mesh_child_pending_acks > 0) return;
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac)) return;
    esp_ota_write(s_update_handle, s_mesh_child_chunk_buf, s_mesh_child_chunk_size);
    s_written_size += s_mesh_child_chunk_size;
    s_current_expected_chunk++;
    OtaChunkAckPayload out; out.chunk_index = ack->chunk_index;
    routing_send_unicast(parent_mac, MSG_OTA_CHUNK_ACK, (uint8_t *)&out, sizeof(out));
    led_flash_rx();
    /* Reboot après réception de MSG_OTA_MESH_REBOOT. */
}

/** Enfant reçoit OTA_MESH_REBOOT : forward aux enfants, attend REBOOT_ACK, envoie REBOOT_ACK au parent, esp_ota_end/set_boot puis reboot. */
static void do_child_reboot(void) {
    if (esp_ota_end(s_update_handle) != ESP_OK) { led_manager_set_state(LED_STATE_ERROR); return; }
#if OTA_MESH_VERIFY_MD5
    if (!verify_partition_md5()) { led_manager_set_state(LED_STATE_ERROR); vTaskDelay(pdMS_TO_TICKS(2000)); return; }
#endif
    if (s_update_partition && esp_ota_set_boot_partition(s_update_partition) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}
static void handle_ota_mesh_reboot(const uint8_t *src_mac) {
    if (s_state != OTA_MESH_CHILD_ACTIVE && s_state != OTA_MESH_CHILD_REBOOT_WAIT) return;
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac) || memcmp(src_mac, parent_mac, 6) != 0) return;
    if (s_targets.size() > 0) {
        s_state = OTA_MESH_CHILD_REBOOT_WAIT;
        s_mesh_child_pending_acks = s_targets.size();
        memset(s_mesh_child_ack_received, 0, sizeof(s_mesh_child_ack_received));
        for (size_t i = 0; i < s_targets.size(); i++)
            routing_send_unicast(s_targets[i].mac, MSG_OTA_MESH_REBOOT, nullptr, 0);
    } else {
        routing_send_unicast(parent_mac, MSG_OTA_MESH_REBOOT_ACK, nullptr, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
        do_child_reboot();
    }
}

/** Enfant reçoit REBOOT_ACK : quand tous ont ACKé, REBOOT_ACK au parent, esp_ota_end/set_boot puis reboot. */
static void handle_ota_mesh_reboot_ack(const uint8_t *src_mac) {
    if (s_state != OTA_MESH_CHILD_REBOOT_WAIT) return;
    size_t idx;
    for (idx = 0; idx < s_targets.size(); idx++) if (memcmp(src_mac, s_targets[idx].mac, 6) == 0) break;
    if (idx >= s_targets.size() || s_mesh_child_ack_received[idx]) return;
    s_mesh_child_ack_received[idx] = true;
    s_mesh_child_pending_acks--;
    if (s_mesh_child_pending_acks > 0) return;
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac)) return;
    routing_send_unicast(parent_mac, MSG_OTA_MESH_REBOOT_ACK, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    do_child_reboot();
}

/** ROOT reçoit REBOOT_ACK : quand tous ont ACKé, callback reprise tâches, LED ROOT. */
static void handle_ota_mesh_reboot_ack_root(const uint8_t *src_mac) {
    if (s_state != OTA_MESH_ROOT_REBOOT_WAIT) return;
    size_t i;
    for (i = 0; i < s_targets.size(); i++) if (memcmp(src_mac, s_targets[i].mac, 6) == 0) break;
    if (i >= s_targets.size()) return;
    s_mesh_reboot_acks_count++;
    if (s_mesh_reboot_acks_count < s_targets.size()) return;
    s_state = OTA_IDLE;
    s_update_partition = nullptr;
    led_manager_set_state(LED_STATE_ROOT);
    if (s_mesh_done_cb) s_mesh_done_cb();
    ESP_LOGI(TAG, "[OTA_MESH] ROOT: tous reboot ACK recus, reprise normale.");
}

static void handle_ota_adv(const uint8_t *src_mac, const uint8_t *payload) {
    if (!payload) return;
    OtaAdvPayload *adv = (OtaAdvPayload *)payload;
    if (s_state != OTA_IDLE) return;

#if OTA_MESH_ACCEPT_ONLY_FROM_PARENT
    /* Sécurité : n'accepter l'annonce OTA que depuis le parent (évite injection par un tiers). */
    uint8_t parent_mac[6];
    if (!routing_get_parent_mac(parent_mac)) {
        /* ROOT n'a pas de parent : il ne reçoit pas d'ADV par le mesh (il est la source). Ignorer. */
        return;
    }
    if (memcmp(src_mac, parent_mac, 6) != 0) {
        ESP_LOGW(TAG, "OTA ADV rejete: source n'est pas le parent (securite mesh).");
        return;
    }
#endif

    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        ESP_LOGE(TAG, "Pas de partition OTA pour reception mesh.");
        return;
    }
    s_total_size = adv->totalSize;
    s_total_chunks = adv->totalChunks;
    memcpy(s_expected_md5, adv->md5Hex, 32);
    s_current_expected_chunk = 0;
    s_written_size = 0;

    esp_err_t err = esp_ota_begin(s_update_partition, s_total_size, &s_update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed %d", err);
        return;
    }
    ESP_LOGI(TAG, "Recu OTA ADV. Taille: %lu, chunks: %u. Passage en DOWNLOADING.", (unsigned long)s_total_size, (unsigned)s_total_chunks);
    s_state = OTA_RECEIVING_MESH;
    s_last_chunk_tick = xTaskGetTickCount();
    led_manager_set_state(LED_STATE_OTA);
    send_ota_req(0);
}

/**
 * @brief Réception d'un chunk OTA par la radio (PULL) - écriture séquentielle avec retry.
 *
 * @details
 * Vérifie chunk_index == current_expected_chunk. Si oui : esp_ota_write, incrémente,
 * demande le suivant ou termine (esp_ota_end, set_boot_partition, MSG_OTA_DONE, restart).
 */
static void handle_ota_chunk(const uint8_t *payload, uint16_t len) {
    if (s_state != OTA_RECEIVING_MESH || !payload || len < (uint16_t)(sizeof(uint16_t) + sizeof(uint8_t))) return;

    OtaChunkPayload *chunk = (OtaChunkPayload *)payload;
    if (chunk->chunk_index != s_current_expected_chunk) return; /* Ignorer ou retry géré par timeout */

    uint8_t write_len = chunk->chunk_size;
    if (write_len > 200) write_len = 200;

    if (esp_ota_write(s_update_handle, chunk->data, write_len) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed chunk %u", (unsigned)chunk->chunk_index);
        return;
    }
    s_last_chunk_tick = xTaskGetTickCount();
    s_written_size += write_len;
    led_flash_rx();

    s_current_expected_chunk++;

    if (s_current_expected_chunk >= s_total_chunks) {
        /* Réception complète : fin OTA, éventuelle vérif MD5, puis reboot */
        if (esp_ota_end(s_update_handle) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed");
            led_manager_set_state(LED_STATE_ERROR);
            return;
        }
        uint8_t parent_mac[6];
        if (routing_get_parent_mac(parent_mac))
            routing_send_unicast(parent_mac, MSG_OTA_DONE, nullptr, 0);

#if OTA_MESH_VERIFY_MD5
        if (!verify_partition_md5()) {
            ESP_LOGE(TAG, "OTA mesh: refus set_boot (MD5 invalide). Redemarrage sur ancienne app.");
            led_manager_set_state(LED_STATE_ERROR);
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
            return;
        }
#endif
        if (s_update_partition && esp_ota_set_boot_partition(s_update_partition) == ESP_OK) {
            ESP_LOGI(TAG, "OTA mesh OK. Reboot dans 2s.");
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            led_manager_set_state(LED_STATE_ERROR);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        esp_restart();
    } else {
        send_ota_req(s_current_expected_chunk);
    }
}

/**
 * @brief Traitement d'une demande de chunk (MSG_OTA_REQ) - serveur lit la partition et envoie MSG_OTA_CHUNK.
 *
 * @details
 * Lit le bloc demandé depuis la partition OTA (req->requested_chunk_index * 200),
 * taille 200 ou moins pour le dernier chunk. Envoie OtaChunkPayload en unicast à l'enfant.
 */
static void handle_ota_req(const uint8_t *src_mac, const uint8_t *payload) {
    if (s_state != OTA_DISTRIBUTING || !s_update_partition || !payload) return;
    if (s_current_target_idx >= s_targets.size()) return;
    if (memcmp(src_mac, s_targets[s_current_target_idx].mac, 6) != 0) return;

    OtaReqPayload *req = (OtaReqPayload *)payload;
    uint16_t idx = req->requested_chunk_index;
    if (idx >= s_total_chunks) return;
    uint32_t offset = (uint32_t)idx * OTA_CHUNK_DATA_SIZE;
    uint32_t remaining = (s_total_size > offset) ? (s_total_size - offset) : 0;
    uint16_t read_len = (remaining > OTA_CHUNK_DATA_SIZE) ? OTA_CHUNK_DATA_SIZE : (uint16_t)remaining;
    if (read_len == 0) return;

    uint8_t chunkData[200];
    if (esp_partition_read(s_update_partition, offset, chunkData, read_len) != ESP_OK) return;

    OtaChunkPayload resp;
    resp.chunk_index = idx;
    resp.chunk_size = (uint8_t)read_len;
    memcpy(resp.data, chunkData, read_len);
    if (read_len < 200) memset(resp.data + read_len, 0, 200 - read_len);

    size_t payload_len = (size_t)(sizeof(uint16_t) + sizeof(uint8_t) + read_len);
    routing_send_unicast(src_mac, MSG_OTA_CHUNK, (uint8_t *)&resp, (uint16_t)payload_len);
    s_last_req_time = xTaskGetTickCount();
}

/**
 * @brief Notification de fin de téléchargement.
 *
 * @details
 * Appelé quand un soldat dit à son chef : "C'est bon, j'ai tout reçu !".
 * Le chef note que ce soldat a fini et passe au suivant.
 */
static void handle_ota_done(const uint8_t *src_mac) {
    if (s_state != OTA_DISTRIBUTING)
        return;
    if (s_current_target_idx < s_targets.size())
    {
        if (memcmp(src_mac, s_targets[s_current_target_idx].mac, 6) == 0)
        {
            ESP_LOGI(TAG, "Enfant %d a fini sa MAJ.", s_current_target_idx);
            s_current_child_done = true;
        }
    }
}

/**
 * @brief Aiguillage des messages OTA reçus par radio.
 *
 * @details
 * Regarde le type de message OTA (Pub, Morceau, Demande, Fini) et appelle la bonne fonction.
 */
void ota_tree_on_mesh_message(const uint8_t *src_mac, uint8_t msgType, const uint8_t *payload, uint16_t len) {
    switch (msgType)
    {
    case MSG_OTA_MESH_ENTER:
        handle_ota_mesh_enter(src_mac, payload, len);
        break;
    case MSG_OTA_CHUNK_ACK:
        if (LEXACARE_THIS_NODE_IS_GATEWAY)
            handle_ota_chunk_ack(src_mac, payload, len);
        else
            handle_ota_chunk_ack_child(src_mac, payload, len);
        break;
    case MSG_OTA_MESH_REBOOT:
        handle_ota_mesh_reboot(src_mac);
        break;
    case MSG_OTA_MESH_REBOOT_ACK:
        if (LEXACARE_THIS_NODE_IS_GATEWAY)
            handle_ota_mesh_reboot_ack_root(src_mac);
        else
            handle_ota_mesh_reboot_ack(src_mac);
        break;
    case MSG_OTA_ADV:
        handle_ota_adv(src_mac, payload);
        break;
    case MSG_OTA_CHUNK:
        if (s_state == OTA_MESH_CHILD_ACTIVE)
            handle_ota_chunk_mesh_child(src_mac, payload, len);
        else
            handle_ota_chunk(payload, len);
        break;
    case MSG_OTA_REQ:
        handle_ota_req(src_mac, payload);
        break;
    case MSG_OTA_DONE:
        handle_ota_done(src_mac);
        break;
    }
}

/**
 * @brief Le Gestionnaire de Mise à Jour (Tâche de fond).
 *
 * @details
 * C'est le cerveau qui gère l'état de la mise à jour.
 *
 * - **Si je suis en train de recevoir (RECEIVING_MESH)** :
 *   Je regarde ce qu'il me manque et j'envoie des demandes ("Donne-moi la suite") à mon chef.
 *   Quand j'ai tout, je préviens mon chef ("J'ai fini") et je deviens Distributeur.
 *
 * - **Si je suis en train de distribuer (DISTRIBUTING)** :
 *   Je m'occupe de mes soldats un par un.
 *   J'envoie la pub ("J'ai une maj") au soldat en cours.
 *   J'attends qu'il ait fini.
 *   Quand tous mes soldats ont fini, je redémarre (Reboot) pour appliquer la mise à jour sur moi-même.
 */
void ota_tree_task(void *pv) {
    while (1)
    {
        if (s_state == OTA_RECEIVING_MESH)
        {
            /* Timeout 500 ms : renvoyer la requête du chunk attendu (résilience perte paquets). */
            TickType_t now = xTaskGetTickCount();
            if ((now - s_last_chunk_tick) >= pdMS_TO_TICKS(OTA_REQ_TIMEOUT_MS))
            {
                send_ota_req(s_current_expected_chunk);
                s_last_chunk_tick = now;
            }
        }
        else if (s_state == OTA_DISTRIBUTING)
        {
            if (s_current_target_idx < s_targets.size())
            {
                // Envoyer ADV à l'enfant courant
                if (!s_current_child_done)
                {
                    static size_t s_last_logged_adv_idx = (size_t)-1;
                    if (s_last_logged_adv_idx != s_current_target_idx) {
                        ESP_LOGI(TAG, "OTA: envoi ADV vers enfant %u (nodeId 0x%04X)", (unsigned)s_current_target_idx, (unsigned)s_targets[s_current_target_idx].nodeId);
                        s_last_logged_adv_idx = s_current_target_idx;
                    }
                    OtaAdvPayload adv;
                    adv.totalSize = s_total_size;
                    adv.totalChunks = s_total_chunks;
                    memcpy(adv.md5Hex, s_expected_md5, 32);
                    routing_send_unicast(s_targets[s_current_target_idx].mac, MSG_OTA_ADV, (uint8_t *)&adv, sizeof(adv));

                    // Timeout si l'enfant ne répond pas (ex: 30s)
                    if (xTaskGetTickCount() - s_last_req_time > pdMS_TO_TICKS(30000))
                    {
                        ESP_LOGW(TAG, "Timeout enfant %d, passage au suivant", s_current_target_idx);
                        s_current_target_idx++;
                        s_current_child_done = false;
                        s_last_req_time = xTaskGetTickCount();
                    }
                }
                else
                {
                    s_current_target_idx++;
                    s_current_child_done = false;
                    s_last_req_time = xTaskGetTickCount();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            else
            {
                // Tous les enfants servis (ou aucun enfant)
#if LEXACARE_THIS_NODE_IS_GATEWAY
                /* ROOT : OTA mesh ne met à jour que les enfants, pas le ROOT. Pas de reboot. */
                ESP_LOGI(TAG, "Distribution terminee (ROOT). Enfants mis a jour, ROOT reste sur firmware actuel.");
                s_state = OTA_IDLE;
                s_update_partition = nullptr;
                led_manager_set_state(LED_STATE_ROOT);
#else
                /* Noeud intermédiaire : après avoir distribué à ses enfants, s'installer et rebooter. */
                ESP_LOGI(TAG, "Distribution terminee. Validation partition puis reboot...");
                if (s_update_partition) {
                    esp_err_t err = esp_ota_set_boot_partition(s_update_partition);
                    if (err == ESP_OK) {
                        for (int i = 0; i < 4; i++) {
                            led_manager_set_state(LED_STATE_CONNECTED);
                            vTaskDelay(pdMS_TO_TICKS(150));
                            led_manager_set_state(LED_STATE_OTA);
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                    } else {
                        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed %d", err);
                        led_manager_set_state(LED_STATE_ERROR);
                        vTaskDelay(pdMS_TO_TICKS(2000));
                    }
                }
                esp_restart();
#endif
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
