/**
 * @file sys_storage.h
 * @brief Interface publique du composant sys_storage — SPIFFS, log circulaire, panic handler.
 *
 * Système de fichiers : SPIFFS (built-in ESP-IDF v5.4, composant "spiffs").
 * Point de montage    : /spiffs
 * Partition           : "storage" (1 Mo, subtype spiffs dans partitions.csv)
 *
 * Note : la migration vers LittleFS est possible en remplaçant :
 *   esp_spiffs.h → esp_littlefs.h (composant joltwallet/esp_littlefs)
 *   esp_vfs_spiffs_conf_t → esp_vfs_littlefs_conf_t
 *   esp_vfs_spiffs_register → esp_vfs_littlefs_register
 *   esp_spiffs_info → esp_littlefs_info
 */

#pragma once

#include "esp_err.h"

/**
 * @defgroup group_storage Stockage & Logs
 * @brief Gestion du stockage local (SPIFFS) et mécanismes de log/panic.
 *
 * Pourquoi ce module existe :
 * - conserver des traces (logs) même si le PC n'est pas connecté
 * - aider au diagnostic après un redémarrage inattendu (panic handler)
 *
 * @{
 */

/* ================================================================
 * Constantes
 * ================================================================ */
#define STORAGE_MOUNT_POINT     "/spiffs"
#define STORAGE_PARTITION       "storage"
#define LOG_FILE_PATH           STORAGE_MOUNT_POINT "/app.log"
#define PANIC_FILE_PATH         STORAGE_MOUNT_POINT "/panic.log"
#define LOG_MAX_SIZE_BYTES      (900UL * 1024UL)  /**< Seuil de rotation */
#define LOG_TRIM_PERCENT        10                 /**< % d'entrées à supprimer */

/* Alias de compatibilité (maintenu pour les composants existants) */
#define LITTLEFS_MOUNT_POINT    STORAGE_MOUNT_POINT
#define LITTLEFS_PARTITION      STORAGE_PARTITION

/* ================================================================
 * littlefs_manager_init
 * @brief Monte la partition SPIFFS ("storage") sur /spiffs.
 *
 * Format automatique si la partition n'a jamais été utilisée.
 * Vérifie l'espace disponible avec esp_spiffs_info() après montage.
 *
 * @return ESP_OK si le montage réussit, code d'erreur sinon.
 * ================================================================ */
esp_err_t littlefs_manager_init(void);

/* ================================================================
 * log_write
 * @brief Écrit un message horodaté dans le log circulaire SPIFFS.
 *
 * Thread-safe (mutex interne). Rotation automatique si le fichier
 * dépasse LOG_MAX_SIZE_BYTES (suppression des LOG_TRIM_PERCENT% anciens).
 *
 * @param msg  Message à journaliser (chaîne C terminée par '\0').
 * @return ESP_OK, ESP_ERR_TIMEOUT ou ESP_FAIL.
 * ================================================================ */
esp_err_t log_write(const char *msg);

/* ================================================================
 * panic_handler_install
 * @brief Installe le shutdown handler et prépare la capture RTC.
 *
 * @return ESP_OK si l'installation réussit.
 * ================================================================ */
esp_err_t panic_handler_install(void);

/* ================================================================
 * panic_handler_check_on_boot
 * @brief Transcrit le crash RTC dans panic.log si présent.
 *
 * Appeler APRÈS littlefs_manager_init().
 * ================================================================ */
void panic_handler_check_on_boot(void);

/** @} */ /* end of group_storage */
