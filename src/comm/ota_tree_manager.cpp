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

#include "comm/ota_tree_manager.h"
#include "comm/routing_manager.h"
#include "system/led_manager.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <vector>

static const char *TAG = "OTA_TREE";

enum OtaState
{
    OTA_IDLE,
    OTA_RECEIVING_UART, // ROOT: Reçoit du PC
    OTA_RECEIVING_MESH, // CHILD: Reçoit du Parent
    OTA_DISTRIBUTING,   // SERVER: Distribue aux enfants
    OTA_FINISHED        // Prêt à rebooter
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
    if (s_state != OTA_RECEIVING_UART) return;
    s_total_size = total_size;
    s_total_chunks = total_chunks;
    if (md5) strncpy(s_expected_md5, md5, 32);
    s_expected_md5[32] = '\0';
    s_state = OTA_DISTRIBUTING;
    s_targets = routing_get_children();
    s_current_target_idx = 0;
    s_current_child_done = false;
    ESP_LOGI(TAG, "Propagation demarree: %u chunks vers %u enfant(s).", (unsigned)s_total_chunks, (unsigned)s_targets.size());
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
    if (s_state == OTA_IDLE)
    {
        s_state = OTA_RECEIVING_UART;
        s_total_size = totalSize;
        strncpy(s_expected_md5, md5, 32);
        s_written_size = 0;
        ESP_LOGI(TAG, "Debut OTA UART. Taille: %lu", s_total_size);
        led_manager_set_state(LED_STATE_OTA);
    }

    if (s_state != OTA_RECEIVING_UART)
        return;

    if (esp_partition_write(s_update_partition, offset, data, len) != ESP_OK)
    {
        ESP_LOGE(TAG, "Erreur ecriture flash offset %lu", offset);
        led_manager_set_state(LED_STATE_ERROR);
        return;
    }
    s_written_size += len;
    led_flash_rx(); // Feedback visuel écriture flash

    if (s_written_size >= s_total_size)
    {
        s_total_chunks = (uint16_t)((s_total_size + OTA_CHUNK_DATA_SIZE - 1) / OTA_CHUNK_DATA_SIZE);
        if (s_uart_ota_mode == 0x01) {
            // OTA Série : ROOT seul, pas de diffusion -> reboot immédiat
            ESP_LOGI(TAG, "Reception UART terminee (mode Serie ROOT). Pas de diffusion.");
            s_state = OTA_DISTRIBUTING;
            s_targets.clear();
            s_current_target_idx = 0;
            s_current_child_done = false;
        }
        /* Mode 0x02 : la propagation est démarrée par ota_tree_start_propagation() depuis serial_gateway. */
    }
}

/**
 * @brief Réception d'une annonce de mise à jour (Publicité) - démarre le PULL.
 *
 * @details
 * Si IDLE : obtient la partition OTA, appelle esp_ota_begin, stocke total_chunks,
 * passe en DOWNLOADING et envoie la première requête MSG_OTA_REQ(0).
 */
static void handle_ota_adv(const uint8_t *src_mac, const uint8_t *payload) {
    if (!payload) return;
    OtaAdvPayload *adv = (OtaAdvPayload *)payload;
    if (s_state != OTA_IDLE) return;

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
        /* Réception complète : fin OTA, validation, reboot */
        if (esp_ota_end(s_update_handle) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed");
            led_manager_set_state(LED_STATE_ERROR);
            return;
        }
        uint8_t parent_mac[6];
        if (routing_get_parent_mac(parent_mac))
            routing_send_unicast(parent_mac, MSG_OTA_DONE, nullptr, 0);
        if (s_update_partition && esp_ota_set_boot_partition(s_update_partition) == ESP_OK) {
            ESP_LOGI(TAG, "OTA mesh OK. Reboot dans 2s.");
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
    case MSG_OTA_ADV:
        handle_ota_adv(src_mac, payload);
        break;
    case MSG_OTA_CHUNK:
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
                // Tous les enfants servis (ou aucun enfant) -> valider et rebooter
                ESP_LOGI(TAG, "Distribution terminee. Validation partition puis reboot...");
                if (s_update_partition) {
                    esp_err_t err = esp_ota_set_boot_partition(s_update_partition);
                    if (err == ESP_OK) {
                        /* 4x clignotement vert = succès (visible avant reset) */
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
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
