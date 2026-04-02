/**
 * @file pc_diag.h
 * @brief Interface publique du diagnostic PC — USB-CDC, JSON, commandes Python.
 *
 * Task_Diag_PC (Core 0, priorité 3) :
 *   - Émet toutes les 5 s un rapport JSON sur stdout (USB Serial/JTAG).
 *   - Écoute les commandes JSON entrantes sur stdin.
 *   - Commande "download_logs" : transfert de /littlefs/app.log par blocs.
 *
 * Protocole de commande (depuis l'outil Python) :
 *   → {"cmd":"download_logs"}
 *   ← {"status":"ok","filename":"app.log","size":XXXXX}
 *   ← [blocs bruts ou base64 du fichier]
 *   ← {"status":"done"}
 *
 *   → {"cmd":"clear_logs"}
 *   ← {"status":"ok"}
 *
 *   → {"cmd":"get_diag"}
 *   ← [rapport JSON immédiat]
 */

#pragma once

#include "system_types.h"
#include "esp_err.h"

/**
 * @defgroup group_pc_diag Diagnostic PC (JSON)
 * @brief Sortie USB en JSON lisible + commandes simples depuis l'outil PC.
 *
 * Ce module sert de “fenêtre” sur le firmware :
 * - envoie un état régulier (santé système + mesures capteurs)
 * - permet quelques actions (récupérer/effacer les logs) sans recompiler
 *
 * @{
 */

/* ================================================================
 * pc_diag_task_start
 * @brief Crée la tâche Task_Diag_PC épinglée sur le Core 0.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
esp_err_t pc_diag_task_start(sys_context_t *ctx);

/** @} */ /* end of group_pc_diag */
