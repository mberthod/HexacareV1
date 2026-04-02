/**
 * @file fall_detection_ai.cc
 * @ingroup group_ai_engine
 * @brief Moteur d'inférence de détection de chute — dual-mode ESP-DL / seuil.
 *
 * Ce fichier est compilé en C++ (requis pour l'API ESP-DL).
 * L'interface externe est exposée via extern "C" pour la compatibilité
 * avec main.c et les autres composants C.
 *
 * === MODE MODÈLE (CONFIG_AI_USE_ESPDL_MODEL=y) ===
 *   Charge /littlefs/fall_model.espdl via dl::Model::load().
 *   Construit un Tensor<int16_t> 8×32×1 depuis lidar_matrix_t.
 *   Exécute model->run(). Lit la sortie softmax [P_normal, P_chute].
 *   Si P_chute > CONFIG_AI_FALL_CONFIDENCE_THRESHOLD% → AI_CHUTE_DETECTEE.
 *
 * === MODE SEUIL (CONFIG_AI_USE_ESPDL_MODEL=n, défaut) ===
 *   Algorithme géométrique sur lidar_matrix_t :
 *   1. Calcul de la distance moyenne de la zone inférieure (lignes 6-7).
 *   2. Comparaison avec l'historique glissant (CONFIG_AI_HISTORY_LEN trames).
 *   3. Si variation > CONFIG_AI_FALL_THRESHOLD_PERCENT% ET distance < 1500 mm
 *      → AI_CHUTE_DETECTEE (confiance proportionnelle à la variation).
 *
 * === RÈGLE DE PRIORITÉ MESH (contrainte stricte) ===
 *   AI_CHUTE_DETECTEE → xQueueSendToFront(ai_to_mesh_queue, ...)
 *   Autres états       → xQueueSend(ai_to_mesh_queue, ...)
 */

extern "C" {
#include "fall_detection_ai.h"
#include "system_types.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
}

#include <cstring>
#include <cstdlib>
#include <algorithm>

/* Valeurs par défaut si Kconfig.projbuild n'a pas encore été intégré
 * au cache CMake (premier build sans idf.py reconfigure).
 * Lancer "idf.py reconfigure" puis "idf.py build" pour utiliser menuconfig. */
#ifndef CONFIG_AI_HISTORY_LEN
#define CONFIG_AI_HISTORY_LEN              8
#endif
#ifndef CONFIG_AI_FALL_THRESHOLD_PERCENT
#define CONFIG_AI_FALL_THRESHOLD_PERCENT   80
#endif
#ifndef CONFIG_AI_FALL_CONFIDENCE_THRESHOLD
#define CONFIG_AI_FALL_CONFIDENCE_THRESHOLD 85
#endif

/* Inclusion conditionnelle ESP-DL */
#ifdef CONFIG_AI_USE_ESPDL_MODEL
/* Décommenter après installation du composant espressif/esp-dl :
 * #include "dl_model_base.hpp"
 */
#warning "ESP-DL activé mais les headers ne sont pas inclus. Installer espressif/esp-dl via idf.py update-dependencies."
#endif

static const char *TAG = "ai_engine";

/* ================================================================
 * Constantes de la tâche
 * ================================================================ */
#define TASK_STACK_SIZE     (12288)
#define TASK_PRIORITY       (9)
#define TASK_CORE           (1)

/* Seuil de distance sol : en dessous = personne allongée */
#define FALL_DISTANCE_MAX_MM    1500

/* ================================================================
 * ai_task_ctx_t (interne)
 * @brief Contexte de la tâche d'inférence.
 * ================================================================ */
struct ai_task_ctx_t {
    sys_context_t *sys_ctx;

    /* Historique pour le mode seuil */
    float  dist_history[CONFIG_AI_HISTORY_LEN];
    int    history_head;
    bool   history_full;

#ifdef CONFIG_AI_USE_ESPDL_MODEL
    /* dl::Model *model; */  /* Pointeur vers le modèle chargé */
    bool model_loaded;
#endif
};

/* ================================================================
 * threshold_avg_lower_zone (interne)
 * @brief Calcule la distance moyenne des lignes inférieures (6-7) de la matrice.
 *
 * Les lignes du bas correspondent à la zone sol — une distance faible
 * et soudaine indique une personne allongée (chute potentielle).
 *
 * @param matrix Matrice LIDAR 8×32.
 * @return Distance moyenne en mm (0 si toutes les zones invalides).
 * ================================================================ */
static float threshold_avg_lower_zone(const lidar_matrix_t *matrix)
{
    float sum   = 0.0f;
    int   count = 0;

    /* Lignes 6 et 7 = zone la plus basse du champ de vision */
    for (int row = 6; row < LIDAR_ROWS; row++) {
        for (int col = 0; col < LIDAR_COLS; col++) {
            uint16_t d = matrix->data[row][col];
            if (d > 0 && d < 5000) { /* Filtrer les mesures invalides */
                sum += (float)d;
                count++;
            }
        }
    }

    return (count > 0) ? (sum / (float)count) : 0.0f;
}

/* ================================================================
 * threshold_detect_fall (interne)
 * @brief Algorithme de détection par seuil sur l'historique glissant.
 *
 * @param ctx      Contexte de la tâche (historique).
 * @param current  Distance moyenne actuelle de la zone inférieure.
 * @param confidence Pointeur vers la confiance calculée (0–100).
 * @return ai_event_state_e détecté.
 * ================================================================ */
static ai_event_state_e threshold_detect_fall(ai_task_ctx_t *ctx,
                                                float current_mm,
                                                uint8_t *confidence)
{
    /* Mise à jour de l'historique */
    ctx->dist_history[ctx->history_head] = current_mm;
    ctx->history_head = (ctx->history_head + 1) % CONFIG_AI_HISTORY_LEN;
    if (ctx->history_head == 0) ctx->history_full = true;

    int len = ctx->history_full ? CONFIG_AI_HISTORY_LEN : ctx->history_head;
    if (len < 2) {
        *confidence = 0;
        return AI_NORMAL;
    }

    /* Référence = moyenne des (len-1) premières valeurs de l'historique */
    float ref_sum = 0.0f;
    for (int i = 0; i < len - 1; i++) {
        int idx = (ctx->history_head - len + i + CONFIG_AI_HISTORY_LEN)
                  % CONFIG_AI_HISTORY_LEN;
        ref_sum += ctx->dist_history[idx];
    }
    float ref_avg = ref_sum / (float)(len - 1);

    if (ref_avg < 1.0f) {
        *confidence = 0;
        return AI_NORMAL;
    }

    /* Variation relative entre la mesure actuelle et la référence historique */
    float variation_pct = ((ref_avg - current_mm) / ref_avg) * 100.0f;

    ESP_LOGD(TAG, "Zone bas: %.0f mm, réf: %.0f mm, variation: %.1f%%",
             current_mm, ref_avg, variation_pct);

    /* Seuil de chute : distance faible ET variation significative */
    float threshold = (float)CONFIG_AI_FALL_THRESHOLD_PERCENT;

    if (variation_pct >= threshold && current_mm < (float)FALL_DISTANCE_MAX_MM) {
        /* Confiance proportionnelle à la variation au-delà du seuil */
        float excess = variation_pct - threshold;
        float conf   = 50.0f + (excess / threshold) * 50.0f;
        *confidence  = (uint8_t)std::min(conf, 100.0f);
        return AI_CHUTE_DETECTEE;
    }

    if (variation_pct >= threshold * 0.5f) {
        *confidence = (uint8_t)(variation_pct / threshold * 40.0f);
        return AI_MOUVEMENT_ANORMAL;
    }

    *confidence = 0;
    return AI_NORMAL;
}

#ifdef CONFIG_AI_USE_ESPDL_MODEL
/* ================================================================
 * espdl_detect_fall (interne)
 * @brief Inférence CNN via ESP-DL.
 *
 * @param ctx     Contexte de la tâche (modèle chargé).
 * @param matrix  Matrice LIDAR 8×32.
 * @param confidence Confiance retournée (0–100).
 * @return État détecté.
 * ================================================================ */
static ai_event_state_e espdl_detect_fall(ai_task_ctx_t *ctx,
                                            const lidar_matrix_t *matrix,
                                            uint8_t *confidence)
{
    if (!ctx->model_loaded) {
        *confidence = 0;
        return AI_NORMAL;
    }

    /* Construction du tenseur d'entrée int16_t [1, 8, 32, 1]
     * (Décommenter après intégration de l'ULD ESP-DL) :
     *
     * int16_t input_data[LIDAR_ROWS * LIDAR_COLS];
     * for (int r = 0; r < LIDAR_ROWS; r++) {
     *     for (int c = 0; c < LIDAR_COLS; c++) {
     *         input_data[r * LIDAR_COLS + c] = (int16_t)matrix->data[r][c];
     *     }
     * }
     *
     * dl::Tensor<int16_t> input;
     * input.set_shape({1, LIDAR_ROWS, LIDAR_COLS, 1});
     * input.set_element(input_data);
     *
     * ctx->model->run(&input);
     *
     * dl::Tensor<float> *output = ctx->model->get_output(0);
     * float p_normal = output->get_element_ptr()[0];
     * float p_fall   = output->get_element_ptr()[1];
     *
     * *confidence = (uint8_t)(p_fall * 100.0f);
     * if (*confidence >= CONFIG_AI_FALL_CONFIDENCE_THRESHOLD) {
     *     return AI_CHUTE_DETECTEE;
     * }
     * if (p_fall >= 0.3f) {
     *     return AI_MOUVEMENT_ANORMAL;
     * }
     */

    *confidence = 0;
    return AI_NORMAL;
}
#endif /* CONFIG_AI_USE_ESPDL_MODEL */

/* ================================================================
 * send_ai_event (interne)
 * @brief Pousse un ai_event_t dans les queues réseau et diagnostic.
 *
 * Règle de priorité (contrainte stricte) :
 *   AI_CHUTE_DETECTEE → xQueueSendToFront (court-circuite la télémétrie)
 *   Autres états       → xQueueSend (FIFO normale)
 *
 * @param ctx   Contexte de la tâche.
 * @param event Événement à envoyer.
 * ================================================================ */
static void send_ai_event(const ai_task_ctx_t *ctx, const ai_event_t *event)
{
    BaseType_t mesh_ret, diag_ret;

    if (event->state == AI_CHUTE_DETECTEE) {
        /* Priorité HAUTE : injection en tête de queue réseau */
        mesh_ret = xQueueSendToFront(ctx->sys_ctx->ai_to_mesh_queue,
                                      event, 0);
        ESP_LOGW(TAG, "CHUTE DÉTECTÉE (confiance %u%%) — priorité mesh activée",
                 event->confidence);
    } else {
        /* Priorité normale : FIFO standard */
        mesh_ret = xQueueSend(ctx->sys_ctx->ai_to_mesh_queue, event, 0);
    }

    /* Diagnostic PC : toujours FIFO */
    diag_ret = xQueueSend(ctx->sys_ctx->diag_to_pc_queue, event, 0);

    if (mesh_ret != pdTRUE) {
        ESP_LOGD(TAG, "ai_to_mesh_queue pleine — événement perdu");
    }
    if (diag_ret != pdTRUE) {
        ESP_LOGD(TAG, "diag_to_pc_queue pleine — événement perdu");
    }
}

/* ================================================================
 * task_ai_inference (interne)
 * @brief Tâche FreeRTOS d'inférence — Core 1, priorité 9.
 *
 * @param pvParam Pointeur vers ai_task_ctx_t alloué par ai_engine_task_start.
 * ================================================================ */
static void task_ai_inference(void *pvParam)
{
    ai_task_ctx_t *ctx = static_cast<ai_task_ctx_t *>(pvParam);

    /* Abonnement au TWDT */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

#ifdef CONFIG_AI_USE_ESPDL_MODEL
    /* Chargement du modèle ESP-DL depuis LittleFS */
    /* ctx->model = dl::Model::load("/littlefs/fall_model.espdl", MEM_AUTO); */
    /* ctx->model_loaded = (ctx->model != nullptr); */
    ctx->model_loaded = false; /* Passer à true après intégration de l'ULD */
    if (!ctx->model_loaded) {
        ESP_LOGE(TAG, "Modèle /littlefs/fall_model.espdl non trouvé — "
                      "basculement sur mode seuil de secours");
    } else {
        ESP_LOGI(TAG, "Modèle ESP-DL chargé (seuil confiance : %d%%)",
                 CONFIG_AI_FALL_CONFIDENCE_THRESHOLD);
    }
#else
    ESP_LOGI(TAG, "Mode seuil actif (seuil variation : %d%%, historique : %d trames)",
             CONFIG_AI_FALL_THRESHOLD_PERCENT, CONFIG_AI_HISTORY_LEN);
#endif

    ESP_LOGI(TAG, "Task_AI_Inference démarrée sur Core %d", xPortGetCoreID());

    sensor_frame_t frame;

    while (true) {
        /* Reset watchdog même si aucune trame n'arrive encore.
         * Important : au boot, la tâche d'acquisition peut démarrer après nous
         * (ou être absente si LIDAR KO). Un blocage portMAX_DELAY déclenche le TWDT. */
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        /* Attente d'une trame capteur avec timeout (permet de nourrir le TWDT) */
        if (xQueueReceive(ctx->sys_ctx->sensor_to_ai_queue,
                           &frame, pdMS_TO_TICKS(500)) != pdTRUE) {
            continue;
        }

        if (!frame.lidar_valid) {
            continue; /* Ignorer les trames sans données LIDAR */
        }

        ai_event_t event = {
            .state        = AI_NORMAL,
            .confidence   = 0,
            .timestamp_us = esp_timer_get_time(),
        };

#ifdef CONFIG_AI_USE_ESPDL_MODEL
        if (ctx->model_loaded) {
            event.state = espdl_detect_fall(ctx, &frame.lidar, &event.confidence);
        } else {
            /* Repli sur le mode seuil si le modèle n'est pas disponible */
            float avg = threshold_avg_lower_zone(&frame.lidar);
            event.state = threshold_detect_fall(ctx, avg, &event.confidence);
        }
#else
        float avg = threshold_avg_lower_zone(&frame.lidar);
        event.state = threshold_detect_fall(ctx, avg, &event.confidence);
#endif

        /* Envoi des événements non normaux */
        if (event.state != AI_NORMAL) {
            send_ai_event(ctx, &event);
        }
    }
}

/* ================================================================
 * ai_engine_task_start (extern "C")
 * @brief Alloue le contexte et crée Task_AI_Inference sur Core 1.
 *
 * @param ctx Pointeur vers le contexte système.
 * @return ESP_OK si la tâche est créée avec succès.
 * ================================================================ */
extern "C" esp_err_t ai_engine_task_start(sys_context_t *ctx)
{
    ai_task_ctx_t *task_ctx = static_cast<ai_task_ctx_t *>(
        calloc(1, sizeof(ai_task_ctx_t)));
    if (!task_ctx) {
        return ESP_ERR_NO_MEM;
    }

    task_ctx->sys_ctx     = ctx;
    task_ctx->history_head = 0;
    task_ctx->history_full = false;
    memset(task_ctx->dist_history, 0, sizeof(task_ctx->dist_history));

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_ai_inference,
        "Task_AI_Inference",
        TASK_STACK_SIZE,
        task_ctx,
        TASK_PRIORITY,
        nullptr,
        TASK_CORE);

    if (ret != pdPASS) {
        free(task_ctx);
        ESP_LOGE(TAG, "Échec création Task_AI_Inference");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Task_AI_Inference créée (Core %d, priorité %d)",
             TASK_CORE, TASK_PRIORITY);
    return ESP_OK;
}
