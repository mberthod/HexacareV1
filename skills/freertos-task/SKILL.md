# SKILL : freertos-task

Pattern pour ajouter une tâche FreeRTOS au firmware LexaCare. Tout écart doit
être justifié dans le PR.

## Quand utiliser

- Ajout d'une nouvelle tâche périodique ou réactive (ex : telemetry, health check, state reporter)
- Découpage d'une tâche existante devenue trop grosse

## Quand NE PAS utiliser

- Pour un simple callback — utiliser plutôt `esp_event` (event loop partagé)
- Pour un timer périodique léger — `esp_timer_create()` (callback en contexte ISR soft)

## Pattern canonique

### 1. Header `firmware/main/task_<nom>.h`

```c
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// Queue publiée par la tâche (si applicable)
extern QueueHandle_t my_result_q;

// Entry point. Appelé depuis app_main via xTaskCreatePinnedToCore.
void task_my_name_entry(void *arg);

// Paramètres (pour le cas où on veut configurer depuis app_main)
typedef struct {
    uint32_t period_ms;
    uint8_t  some_config;
} task_my_name_cfg_t;
```

### 2. Source `firmware/main/task_<nom>.c`

```c
#include "task_my_name.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "task_my";

QueueHandle_t my_result_q = NULL;

void task_my_name_entry(void *arg)
{
    const task_my_name_cfg_t *cfg = (const task_my_name_cfg_t *)arg;

    // Init queue (une seule fois)
    if (my_result_q == NULL) {
        my_result_q = xQueueCreate(8, sizeof(my_result_t));
        configASSERT(my_result_q != NULL);
    }

    // Register au watchdog si tâche critique
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "started, period=%lu ms", cfg->period_ms);

    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(cfg->period_ms);

    for (;;) {
        esp_task_wdt_reset();

        // --- corps de la tâche ---
        my_result_t result = { /* ... */ };
        if (xQueueSend(my_result_q, &result, 0) != pdPASS) {
            ESP_LOGW(TAG, "queue full, dropping sample");
        }

        vTaskDelayUntil(&next_wake, period);
    }
}
```

### 3. Création dans `app_main.c`

```c
#include "task_my_name.h"

void app_main(void)
{
    // ... init autres modules ...

    static task_my_name_cfg_t my_cfg = {
        .period_ms = 100,
        .some_config = 0,
    };

    BaseType_t ok = xTaskCreatePinnedToCore(
        task_my_name_entry,   // entry
        "task_my",            // name (<16 chars)
        4096,                 // stack size en bytes (pas en words !)
        &my_cfg,              // arg
        3,                    // priority
        NULL,                 // handle (inutile si pas de xTaskNotify)
        tskNO_AFFINITY        // ou 0 (Core 0) / 1 (Core 1)
    );
    configASSERT(ok == pdPASS);
}
```

### 4. Update `CMakeLists.txt` de `main/`

```cmake
idf_component_register(
    SRCS "app_main.c"
         "task_audio.c"
         "task_vision.c"
         "orchestrator.c"
         "task_my_name.c"       # <-- ajouté
    INCLUDE_DIRS "."
    REQUIRES ... esp_task_wdt
)
```

## Choix priorité et cœur

| Type de tâche | Priorité | Cœur recommandé | Stack |
|---------------|----------|-----------------|-------|
| Acquisition capteur critique (I2S, SPI haute fréquence) | 5 | pinned | 8 KB |
| Traitement signal + inférence | 4 | même cœur que l'acquisition | 8 KB |
| Logique applicative (FSM) | 3 | Core 1 (laisser Core 0 au réseau si actif) | 4 KB |
| Orchestration, fusion, reporting | 2–3 | Core 1 | 4 KB |
| Log, telemetry, health | 2 | `tskNO_AFFINITY` | 3 KB |
| Monitoring / watchdog feeder | 1 | `tskNO_AFFINITY` | 2 KB |

Rester dans la plage 1–5. Au-dessus on entre en conflit avec les tâches IDF
internes (LwIP, WiFi, timer svc).

## Gotchas

- La taille de stack `xTaskCreatePinnedToCore` est en **bytes**, pas en words comme sur d'autres ports FreeRTOS. Erreur commune.
- `vTaskDelayUntil()` est préféré à `vTaskDelay()` pour les tâches périodiques — le deuxième dérive si le corps prend du temps variable.
- Ne jamais faire un `esp_restart()` depuis une tâche non-supervisor. Publier un événement, laisser le supervisor redémarrer.
- Si la tâche fait du bloquant réseau (socket), `tskNO_AFFINITY` recommandé pour que le scheduler la migre entre cœurs selon la charge.
