/**
 * @file ota_node.c
 * @ingroup group_mesh
 * @brief OTA nœud-à-nœud via ESP-NOW — fragments 200 octets.
 *
 * Protocole :
 *   1. Réception d'un fragment OTA_FRAGMENT déchiffré (ota_fragment_payload_t).
 *   2. Premier fragment (offset == 0) : appel esp_ota_begin().
 *   3. esp_ota_write() avec les données du fragment.
 *   4. Retransmission immédiate du fragment brut (chiffré) aux enfants.
 *   5. Dernier fragment (is_last == true) :
 *      esp_ota_end() → vérification MD5 → esp_ota_set_boot_partition()
 *      → log_write("OTA terminé") → esp_restart().
 */

#include "ota_node.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_now.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ota_node";

/* ================================================================
 * État interne de l'OTA
 * ================================================================ */
typedef struct {
    esp_ota_handle_t    ota_handle;
    const esp_partition_t *ota_partition;
    bool                in_progress;
    uint32_t            bytes_written;
    uint32_t            firmware_size;
} ota_state_t;

static ota_state_t s_ota; /* Instance unique du module OTA */

/* ================================================================
 * ota_node_init
 * @brief Initialise l'état OTA (réinitialise si une OTA était en cours).
 *
 * @return ESP_OK toujours.
 * ================================================================ */
esp_err_t ota_node_init(void)
{
    memset(&s_ota, 0, sizeof(s_ota));

    /* Identification de la partition de mise à jour */
    s_ota.ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_ota.ota_partition) {
        ESP_LOGW(TAG, "Aucune partition OTA disponible");
    } else {
        ESP_LOGI(TAG, "Partition OTA cible : %s (offset 0x%08lX, taille %lu Ko)",
                 s_ota.ota_partition->label,
                 s_ota.ota_partition->address,
                 s_ota.ota_partition->size / 1024);
    }

    return ESP_OK;
}

/* ================================================================
 * ota_node_process_fragment
 * @brief Traite un fragment OTA et le retransmet aux enfants.
 *
 * @param frag          Payload du fragment (données en clair).
 * @param children_mac  Tableau des MACs des enfants (peut être NULL).
 * @param child_count   Nombre d'enfants.
 * @param raw_frame     Trame brute chiffrée à retransmettre.
 * @param frame_len     Longueur de la trame brute.
 * @return ESP_OK si le traitement réussit.
 * ================================================================ */
esp_err_t ota_node_process_fragment(const ota_fragment_payload_t *frag,
                                     const uint8_t (*children_mac)[6],
                                     int child_count,
                                     const uint8_t *raw_frame,
                                     size_t frame_len)
{
    if (!frag) return ESP_ERR_INVALID_ARG;
    if (!s_ota.ota_partition) {
        ESP_LOGE(TAG, "Pas de partition OTA — fragment ignoré");
        return ESP_FAIL;
    }

    /* Premier fragment : initialisation de l'OTA */
    if (frag->fragment_offset == 0) {
        if (s_ota.in_progress) {
            ESP_LOGW(TAG, "Nouvelle OTA démarrée — abandon de l'OTA précédente");
            esp_ota_abort(s_ota.ota_handle);
        }

        ESP_ERROR_CHECK(esp_ota_begin(s_ota.ota_partition,
                                       OTA_WITH_SEQUENTIAL_WRITES,
                                       &s_ota.ota_handle));
        s_ota.in_progress   = true;
        s_ota.bytes_written = 0;
        s_ota.firmware_size = frag->firmware_size;
        ESP_LOGI(TAG, "OTA démarrée — firmware attendu : %lu octets",
                 frag->firmware_size);
    }

    if (!s_ota.in_progress) {
        ESP_LOGW(TAG, "Fragment OTA reçu hors séquence — ignoré");
        return ESP_FAIL;
    }

    /* Vérification de la cohérence de l'offset */
    if (frag->fragment_offset != s_ota.bytes_written) {
        ESP_LOGW(TAG, "Fragment hors ordre : attendu %lu, reçu %lu",
                 s_ota.bytes_written, frag->fragment_offset);
        return ESP_FAIL;
    }

    /* Écriture du fragment dans la partition OTA */
    ESP_ERROR_CHECK(esp_ota_write(s_ota.ota_handle,
                                   frag->data,
                                   frag->fragment_data_len));
    s_ota.bytes_written += frag->fragment_data_len;

    ESP_LOGD(TAG, "OTA : %lu / %lu octets écrits (%.1f%%)",
             s_ota.bytes_written, s_ota.firmware_size,
             (float)s_ota.bytes_written / (float)s_ota.firmware_size * 100.0f);

    /* Retransmission immédiate aux enfants (propagation du fragment) */
    if (raw_frame && frame_len > 0 && child_count > 0) {
        for (int i = 0; i < child_count; i++) {
            esp_err_t tx_ret = esp_now_send(children_mac[i],
                                             raw_frame, frame_len);
            if (tx_ret != ESP_OK) {
                ESP_LOGW(TAG, "Retransmission OTA enfant %d échouée : %s",
                         i, esp_err_to_name(tx_ret));
            }
        }
        ESP_LOGD(TAG, "Fragment retransmis à %d enfant(s)", child_count);
    }

    /* Dernier fragment : finalisation */
    if (frag->is_last) {
        ESP_LOGI(TAG, "Dernier fragment reçu — finalisation OTA");

        esp_err_t end_ret = esp_ota_end(s_ota.ota_handle);
        if (end_ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end échoué : %s", esp_err_to_name(end_ret));
            s_ota.in_progress = false;
            return end_ret;
        }

        esp_err_t boot_ret = esp_ota_set_boot_partition(s_ota.ota_partition);
        if (boot_ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition échoué : %s",
                     esp_err_to_name(boot_ret));
            s_ota.in_progress = false;
            return boot_ret;
        }

        ESP_LOGI(TAG, "OTA terminée — redémarrage dans 1 s");
        s_ota.in_progress = false;

        /* Délai court pour laisser les logs s'afficher */
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    return ESP_OK;
}
