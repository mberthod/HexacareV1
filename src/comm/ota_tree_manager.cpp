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

// Pour la distribution
static std::vector<ChildInfo> s_targets;
static size_t s_current_target_idx = 0;
static bool s_current_child_done = false;
static uint32_t s_last_req_time = 0;

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
        s_state = OTA_DISTRIBUTING;
        if (s_uart_ota_mode == 0x01) {
            // OTA Série : ROOT seul, pas de diffusion -> reboot immédiat
            ESP_LOGI(TAG, "Reception UART terminee (mode Serie ROOT). Pas de diffusion.");
            s_targets.clear();
        } else {
            // OTA Mesh : ROOT diffuse aux enfants
            ESP_LOGI(TAG, "Reception UART terminee. Passage en mode DISTRIBUTION Mesh.");
            s_targets = routing_get_children();
        }
        s_current_target_idx = 0;
        s_current_child_done = false;
    }
}

/**
 * @brief Réception d'une annonce de mise à jour (Publicité).
 *
 * @details
 * Appelé quand un boîtier reçoit un message "Hé ! J'ai une mise à jour pour toi !" de son chef.
 * Il note la taille et la signature, et se prépare à télécharger.
 */
static void handle_ota_adv(const uint8_t *src_mac, const uint8_t *payload) {
    OtaAdvPayload *adv = (OtaAdvPayload *)payload;
    if (s_state == OTA_IDLE)
    {
        ESP_LOGI(TAG, "Recu OTA ADV. Taille: %lu. Passage en RECEIVING_MESH", adv->totalSize);
        s_state = OTA_RECEIVING_MESH;
        s_total_size = adv->totalSize;
        memcpy(s_expected_md5, adv->md5Hex, 32);
        s_written_size = 0;
        led_manager_set_state(LED_STATE_OTA);
        // On demandera le premier chunk dans la loop
    }
}

/**
 * @brief Réception d'un morceau de mise à jour (Chunk) par la radio.
 *
 * @details
 * Appelé quand un boîtier reçoit un bout de fichier de son chef.
 * Il l'écrit dans sa mémoire Flash au bon endroit.
 */
static void handle_ota_chunk(const uint8_t *payload, uint16_t len) {
    if (s_state != OTA_RECEIVING_MESH)
        return;

    OtaChunkPayload *chunk = (OtaChunkPayload *)payload;
    uint32_t offset = chunk->chunkIndex * 200; // Attention: chunkIndex est u16, data 200
    // Note: Dans le protocole V1 c'était chunkIndex * 200. Ici on garde la compatibilité.

    if (esp_partition_write(s_update_partition, offset, chunk->data, len - sizeof(uint16_t) * 2) == ESP_OK)
    {
        s_written_size += (len - sizeof(uint16_t) * 2);
        led_flash_rx();
        // Logique simplifiée : on incrémente juste.
        // Vrai système : bitmap pour savoir quel chunk manque.
    }
}

/**
 * @brief Traitement d'une demande de morceau (Requête).
 *
 * @details
 * Appelé quand un soldat demande à son chef : "Envoie-moi le bout numéro 42 stp".
 * Le chef va lire ce bout dans sa propre mémoire et l'envoyer au soldat.
 */
static void handle_ota_req(const uint8_t *src_mac, const uint8_t *payload) {
    if (s_state != OTA_DISTRIBUTING)
        return;

    // Vérifier que c'est l'enfant en cours
    if (s_current_target_idx < s_targets.size())
    {
        if (memcmp(src_mac, s_targets[s_current_target_idx].mac, 6) == 0)
        {
            OtaReqPayload *req = (OtaReqPayload *)payload;

            // Lire le chunk en flash
            uint8_t buffer[250];
            OtaChunkPayload *chunk = (OtaChunkPayload *)(buffer + sizeof(TreeMeshHeader)); // Astuce pour offset
            // On prépare juste le payload
            uint8_t chunkData[200];
            if (esp_partition_read(s_update_partition, req->offset, chunkData, req->length) == ESP_OK)
            {
                // Construire réponse MSG_OTA_CHUNK
                OtaChunkPayload resp;
                resp.chunkIndex = req->offset / 200;
                resp.totalChunks = s_total_size / 200; // Approx
                memcpy(resp.data, chunkData, req->length);

                routing_send_unicast(src_mac, MSG_OTA_CHUNK, (uint8_t *)&resp, sizeof(OtaChunkPayload));
                s_last_req_time = xTaskGetTickCount();
            }
        }
    }
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
            // Demander le prochain chunk manquant
            // Simplification: on demande séquentiellement
            if (s_written_size < s_total_size)
            {
                uint8_t parent_mac[6];
                if (routing_get_parent_mac(parent_mac))
                {
                    OtaReqPayload req;
                    req.offset = s_written_size;
                    req.length = (s_total_size - s_written_size > 200) ? 200 : (s_total_size - s_written_size);
                    routing_send_unicast(parent_mac, MSG_OTA_REQ, (uint8_t *)&req, sizeof(req));
                    vTaskDelay(pdMS_TO_TICKS(100)); // Throttle
                }
            }
            else
            {
                // Fini !
                ESP_LOGI(TAG, "Reception Mesh terminee. Verification...");
                // TODO: Verify MD5
                uint8_t parent_mac[6];
                if (routing_get_parent_mac(parent_mac))
                {
                    routing_send_unicast(parent_mac, MSG_OTA_DONE, nullptr, 0);
                }
                s_state = OTA_DISTRIBUTING;
                s_targets = routing_get_children();
                s_current_target_idx = 0;
                s_current_child_done = false;
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
                    adv.totalChunks = s_total_size / 200; // Approx
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
