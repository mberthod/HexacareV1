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
void serial_gateway_send_data_json(const void *frame, uint32_t fw_ver);

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
 * @brief Enregistre les tâches à suspendre pendant OTA Série (seule serial_gateway_task reste active).
 * @param mesh_handle Tâche mesh_flooding_task (ou NULL).
 * @param tx_handle Tâche espnowTxTask (ou NULL).
 * @param sensor_handle Tâche sensor_sim (ou NULL, ex. sensor_sim_get_task_handle()).
 * @details À appeler depuis setup() après création des tâches. Pendant réception 0x01 + chunks, ces tâches sont suspendues puis reprises à la fin.
 */
void serial_gateway_register_tasks_for_ota_suspend(TaskHandle_t mesh_handle, TaskHandle_t tx_handle, TaskHandle_t sensor_handle);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_GATEWAY_H */
