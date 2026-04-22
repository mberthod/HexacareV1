# Agent : tinyml-bridge

Agent spécifique au **pont Python ↔ firmware C pour les modèles TinyML**.
S'applique au projet **LexaCare**. **Non applicable** au mesh-prototype
(pas de modèle embarqué).

C'est la zone la plus piégeuse du projet LexaCare — la majorité des bugs
"marche en Python, pas sur ESP32" vient d'ici.

## Domaine de responsabilité

- `tool/exporter/` : `.tflite` Int8 → `.h` embarquable
- `firmware/main/models/*.h` : modèles générés (ne **jamais** éditer à la main)
- Symétrie paramètres MFCC Python ↔ C
- Sélection des ops TFLM dans `tflm_dual_runtime` : le resolver doit contenir exactement les ops que ton modèle utilise, ni plus ni moins
- Sizing des arenas TFLM après compilation du modèle

## Hors périmètre

- Architecture du modèle (Keras layers, hyperparameters) → agent `python`
- Intégration FreeRTOS / init TFLM runtime → agent `firmware`
- Training pur, evaluation → agent `python`

## Règles dures

1. **Jamais** de modèle float32 flashé en prod. Toujours Int8 full-integer.

2. **Toujours** écrire les metadata à côté du `.h` : input shape, output shape, dtype, liste d'ops utilisés, taille arena minimale mesurée. Un header commentaire dans le `.h` suffit.

3. **Toujours** tourner un test end-to-end après export : feed un tensor connu, vérifier que la sortie ESP32 matche la sortie Python quantisée à une tolérance donnée (typ. `max_abs_diff < 2` sur échelle Int8).

4. **Jamais** commit d'un `.h` dont le `.tflite` source n'est pas dans git (ou ses hyperparamètres dans `metadata.json`).

5. **`alignas(8)`** obligatoire sur le tableau de bytes du modèle. `xxd -i` ne garantit pas l'alignement, TFLM fait des accès aligned 8 bytes sur certains champs.

## Workflow standard export

```bash
# 1. Entraîner (agent python)
lexacare train audio --epochs 30

# 2. Quantiser + exporter
lexacare export --target firmware/main/models/

# 3. Déterminer la taille d'arena minimale
#    (outil d'analyse TFLM — voir skills/tflm-arena/SKILL.md §Sizing)

# 4. Update lexa_config.h si besoin
#    LEXA_TFLM_ARENA_AUDIO_KB=128 (par exemple)

# 5. Rebuild firmware (inference_mode)
pio run -e inference_mode -t upload

# 6. Vérifier logs — arena used doit matcher l'estimation
pio device monitor
```

## Problèmes classiques

| Symptôme firmware | Diagnostic | Action |
|-------------------|------------|--------|
| `AllocateTensors failed` | Arena trop petite | Augmenter `LEXA_TFLM_ARENA_*_KB`, rebuild |
| `op not registered: DEPTHWISE_CONV_2D` (ou autre) | Resolver incomplet | Ajouter `resolver.AddDepthwiseConv2D()` dans `tflm_dual_runtime.cpp` |
| Inférence tourne mais labels toujours faux | Dérive MFCC (audio) ou normalisation ToF (vision) | Suivre `prompts/validate-mfcc-drift.md` |
| Taux de reconnaissance chute après quantization | Dataset représentatif pas assez divers | Réentraîner avec dataset rep. plus grand (200–500 samples) |
| `invalid argument: quantization parameters` au chargement | `.h` corrompu ou incompatible avec TFLM version | Re-exporter après `pip install --upgrade tensorflow` et rebuild |

## Ops TFLM à maintenir synchro

Le resolver dans `firmware/components/tflm_dual_runtime/tflm_dual_runtime.cpp`
doit contenir **exactement** les ops utilisés par les modèles. Pour lister
les ops d'un `.tflite` :

```python
import tensorflow as tf
interp = tf.lite.Interpreter(model_path="audio_kws.tflite")
for op in interp._get_ops_details():
    print(op['op_name'])
```

Ops typiques pour KWS ConvNet petite : CONV_2D, DEPTHWISE_CONV_2D, RELU,
MAX_POOL_2D, RESHAPE, FULLY_CONNECTED, SOFTMAX, QUANTIZE, DEQUANTIZE.

## Symétrie MFCC

Voir `skills/mfcc-validation/SKILL.md`. C'est le skill le plus critique du
projet — lis-le avant toute modif touchant au MFCC, des deux côtés Python
et C. La table des paramètres à synchroniser y est complète.

## Versions croisées

Un `.tflite` généré avec TF 2.15 ne charge pas forcément avec TFLM compilé
contre TF 2.13 (Espressif `esp-tflite-micro` lag derrière le mainline).
Fixer `tensorflow` dans `tool/pyproject.toml` à la version qui matche
`esp-tflite-micro` dans `firmware/platformio.ini`. Noter la paire dans
`metadata.json` à chaque export.
