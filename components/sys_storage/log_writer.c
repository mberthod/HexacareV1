/**
 * @file log_writer.c
 * @ingroup group_storage
 * @brief Log circulaire sur LittleFS — rotation automatique à 900 Ko.
 *
 * Algorithme de rotation :
 *   1. Ouvrir le fichier en mode "a" (append).
 *   2. Écrire le message horodaté.
 *   3. Si ftell() dépasse LOG_MAX_SIZE_BYTES :
 *      a. Lire tout le fichier en mémoire (allocation heap).
 *      b. Calculer le nombre d'octets à ignorer (LOG_TRIM_PERCENT %).
 *      c. Avancer jusqu'à la fin de la ligne correspondante.
 *      d. Réécrire le reste du fichier depuis le début.
 *
 * Thread-safety : mutex FreeRTOS créé à la première écriture.
 */

#include "sys_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char    *TAG        = "log_writer";
static SemaphoreHandle_t s_mutex = NULL;

/* ================================================================
 * log_trim_file (interne)
 * @brief Supprime les LOG_TRIM_PERCENT % d'entrées les plus anciennes.
 *
 * Appelée uniquement depuis log_write() quand le fichier dépasse
 * LOG_MAX_SIZE_BYTES. Alloue un buffer temporaire sur le tas.
 *
 * @param file_size Taille actuelle du fichier en octets.
 * ================================================================ */
static void log_trim_file(long file_size)
{
    FILE *f = fopen(LOG_FILE_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "Impossible d'ouvrir %s pour rotation", LOG_FILE_PATH);
        return;
    }

    /* Nombre d'octets à ignorer (10 % les plus anciens) */
    long skip_target = file_size / LOG_TRIM_PERCENT;
    long skipped     = 0;
    char line[256];

    /* Avancer jusqu'à la fin de la ligne qui dépasse skip_target */
    while (skipped < skip_target) {
        if (fgets(line, sizeof(line), f) == NULL) {
            break;
        }
        skipped += (long)strlen(line);
    }

    long rest_start = ftell(f);
    long rest_size  = file_size - rest_start;

    if (rest_size <= 0) {
        fclose(f);
        remove(LOG_FILE_PATH);
        ESP_LOGI(TAG, "Rotation : fichier vide après suppression, recréé");
        return;
    }

    /* Lire le contenu restant */
    char *buf = malloc((size_t)rest_size + 1);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "Rotation : allocation mémoire échouée (%ld octets)", rest_size);
        return;
    }

    size_t read_bytes = fread(buf, 1, (size_t)rest_size, f);
    fclose(f);

    /* Réécrire le fichier sans les entrées supprimées */
    f = fopen(LOG_FILE_PATH, "w");
    if (f) {
        fwrite(buf, 1, read_bytes, f);
        fclose(f);
    }

    free(buf);
    ESP_LOGI(TAG, "Rotation du log : %ld octets anciens supprimés, %ld conservés",
             rest_start, (long)read_bytes);
}

/* ================================================================
 * log_write
 * @brief Écrit un message horodaté dans le log circulaire LittleFS.
 *
 * Format d'une entrée : "[XXXXXX.XXX] message\n"
 * où XXXXXX.XXX = secondes depuis le boot (esp_timer_get_time / 1e6).
 *
 * @param msg Message à journaliser (chaîne C terminée par '\0').
 * @return ESP_OK, ESP_ERR_TIMEOUT ou ESP_FAIL.
 * ================================================================ */
esp_err_t log_write(const char *msg)
{
    if (!msg) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Création du mutex lors du premier appel (init paresseuse) */
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Horodatage en secondes depuis le boot */
    int64_t us   = esp_timer_get_time();
    double  secs = (double)us / 1e6;

    FILE *f = fopen(LOG_FILE_PATH, "a");
    if (!f) {
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    fprintf(f, "[%10.3f] %s\n", secs, msg);
    long current_pos = ftell(f);
    fclose(f);

    /* Déclenchement de la rotation si le seuil est dépassé */
    if (current_pos >= (long)LOG_MAX_SIZE_BYTES) {
        log_trim_file(current_pos);
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
