# TinyML pipeline — LexaCare V1

Chaîne end-to-end de la donnée brute au modèle déployé. À lire une fois
pour avoir la carte mentale, puis à consulter par section.

## Vue d'ensemble

```
┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
│ Capture         │ → │ Training        │ → │ Deploy          │
│ (firmware +     │   │ (Python TF)     │   │ (export .h +    │
│  tool collect)  │   │                 │   │  rebuild fw)    │
└─────────────────┘   └─────────────────┘   └─────────────────┘
   ↓                     ↓                     ↓
   .wav, .tof            .tflite Int8          .h + arena tuned
   + metadata            + val accuracy        + resolver synced
```

## Étape 1 : Capture (dataset)

### Principes

- **Firmware en mode capture** : `pio run -e capture_mode -t upload`. Streame
  les données brutes (audio PCM 16-bit ou ToF 8×8 float) sur USB-CDC.
- **Outil PC écoute et stocke** : `lexacare collect <domaine> -l <label> -d <secondes>`.
- Un dataset se construit en plusieurs sessions (une par label, par utilisateur,
  par condition ambiante).

### Organisation des fichiers

```
tool/datasets/
├── audio/
│   ├── aide/
│   │   ├── 2025-11-01_mathieu_silencieux_001.wav
│   │   ├── 2025-11-01_mathieu_silencieux_002.wav
│   │   └── ...
│   ├── _silence/
│   ├── _unknown/          (parole non-mot-clé — très important)
│   └── metadata.jsonl     (un record par fichier : label, durée, source)
└── tof/
    ├── debout/
    ├── chute/
    ├── assis/
    └── metadata.jsonl
```

### Quantités visées

| Domaine | Classes | Échantillons par classe | Total min |
|---------|---------|-------------------------|-----------|
| Audio KWS | 3 (`aide`, `_silence`, `_unknown`) | 500 | 1500 |
| ToF fall | 3–4 (`debout`, `chute`, `assis`, `absent`) | 300 | 1200 |

Pour aller en prod, multiplier par 3–5× avec des enregistrements dans
plusieurs environnements sonores (calme, télé en fond, musique, voix
multiples).

### Pièges capture

- Audio trop peu varié (même pièce, même micro, même voix) → modèle surfit sur
  ce domaine et échoue ailleurs. Diversifier autant que possible.
- Classe `_unknown` souvent sous-échantillonnée → modèle dit "aide" pour
  n'importe quel mot. Enregistrer beaucoup de parole courante sans le
  mot-clé.
- ToF "chute" joué théâtralement → pas la même signature qu'une vraie chute.
  Capturer des chutes "réalistes" (dropdown contrôlé d'un mannequin, par ex).

## Étape 2 : Préprocessing

Identique au runtime — ce qui compte c'est la **symétrie**.

### Audio

```
WAV 16 kHz mono (ou stéréo down-mixé)
    ↓ pre-emphasis coeff=0.97
    ↓ frame 25 ms, hop 10 ms
    ↓ windowing Hann périodique
    ↓ FFT 512
    ↓ |X|²
    ↓ bank mel Slaney (40 filters, 20–8000 Hz)
    ↓ log
    ↓ DCT-II ortho-normalized (13 coeffs)
MFCC matrix [T×13] où T = nombre de frames (typ. 98 pour 1s)
```

Détail : voir `skills/mfcc-validation/SKILL.md`.

### ToF

```
4× VL53L8CX, chacun en résolution 8×8
    ↓ assembler en frame 16×8 (layout dépend du placement PCB)
    ↓ normalisation : distance / 4000 mm → [0, 1]
    ↓ remplacer invalid (status != 5) par 0
    ↓ éventuel filtrage temporel (moyenne glissante 3 frames)
Frame TFLM [16, 8, 1] en Int8 (après quant)
```

## Étape 3 : Training

### Architectures V1

**Audio KWS** (petit ConvNet) :

```
Input [49, 13, 1]  (49 frames ~= 500 ms, 13 MFCC)
  Conv2D(8, (10, 4), stride=(2, 2), activation='relu')
  DepthwiseConv2D((3, 3), stride=(1, 1), activation='relu')
  Conv2D(16, (1, 1), activation='relu')
  MaxPool2D((2, 2))
  Flatten
  Dense(32, 'relu')
  Dense(n_classes, 'softmax')
```

~8k params float, ~8 KB Int8. Inférence ~1.5 ms sur ESP32-S3.

**Vision fall** (encore plus petit) :

```
Input [16, 8, 1]  (frame ToF assemblée)
  Conv2D(4, (3, 3), activation='relu')
  Conv2D(8, (3, 3), activation='relu')
  GlobalAveragePooling2D
  Dense(16, 'relu')
  Dense(n_classes, 'softmax')
```

~1k params, inférence ~0.3 ms.

### Hyperparamètres

```python
# tool/lexacare/config.py
TRAINING = {
    "audio": {
        "batch_size": 64,
        "epochs": 30,
        "lr": 1e-3,
        "optimizer": "adam",
        "augmentation": {
            "noise_snr_db_range": (5, 30),
            "time_shift_ms_range": (-100, 100),
            "gain_db_range": (-6, 6),
        },
    },
    "tof": {
        "batch_size": 32,
        "epochs": 40,
        "lr": 5e-4,
        "optimizer": "adam",
        "augmentation": {
            "dropout_rate": 0.1,  # zeros aléatoires simulant capteur HS
            "noise_mm_std": 20,    # bruit gaussien sur distances
        },
    },
}
```

### Monitoring

TensorBoard local : `tensorboard --logdir tool/runs/`.
Regarder : `train_loss`, `val_loss`, `val_accuracy`, confusion matrix par
epoch (custom callback).

### Critère d'arrêt

- Val accuracy > 92 % et pas d'amélioration sur 5 epochs → early stop
- Gap train/val > 10 % → overfitting, augmenter augmentation ou réduire modèle

## Étape 4 : Quantization + export

Voir `skills/tflite-export/SKILL.md` pour la procédure détaillée.

Livrable final pour le firmware :

```
firmware/main/models/
├── audio_kws_int8.h        données binaires du tflite, alignas(8)
├── audio_kws_labels.h      tableau de labels
├── vision_fall_int8.h
└── vision_fall_labels.h
```

Plus à côté (pas dans firmware, dans tool) :

```
tool/trained/
├── audio_kws_int8.tflite          source
├── audio_kws_int8.metadata.json   commit git, hyperparams, accuracy
├── audio_kws_labels.txt
├── vision_fall_int8.tflite
├── vision_fall_int8.metadata.json
└── vision_fall_labels.txt
```

## Étape 5 : Déploiement

```bash
cd firmware/
pio run -e inference_mode -t upload
pio device monitor
```

Vérifier dans les logs :

- `audio model loaded, arena used=X / Y` (X doit être 70–90 % de Y)
- `vision model loaded, arena used=X / Y`
- Inférence quelques secondes plus tard avec un résultat cohérent

## Étape 6 : Validation terrain

- Jouer un set de test capture (audio + scénarios ToF) en conditions réelles
- Mesurer le taux de faux positifs (FP) et faux négatifs (FN) par classe
- Si FP alerte > 1 par heure en conditions normales → seuil d'orchestrator
  trop bas, ajuster la fenêtre de corrélation ou la confiance min
- Si FN chute détectée > 5 % → réentraîner avec plus d'échantillons chute
  ou revoir l'archi

## Boucle d'itération

```
Déploiement terrain
    ↓ collecte des cas ambigus/ratés (logging enrichi en R&D seulement)
    ↓ relabellisation
    ↓ ajout au dataset
    ↓ réentraînement
    ↓ redéploiement
```

C'est le *active learning* appliqué. À faire toutes les 2 semaines en phase
pilote, puis trimestrel en prod.
