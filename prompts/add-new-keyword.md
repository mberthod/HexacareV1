# PROMPT : ajouter une nouvelle classe au KWS audio

Procédure pas-à-pas pour ajouter un nouveau mot-clé (ex : ajouter `urgence`
en plus de `aide`). Chaque étape est atomique — une seule peut être déléguée
à Gemma 4B à la fois.

## Prérequis

- Pipeline audio fonctionnel (`capture_mode` et `inference_mode` buildent et tournent)
- Modèle audio actuel en prod avec ses classes connues
- Dataset existant conservé dans `tool/datasets/audio/`

## Étapes

### 1. Collecter le dataset de la nouvelle classe

En capture_mode, micro brancé, dans un environnement sonore varié :

```bash
cd tool && source .venv/bin/activate
lexacare collect audio -l urgence -d 60 -p /dev/ttyACM0
```

Viser 500 échantillons minimum. Répéter dans plusieurs conditions :

- calme total
- avec TV/radio en fond
- plusieurs voix différentes (demander à des proches d'enregistrer)
- distance micro variable (30 cm à 3 m)

Vérifier la qualité :

```bash
ls tool/datasets/audio/urgence/ | wc -l    # doit être proche du nb attendu
python -m tools.check_dataset_quality urgence
```

### 2. Mettre à jour la liste des classes

Fichier : `tool/lexacare/config.py`

```python
# AVANT
AUDIO_LABELS = ["_silence", "_unknown", "aide"]

# APRÈS
AUDIO_LABELS = ["_silence", "_unknown", "aide", "urgence"]
```

**Attention** : l'ordre est fixé par la position dans cette liste. La classe
index 3 = `urgence`. Ne pas réordonner les classes existantes (casserait les
modèles précédents).

### 3. Retrainer le modèle

```bash
lexacare train audio --epochs 30 --seed 42
```

Vérifier la convergence dans TensorBoard :

```bash
tensorboard --logdir tool/runs/
# Ouvrir la dernière run, regarder val_accuracy et confusion matrix
```

Critères de validation :

- `val_accuracy > 0.92`
- pas de classe complètement confondue avec une autre dans la matrice de confusion
- `_unknown` bien séparé de `urgence` (sinon ajouter des samples dans `_unknown`)

### 4. Quantiser et évaluer

Le script `lexacare train` fait déjà la quantization et logue l'accuracy post-quant.
Vérifier :

```
float accuracy: 0.947
quant accuracy: 0.931  (drop = 1.6%, OK)
```

Si drop > 3 % : regrossir le dataset représentatif, réentraîner.

### 5. Mettre à jour le resolver TFLM si nouveau op

Lister les ops du nouveau modèle :

```bash
python -c "
import tensorflow as tf
interp = tf.lite.Interpreter(model_path='tool/trained/audio_kws_int8.tflite')
print(sorted({op['op_name'] for op in interp._get_ops_details()}))
"
```

Comparer à la liste dans `firmware/components/tflm_dual_runtime/tflm_dual_runtime.cpp`.
Si un op apparaît qui n'était pas là avant :

- Ajouter `resolver.AddXxx()` dans `tflm_dual_runtime.cpp`
- Incrémenter le template `<N>` du `MicroMutableOpResolver`
- Rebuild

### 6. Exporter

```bash
lexacare export --target firmware/main/models/
```

Vérifier le header du `.h` généré :

```c
// Generated from audio_kws_int8.tflite at 2025-11-15T14:32:00Z
// input: int8 [1, 49, 13, 1]
// output: int8 [1, 4]          <-- 4 classes maintenant
// labels: _silence, _unknown, aide, urgence
// arena estimated minimum: 98560 bytes
```

### 7. Mettre à jour `audio_kws_labels.h`

Fichier : `firmware/main/models/audio_kws_labels.h`

```c
static const char *AUDIO_KWS_LABELS[] = {
    "_silence",
    "_unknown",
    "aide",
    "urgence",    // nouveau
};
static const int AUDIO_KWS_N_CLASSES = 4;
```

Normalement `lexacare export` regénère ce fichier automatiquement. Vérifier.

### 8. Mettre à jour l'orchestrator

Fichier : `firmware/main/orchestrator.c`

Si l'orchestrator a une logique qui checke un label spécifique (ex : uniquement
alerter sur `aide`), décider si `urgence` doit déclencher la même alerte :

```c
// AVANT
if (audio_label == AUDIO_LABEL_AIDE && vision_fall_detected) { trigger_alert(); }

// APRÈS
bool keyword_match = (audio_label == AUDIO_LABEL_AIDE
                   || audio_label == AUDIO_LABEL_URGENCE);
if (keyword_match && vision_fall_detected) { trigger_alert(); }
```

Définir les constantes d'index dans un header partagé :

```c
// firmware/main/app_defs.h
enum {
    AUDIO_LABEL_SILENCE = 0,
    AUDIO_LABEL_UNKNOWN = 1,
    AUDIO_LABEL_AIDE    = 2,
    AUDIO_LABEL_URGENCE = 3,
};
```

### 9. Build et test

```bash
cd firmware
pio run -e inference_mode -t upload
pio device monitor
```

Vérifier les logs :

```
I (xxx) tflm_dual: audio model loaded, arena used=99840 / 131072
```

Tester les 4 classes en les prononçant :

- silence ambiant → `label=0 conf=0.9+`
- parler normalement → `label=1 conf=0.7+`
- dire "aide" → `label=2 conf=0.8+`
- dire "urgence" → `label=3 conf=0.8+`

### 10. Documentation et commit

Update `knowledge/tinyml-pipeline.md` si la liste des classes y était référencée.

Commit atomique :

```bash
git add tool/lexacare/config.py tool/trained/audio_kws_int8.* \
        firmware/main/models/audio_kws_int8.h \
        firmware/main/models/audio_kws_labels.h \
        firmware/main/orchestrator.c firmware/main/app_defs.h
git commit -m "feat: add 'urgence' keyword to audio KWS (4 classes total)"
```

## Points de vigilance

- **Ne jamais** retirer `_silence` ou `_unknown` — ce sont les garde-fous qui évitent les faux positifs permanents.
- Si `urgence` et `aide` sont phonétiquement trop proches, le modèle va confondre. Soit accepter la confusion (les deux déclenchent la même alerte → peu grave), soit choisir un mot plus différent phonétiquement.
- Pour tester en condition réelle, enregistrer une session de 10 min de vie normale (parole, télé, bruits ambiants) et compter les faux positifs par heure. Seuil de tolérance : < 1 FP par heure. Si pire, durcir le seuil de confiance dans l'orchestrator.
