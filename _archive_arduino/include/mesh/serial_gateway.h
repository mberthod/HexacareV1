/**
 * @file serial_gateway.h
 * @brief ROOT (Node 0) : pont série. Sortie = JSON pour MSG_TYPE_DATA.
 * @details Entrée : protocole binaire [1 octet mode][38 octets header][N×200 octets chunks]
 *   mode 0x01 = OTA Série (ROOT se flashe), 0x02 = OTA Mesh (ROOT diffuse).
 *   Ou lignes texte : OTA_CHUNK:{index}:{total}:{400 hex} pour broadcast mesh.
 */

#ifndef SERIAL_GATEWAY_H
#define SERIAL_GATEWAY_H

#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise la gateway série (port, buffer). À appeler si ce nœud est ROOT/Gateway.
 * @return 1 si OK, 0 en erreur.
 */
int serial_gateway_init(void);

/**
 * @brief Formate une trame Data (LexaFullFrame) en JSON et l'envoie sur Serial.
 * @param frame Trame 32 octets (CRC déjà vérifié).
 * @param fw_ver Version firmware à inclure dans le JSON.
 * @details Exemple : {"nodeId":"0xABCD","vBat":3700,"probFallLidar":15,"tempExt":2250,"fw_ver":"1.0"}
 */
void serial_gateway_send_data_json(const void *frame);

/**
 * @brief Point d'entrée de la tâche qui lit le port série et parse les commandes (OTA_CHUNK:...).
 * @param pv Paramètre non utilisé.
 * @details À lier à xTaskCreatePinnedToCore(..., CORE_PRO). Parse OTA_CHUNK:index:total:hex et envoie en broadcast ESP-NOW.
 */
void serial_gateway_task(void *pv);

/**
 * @brief Indique si une réception OTA Série (mode 0x01) est en cours sur le port série.
 * @return 1 si le ROOT reçoit des chunks OTA depuis le Serial, 0 sinon.
 * @details Tant que 1, le mesh ne doit pas injecter OTA_ADV/OTA_CHUNK dans ota_mesh (éviter mélange avec les chunks série).
 */
// int serial_gateway_is_ota_serial_receiving(void);

/**
 * @brief Enregistre les tâches à suspendre pendant OTA locale (0x01) — seule serial_gateway_task reste active.
 * @param routing_handle Tâche routing_task (ou NULL).
 * @param ota_handle Tâche ota_tree_task (ou NULL).
 * @param data_tx_handle Tâche dataTxTask (ou NULL).
 * @param sensor_handle Tâche sensor_sim (ou NULL, ex. sensor_sim_get_task_handle()).
 * @details À appeler depuis setup() après création des tâches. Dès réception de 0x01, ces tâches sont suspendues pour un flux OTA série très rapide ; à la fin le nœud reboote (pas de reprise).
 */
void serial_gateway_register_tasks_for_ota_suspend(TaskHandle_t routing_handle, TaskHandle_t ota_handle, TaskHandle_t data_tx_handle, TaskHandle_t sensor_handle);

/** Reprend les tâches après fin OTA mesh (0x02) sur le ROOT. À enregistrer via ota_tree_register_mesh_done_cb(). */
void serial_gateway_resume_tasks_after_ota_mesh(void);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_GATEWAY_H */
