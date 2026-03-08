/**
 * @file mesh_mqtt.cpp
 * @brief Gestion du réseau Mesh. Sortie des données uniquement par port série.
 *
 * Le nœud ROOT (connecté au PC en série) reçoit les données du mesh et les
 * envoie sur le port série uniquement — pas de MQTT WiFi. Les autres nœuds
 * sont des nœuds mesh standard (pas de sortie MQTT).
 */

#include "config/config.h"
#include "mesh_mqtt.h"
#include "ota_mesh.h"
#include "system/system_state.h"
#include "system/log_dual.h"
#include "rtos/queues_events.h"
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Arduino.h>
#include "esp_log.h"

static const char* TAG = "MESH";

static painlessMesh mesh;       ///< Instance du réseau Mesh
static bool s_is_root = false;  ///< true si ce nœud est ROOT (sortie série uniquement)
static uint32_t s_last_send = 0; ///< Horodatage du dernier envoi JSON sur série

/**
 * @brief Callback de réception de message Mesh.
 * @param from ID du nœud expéditeur.
 * @param msg Contenu du message.
 */
static void mesh_receive_cb(uint32_t from, const String &msg) {
    ESP_LOGD(TAG, "Message reçu de 0x%08X (len=%d)", from, msg.length());
    if (msg.length() >= 10)
        ota_mesh_on_message(from, (const uint8_t *)msg.c_str(), msg.length());
}

/**
 * @brief Callback de changement de topologie Mesh.
 * Met à jour le statut ROOT du nœud.
 */
static void mesh_changed_cb() {
    if (mesh.isRoot()) {
        ESP_LOGI(TAG, "Ce nœud est maintenant ROOT");
        s_is_root = true;
    } else {
        ESP_LOGD(TAG, "Ce nœud est un satellite");
        s_is_root = false;
    }
    system_state_set_mesh_root(s_is_root);
}

/**
 * @brief Construit l'objet JSON complet (état système).
 * Inclut : MAC, batterie, rails, chute, Lidar, vitaux, audio, température.
 * @param buf Buffer de sortie.
 * @param buf_size Taille du buffer.
 * @return Nombre d'octets écrits, ou 0 en cas d'erreur.
 */
static size_t build_state_json(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return 0;
    StaticJsonDocument<512> doc;
    char mac[18];
    system_state_get_mac_address(mac, sizeof(mac));
    doc["mac_address"] = mac;

    rails_status_t r;
    system_state_get_rails(&r);
    doc["battery_mv"] = r.v_batt_mv;

    JsonObject rails = doc.createNestedObject("rails_status");
    rails["1v8_mv"] = r.v_1v8_mv;
    rails["3v3_mv"] = r.v_3v3_mv;

    doc["fall_detected"] = system_state_get_fall_detected();

    matrix_summary_t m;
    system_state_get_matrix_summary(&m);
    JsonObject mat = doc.createNestedObject("matrix_summary");
    mat["min_mm"] = m.min_mm;
    mat["max_mm"] = m.max_mm;
    mat["sum_mm"] = m.sum_mm;
    mat["valid_zones"] = m.valid_zones;

    vital_signs_t v;
    system_state_get_vitals(&v);
    doc["heart_rate"] = v.heart_rate_bpm;
    doc["breath_rate"] = v.breath_rate_bpm;

    doc["audio_level"] = system_state_get_audio_level();
    doc["sys_temp"] = system_state_get_sys_temp();

    return serializeJson(doc, buf, buf_size);
}

/**
 * @brief Envoie le JSON d'état du ROOT sur le port série uniquement (pas de MQTT).
 */
static void build_and_print_json_to_serial(void) {
    char buf[512];
    size_t n = build_state_json(buf, sizeof(buf));
    if (n > 0) {
        log_dual_println(buf);
        ESP_LOGD(TAG, "ROOT: JSON envoye sur port serie (%u octets)", (unsigned)n);
    }
}

/**
 * @brief Initialise le réseau Mesh (pas de client MQTT : ROOT sort uniquement en série).
 */
void mesh_mqtt_init(void) {
    ESP_LOGI(TAG, "Initialisation Mesh (SSID: %s)...", MESH_SSID);
    mesh.init(MESH_SSID, MESH_PASSWORD, (uint16_t)MESH_PORT);
    String mac = WiFi.macAddress();
    ESP_LOGI(TAG, "MAC Address: %s", mac.c_str());
    system_state_set_mac_address(mac.c_str());
    mesh.onReceive(mesh_receive_cb);
    mesh.onChangedConnections(mesh_changed_cb);
    s_last_send = 0;
}

/**
 * @brief Boucle de gestion Mesh (à appeler sur le Core 0).
 * Le nœud ROOT envoie les données uniquement sur le port série, pas en MQTT WiFi.
 */
void mesh_mqtt_loop(void) {
    mesh.update();
    if (!s_is_root) return;

    uint32_t now = millis();
    if (now - s_last_send >= MQTT_PUB_INTERVAL_MS) {
        s_last_send = now;
        build_and_print_json_to_serial();
    }
}

/**
 * @brief Vérifie si le nœud actuel est ROOT.
 * @return true si ROOT.
 */
bool mesh_mqtt_is_root(void) {
    return s_is_root;
}
