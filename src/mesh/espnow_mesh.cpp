/**
 * @file espnow_mesh.cpp
 * @brief Le Facteur (Couche Radio).
 *
 * @details
 * Ce module est les "jambes" du système. Il ne réfléchit pas au contenu du message,
 * il se contente de le transporter d'un point A à un point B.
 *
 * Il utilise la technologie **ESP-NOW**, qui est comme un WiFi ultra-rapide et simplifié :
 * - Pas de connexion à une Box Internet.
 * - Les boîtiers se parlent directement entre eux (Peer-to-Peer).
 * - C'est très rapide (quelques millisecondes).
 *
 * Quand il reçoit un paquet, il regarde l'étiquette et le donne au bon service :
 * - C'est pour le GPS ? -> `routing_manager`
 * - C'est une mise à jour ? -> `ota_tree_manager`
 * - C'est des données ? -> `serial_gateway` (si c'est le ROOT) ou renvoi au parent.
 */

#include "mesh/espnow_mesh.h"
#include "mesh/routing_manager.h"
#include "mesh/serial_gateway.h"
#include "system/led_manager.h"
#include "config/config.h"
#include "lexacare_protocol.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_log.h>

static const char *TAG = "ESPNOW_MESH";

// Callback de réception ESP-NOW
static void on_espnow_recv(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    if (len < sizeof(TreeMeshHeader))
        return;

    // Feedback visuel (Flash Cyan)
    led_flash_rx();

    TreeMeshHeader *hdr = (TreeMeshHeader *)data;

    // 1. Routage (Beacons, Joins, Heartbeats)
    on_mesh_receive(mac_addr, data, len);

    // 2. Data (Forwarding)
    if (hdr->msgType == MSG_DATA)
    {
        if (g_lexacare_this_node_is_gateway)
        {
            // ROOT: Envoyer au PC via Serial Gateway
            // Le payload est une LexaFullFrame (ou DataPayload V2)
            // On passe le pointeur vers le payload (après le header)
            if (len - sizeof(TreeMeshHeader) >= sizeof(LexaFullFrame_t))
            {
                // TODO: Passer aussi les infos de topologie (srcNodeId, layer) au serial gateway
                // Pour l'instant on passe juste la frame brute
                serial_gateway_send_data_json((void *)(data + sizeof(TreeMeshHeader)));
            }
        }
        else
        {
            // NOEUD: Forward upstream vers parent (TTL décrémenté, CDC)
            routing_forward_upstream(data, (size_t)len);
        }
    }
}

// Callback d'envoi (juste pour debug/LED)
static void on_espnow_send(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        led_flash_tx();
    }
}

int espnow_mesh_init(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Erreur init ESP-NOW");
        return 0;
    }
    ESP_LOGI(TAG, "ESP-NOW Init OK ");

    esp_now_register_recv_cb(on_espnow_recv);
    esp_now_register_send_cb(on_espnow_send);

    ESP_LOGI(TAG, "ESP-NOW Init OK (Channel %d)", ESPNOW_CHANNEL);
    return 1;
}



