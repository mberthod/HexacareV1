/**
 * @file littlefs_manager.c
 * @ingroup group_storage
 * @brief Gestion du montage SPIFFS sur la partition "storage".
 *
 * Implémentation via esp_spiffs (built-in ESP-IDF v5.4).
 * L'API publique reste identique — les fichiers POSIX (fopen/fclose/etc.)
 * fonctionnent de la même façon avec SPIFFS qu'avec LittleFS.
 *
 * Note : SPIFFS ne supporte pas les répertoires. Tous les fichiers
 * sont à la racine du point de montage (/spiffs/).
 */

#include "sys_storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "storage";

/* ================================================================
 * littlefs_manager_init
 * @brief Monte la partition SPIFFS avec la configuration complète.
 *
 * Utilise esp_vfs_spiffs_conf_t (équivalent de la "root file
 * configuration" de LittleFS) :
 *   - base_path            = STORAGE_MOUNT_POINT ("/spiffs")
 *   - partition_label      = STORAGE_PARTITION   ("storage")
 *   - max_files            = 8
 *   - format_if_mount_failed = true
 *
 * Après montage, l'espace est vérifié avec esp_spiffs_info().
 *
 * @return ESP_OK si le montage réussit, code d'erreur sinon.
 * ================================================================ */
esp_err_t littlefs_manager_init(void)
{
    /* Configuration complète du montage SPIFFS */
    const esp_vfs_spiffs_conf_t conf = {
        .base_path              = STORAGE_MOUNT_POINT,
        .partition_label        = STORAGE_PARTITION,
        .max_files              = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Échec du montage ou du formatage de SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partition '%s' introuvable dans la table de partitions",
                     STORAGE_PARTITION);
        } else {
            ESP_LOGE(TAG, "Erreur montage SPIFFS : %s", esp_err_to_name(ret));
        }
        return ret;
    }

    /* Vérification post-montage de l'espace disponible */
    size_t total_bytes = 0;
    size_t used_bytes  = 0;
    ESP_ERROR_CHECK(esp_spiffs_info(STORAGE_PARTITION, &total_bytes, &used_bytes));

    ESP_LOGI(TAG, "SPIFFS monté sur '%s' — %u Ko total, %u Ko utilisés, %u Ko libres",
             STORAGE_MOUNT_POINT,
             (unsigned)(total_bytes / 1024),
             (unsigned)(used_bytes  / 1024),
             (unsigned)((total_bytes - used_bytes) / 1024));

    return ESP_OK;
}
