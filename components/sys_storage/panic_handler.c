/**
 * @file panic_handler.c
 * @ingroup group_storage
 * @brief Capture des paniques système et écriture dans LittleFS.
 *
 * Architecture :
 *   Le système ESP-IDF ne permet pas un accès fiable au système de fichiers
 *   pendant une panique matérielle (le heap peut être corrompu). La stratégie
 *   adoptée est donc à deux niveaux :
 *
 *   Niveau 1 — shutdown handler (redémarrages propres via esp_restart()) :
 *     Appelé avant le reset. Écrit un message de redémarrage dans panic.log.
 *
 *   Niveau 2 — RTC memory (paniques matérielles, watchdog) :
 *     Lors d'une panique, un flag et un message courts sont stockés en
 *     RTC_DATA_ATTR (survit au reset sans coupure secteur).
 *     Au prochain boot, panic_handler_check_on_boot() lit ce flag et
 *     l'écrit dans panic.log avant d'effacer le flag.
 *
 * Note : CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y dans sdkconfig.defaults
 *        garantit que le backtrace complet est imprimé sur la console UART
 *        avant le redémarrage — complément indispensable pour le débogage.
 */

#include "sys_storage.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "panic_handler";

/* ================================================================
 * Variables RTC — survivent au reset sans coupure secteur
 * ================================================================ */
static RTC_DATA_ATTR uint32_t s_panic_magic = 0;
static RTC_DATA_ATTR char     s_panic_msg[192];
static RTC_DATA_ATTR int64_t  s_panic_time_us;

#define PANIC_MAGIC_VALUE  0xDEADCAFEU

/* ================================================================
 * shutdown_handler (interne)
 * @brief Appelée par esp_restart() — redémarrage propre.
 *
 * Positionne le flag RTC avec un message de redémarrage volontaire.
 * La trame sera écrite dans panic.log au prochain boot.
 * ================================================================ */
static void shutdown_handler(void)
{
    s_panic_magic   = PANIC_MAGIC_VALUE;
    s_panic_time_us = esp_timer_get_time();
    strncpy(s_panic_msg, "Redémarrage propre via esp_restart()", sizeof(s_panic_msg) - 1);
    s_panic_msg[sizeof(s_panic_msg) - 1] = '\0';
}

/* ================================================================
 * panic_handler_install
 * @brief Installe le shutdown handler et initialise les variables RTC.
 *
 * @return ESP_OK si l'installation réussit.
 * ================================================================ */
esp_err_t panic_handler_install(void)
{
    ESP_ERROR_CHECK(esp_register_shutdown_handler(shutdown_handler));
    ESP_LOGI(TAG, "Shutdown handler installé");
    return ESP_OK;
}

/* ================================================================
 * panic_handler_check_on_boot
 * @brief Vérifie le flag RTC et écrit l'info dans panic.log si présent.
 *
 * Doit être appelée APRÈS littlefs_manager_init() pour que le
 * système de fichiers soit disponible.
 * ================================================================ */
void panic_handler_check_on_boot(void)
{
    if (s_panic_magic != PANIC_MAGIC_VALUE) {
        /* Aucun crash précédent détecté */
        return;
    }

    ESP_LOGW(TAG, "Crash détecté lors du précédent boot — écriture dans panic.log");

    /* Formatage du message avec horodatage */
    char entry[256];
    double secs = (double)s_panic_time_us / 1e6;
    snprintf(entry, sizeof(entry),
             "[%10.3f] PANIC: %s\n",
             secs, s_panic_msg);

    /* Écriture bloquante/synchrone dans panic.log */
    FILE *f = fopen(PANIC_FILE_PATH, "a");
    if (f) {
        fwrite(entry, 1, strlen(entry), f);
        fflush(f);
        fclose(f);
        ESP_LOGI(TAG, "Entrée de panique écrite dans %s", PANIC_FILE_PATH);
    } else {
        ESP_LOGE(TAG, "Impossible d'ouvrir %s", PANIC_FILE_PATH);
    }

    /* Effacement du flag pour ne pas réécrire au prochain boot */
    s_panic_magic = 0;
    memset(s_panic_msg, 0, sizeof(s_panic_msg));
}
