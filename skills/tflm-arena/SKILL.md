# SKILL : tflm-arena

Gestion des **tensor arenas** TensorFlow Lite Micro pour LexaCare. Deux arenas
isolées, une par modèle (audio + vision), placées en PSRAM pour économiser
la DRAM interne.

## Quand utiliser

- Charger un nouveau modèle
- Diagnostiquer `AllocateTensors failed`
- Optimiser la taille arena après changement d'archi du modèle

## Principes

### Séparation des arenas

LexaCare utilise `tflm_dual_runtime` avec **deux** `tflite::MicroInterpreter`
indépendants, chacun avec sa propre arena. Ne **jamais** partager une arena
entre deux modèles — en cas d'appels concurrents depuis Core 0 et Core 1, le
scheduler ne garantit rien.

### Placement en PSRAM

```c
// Allocation de l'arena en PSRAM (pas en DRAM interne)
uint8_t *arena = (uint8_t *)heap_caps_malloc(
    arena_size_bytes,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
);
configASSERT(arena != NULL);
```

L'accès PSRAM est plus lent que DRAM (cache miss ~100 ns), mais acceptable :
sur ESP32-S3 avec PSRAM octal @ 80 MHz, une inférence KWS petite tourne en
~1.5 ms, une vision ToF 8×8 en ~30 ms. Si la latence devient critique, possibilité
de déplacer certains tenseurs intermédiaires en DRAM via allocator custom
(niveau avancé, pas pour V1).

## Sizing arena

Deux méthodes combinées.

### Méthode 1 — estimation offline

Dans `tool/exporter/`, utiliser `tensorflow.lite.tools` (Python) ou
`genericbenchmark` pour estimer :

```python
import tensorflow as tf
interp = tf.lite.Interpreter(model_path="audio_kws_int8.tflite")
interp.allocate_tensors()
# Somme des tailles des tensors + overhead
total = sum(t['shape'].prod() * t['dtype'].itemsize for t in interp.get_tensor_details())
print(f"estimated minimum arena: {total} bytes")
```

Ajouter ~30 % d'overhead pour les structures internes TFLM (node list,
tensor arena headers). Arrondir au KB supérieur.

### Méthode 2 — mesure on-device

Après première allocation réussie, loguer :

```cpp
size_t used = interp->arena_used_bytes();
ESP_LOGI(TAG, "arena used = %zu / %zu (%.1f%%)",
         used, arena_size, 100.0f * used / arena_size);
```

Viser **used / total** entre 70 % et 90 %. En dessous : gaspillage DRAM/PSRAM.
Au-dessus : risque que le prochain modèle plus gros ne tienne pas.

## Configuration dans `lexa_config.h`

```c
// firmware/main/lexa_config.h
#pragma once

// Tailles en KB (multiples de 4 KB alignés PSRAM)
#define LEXA_TFLM_ARENA_AUDIO_KB   128
#define LEXA_TFLM_ARENA_VISION_KB   64

// Dérivés
#define LEXA_TFLM_ARENA_AUDIO_BYTES  (LEXA_TFLM_ARENA_AUDIO_KB * 1024)
#define LEXA_TFLM_ARENA_VISION_BYTES (LEXA_TFLM_ARENA_VISION_KB * 1024)
```

## Resolver

Le `MicroMutableOpResolver` doit contenir **exactement** les ops du modèle.
Chaque op non utilisé ajoute ~1 KB de code flash. Chaque op manquant →
`op not registered` au runtime.

Pour lister les ops d'un `.tflite` :

```python
import tensorflow as tf
interp = tf.lite.Interpreter(model_path="audio_kws_int8.tflite")
ops = {op['op_name'] for op in interp._get_ops_details()}
print(sorted(ops))
```

Puis dans `tflm_dual_runtime.cpp` :

```cpp
// Ajouter UNIQUEMENT les ops utilisés
static tflite::MicroMutableOpResolver<10> resolver;
resolver.AddConv2D();
resolver.AddDepthwiseConv2D();
resolver.AddFullyConnected();
resolver.AddMaxPool2D();
resolver.AddReshape();
resolver.AddSoftmax();
resolver.AddQuantize();
resolver.AddDequantize();
// etc.
```

Le template `<10>` est le nombre max d'ops — pas besoin de pile up, mettre un
nombre serré pour éviter la confusion.

## Gotchas

- `AllocateTensors failed` sans message = arena trop petite. Monter par pas de 32 KB jusqu'à réussir, puis monter encore de 16 KB pour la marge.
- Si deux modèles tournent concurremment (Core 0 et Core 1), vérifier que **toutes** les variables globales TFLM sont `thread_local` ou dans des structs séparés. Un bug silencieux classique est de déclarer `static tflite::MicroInterpreter *interp;` partagé.
- PSRAM octal mode requis : `CONFIG_SPIRAM_MODE_OCT=y`. En mode quad, les accès 32-bit non alignés par TFLM ralentissent de 5×.
- Après flashing, la première inférence peut prendre 3–5× plus de temps (caches froids). Mesurer à partir de la 5ᵉ inférence.
