# SKILL : tflite-export

Quantization Int8 full-integer d'un modèle Keras et export en header C
embarquable pour LexaCare.

## Workflow complet

### 1. Dataset représentatif

Critical : la qualité de la quantization dépend du dataset représentatif.
Règles :

- **200 à 500 échantillons** tirés du training set (jamais test/val)
- Distribués sur **toutes les classes** (pas juste le silence)
- Prétraités **exactement comme en inference** (même MFCC, même normalisation)

```python
import numpy as np

def representative_dataset_gen(samples: np.ndarray, n: int = 300):
    idx = np.random.default_rng(42).choice(len(samples), n, replace=False)
    for i in idx:
        x = samples[i].astype(np.float32)
        yield [x[np.newaxis, ...]]  # batch de 1
```

### 2. Conversion TFLite Int8

```python
import tensorflow as tf

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = lambda: representative_dataset_gen(
    x_train, n=300
)
# Full integer : forcer int8 partout, pas de fallback float
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_bytes = converter.convert()
Path("audio_kws_int8.tflite").write_bytes(tflite_bytes)
```

### 3. Validation post-quant

**Toujours** avant d'exporter en `.h` :

```python
# Loader le tflite quantisé
interp = tf.lite.Interpreter(model_content=tflite_bytes)
interp.allocate_tensors()
in_detail = interp.get_input_details()[0]
out_detail = interp.get_output_details()[0]

def infer_quant(x_float):
    # Quantiser l'input selon les paramètres du modèle
    scale, zp = in_detail['quantization']
    x_int8 = np.clip(x_float / scale + zp, -128, 127).astype(np.int8)
    interp.set_tensor(in_detail['index'], x_int8[np.newaxis, ...])
    interp.invoke()
    y_int8 = interp.get_tensor(out_detail['index'])[0]
    scale, zp = out_detail['quantization']
    return (y_int8.astype(np.float32) - zp) * scale

# Evaluer sur test set
acc_quant = evaluate(infer_quant, x_test, y_test)
acc_float = model.evaluate(x_test, y_test)[1]
print(f"float: {acc_float:.3f} / quant: {acc_quant:.3f}")
assert acc_float - acc_quant < 0.03, "quantization drop too high"
```

Si le drop > 3 %, **stop** — diagnostiquer avant export :

- Dataset représentatif trop petit ou pas assez divers
- Classes déséquilibrées → rééchantillonner
- Modèle mal entraîné, généralise mal — réentraîner avec plus de data augmentation

### 4. Export en `.h`

Méthode simple : `xxd -i`.

```bash
xxd -i audio_kws_int8.tflite > audio_kws_int8.h
```

Puis post-processing pour formater proprement (à faire dans `tool/exporter/`) :

```c
// firmware/main/models/audio_kws_int8.h
// Generated from audio_kws_int8.tflite at 2025-11-15T14:32:00Z
// git commit: a3f2c1b
// model: KWS ConvNet, 3 classes (_silence, _unknown, aide)
// input: int8 [1, 49, 13, 1]  (MFCC 49 frames × 13 coeffs)
// output: int8 [1, 3]         (softmax-like scaled)
// arena estimated minimum: 98304 bytes
// ops used: CONV_2D, DEPTHWISE_CONV_2D, FULLY_CONNECTED, MAX_POOL_2D,
//           RESHAPE, SOFTMAX, QUANTIZE, DEQUANTIZE

#pragma once
#include <cstdint>

alignas(8) const unsigned char audio_kws_int8_tflite[] = {
    0x18, 0x00, 0x00, 0x00, /* ... */
};
const unsigned int audio_kws_int8_tflite_len = 98432;
```

Note : `alignas(8)` est **obligatoire** — TFLM fait des accès aligned 8 bytes
sur certains champs, `xxd` ne garantit pas l'alignement sans ça.

### 5. Labels

Générer aussi le `.labels.txt` à côté, une classe par ligne dans l'ordre de
l'output softmax :

```
_silence
_unknown
aide
```

Et dans le firmware, un tableau parallèle :

```c
// firmware/main/models/audio_kws_labels.h
static const char *AUDIO_KWS_LABELS[] = {
    "_silence",
    "_unknown",
    "aide",
};
static const int AUDIO_KWS_N_CLASSES = 3;
```

## Automatisation CLI

Tout ce workflow doit être encapsulé dans :

```bash
lexacare export [--target firmware/main/models/]
```

Sous le capot, appelle `tool/exporter/export.py` qui fait :
1. charge `.tflite` depuis `tool/trained/`
2. valide accuracy post-quant
3. génère `.h` avec header commentaire complet
4. copie dans `--target`
5. écrit metadata.json à côté

## Gotchas

- **Ne jamais** check-in un `.h` sans header commentaire listant les ops utilisés — le prochain dev (ou toi dans 3 mois) doit pouvoir reconstituer le resolver.
- Si `inference_input_type=tf.int8` échoue à la conversion, le modèle a probablement un op non quantisable (Reshape dynamique, Attention custom). Simplifier l'archi.
- Après chaque export, lancer le flow de test sur la cible (voir `agents/tinyml-bridge.md` §Workflow standard export).
- Le `.tflite` ET le `.h` doivent être en git. Conventionnel : `.tflite` dans `tool/trained/` (gros fichier mais utile pour debug), `.h` dans `firmware/main/models/`.
