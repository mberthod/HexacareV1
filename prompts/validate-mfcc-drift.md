# PROMPT : détecter et corriger la dérive MFCC Python ↔ C

Procédure opérationnelle. Pour la théorie et la table des paramètres à
synchroniser, voir `skills/mfcc-validation/SKILL.md`.

## Quand lancer ce test

- Après tout changement dans `firmware/components/mfcc_dsp/`
- Après tout changement dans `tool/trainer/mfcc.py` (ou équivalent)
- Après tout changement dans `tool/lexacare/config.py` touchant aux paramètres audio
- **Si symptôme : modèle à 95 %+ accuracy en Python mais taux de reco catastrophique sur ESP32**, alors que le hardware audio fonctionne (les captures brutes sont saines, tu entends bien les `.wav`)
- En routine : avant chaque release firmware

## Étape 1 : générer l'input de référence

Une fois (commiter le fichier) :

```bash
cd tool && source .venv/bin/activate
python -m tests.generate_golden_input
# → tool/tests/data/golden_input.npy (float32, 16000 samples = 1 sec)
# → tool/tests/data/golden_input.s16 (int16, binaire brut, pour le firmware)
```

## Étape 2 : calculer le MFCC Python de référence

```bash
python -m tests.compute_golden_mfcc
# → tool/tests/data/golden_mfcc.npy (float32, shape [13, ~62])
# Affiche : shape, min, max, mean
```

À commiter aussi — c'est la vérité terrain. Si quelqu'un modifie les
paramètres MFCC Python, cette ref doit être régénérée ET la validation
ré-effectuée sur firmware.

## Étape 3 : build firmware en mode `debug_mfcc`

Le build a un 3ᵉ environment PlatformIO dédié. Si absent, il faut le créer
(voir §Créer le debug_mfcc_mode plus bas).

```bash
cd firmware
pio run -e debug_mfcc_mode -t upload
```

Au boot, ce firmware :

1. Lit `golden_input.s16` depuis un blob `.h` embarqué (généré à partir de `tool/tests/data/golden_input.s16` via `xxd -i`)
2. Applique la pré-emphase
3. Calcule le MFCC via `mfcc_dsp/`
4. Dumpe le tableau flat sur série, framé : `<MFCC_START>` puis floats binaires little-endian, puis `<MFCC_END>`
5. Quitte (LED fixe, pas de boucle)

## Étape 4 : capturer et comparer

```bash
cd tool
lexacare validate-mfcc -p /dev/ttyACM0
```

La commande :

1. Reset l'ESP32 (via DTR/RTS)
2. Lit le flux jusqu'au marker `<MFCC_START>`
3. Lit jusqu'au marker `<MFCC_END>`
4. Parse en `np.float32`, reshape
5. Compare au `golden_mfcc.npy`
6. Affiche et retourne code 0 si OK, 1 sinon

Output attendu :

```
=== MFCC validation ===
ESP32 MFCC shape: (13, 62)
Golden MFCC shape: (13, 62)

max abs error:  4.231e-05
mean abs error: 9.874e-06
correlation:    0.999998

VERDICT: PASS (max_abs_error < 1e-3)
```

## Étape 5 : interpréter le résultat

| max_abs_err | Action |
|-------------|--------|
| < 1e-4 | Parfait, rien à faire. |
| 1e-4 à 1e-3 | Acceptable, commit le changement. |
| 1e-3 à 1e-2 | Investiguer avant commit. Probable désync sur un paramètre mineur (type de fenêtre sym/periodic, arrondi intermédiaire). Voir checklist §Étape 6. |
| > 1e-2 | Bloquant. Un paramètre majeur diverge. Ne pas flasher en prod. Diagnostic obligatoire. |

## Étape 6 : checklist diagnostic quand dérive > 1e-3

Dans l'ordre, vérifier :

### 6.1 — Pré-emphase

Python :

```python
sig = librosa.effects.preemphasis(sig, coef=0.97)
# équivaut à : y[n] = x[n] - 0.97 * x[n-1] avec x[-1] = 0
```

C : vérifier dans `mfcc_dsp/preemphasis.c` :

```c
out[0] = in[0];  // pas - 0.97 * in[-1], car in[-1] = 0
for (int n = 1; n < N; n++) {
    out[n] = in[n] - 0.97f * in[n-1];
}
```

Piège classique : utiliser `0.95f` en dur en C alors que Python utilise `0.97`.

### 6.2 — Fenêtre

Python (librosa par défaut) : Hann **périodique** (`sym=False`, équivalent
`fftbins=True`).

C : dans `mfcc_dsp/window.c` ou équivalent, vérifier la formule :

```c
// Périodique (bon)
w[n] = 0.5f * (1.0f - cosf(2.0f * M_PI * n / N));

// Symétrique (FAUX pour notre cas)
w[n] = 0.5f * (1.0f - cosf(2.0f * M_PI * n / (N - 1)));
```

Différence : le dénominateur `N` vs `N-1`. Pour N=512, différence ~0.2 %.
Se propage dans la FFT et s'amplifie.

### 6.3 — Bank mel : formule de warping

Python par défaut = Slaney (`htk=False`).

Formule Slaney :

```
f_sp = 200 / 3
min_log_hz = 1000
min_log_mel = min_log_hz / f_sp
logstep = log(6.4) / 27.0
mel(f) = f / f_sp                       si f < min_log_hz
mel(f) = min_log_mel + log(f/min_log_hz) / logstep  sinon
```

Formule HTK :

```
mel(f) = 2595 * log10(1 + f / 700)
```

Vérifier que `mfcc_dsp/mel_filterbank.c` implémente Slaney, pas HTK.

Alternative : basculer Python en HTK (`htk=True` dans `librosa.feature.mfcc`),
plus facile à coder en C. Mais alors il faut regénérer `golden_mfcc.npy` ET
retrainer les modèles (ils ne seront plus compatibles).

### 6.4 — DCT et normalisation

Python (librosa par défaut) : `dct_type=2`, `norm='ortho'`.

Formule DCT-II ortho-normalisée :

```
X[k] = alpha[k] * sum(x[n] * cos(pi * (2n+1) * k / (2N)), n=0..N-1)
où alpha[0] = sqrt(1/N), alpha[k>0] = sqrt(2/N)
```

Vérifier dans `mfcc_dsp/dct.c` que `alpha` est bien appliqué. Oubli fréquent :
ne pas appliquer `alpha` du tout → tous les MFCC multipliés par un facteur
`sqrt(N)` constant → modèle sort n'importe quoi.

### 6.5 — Ordre des opérations FFT

Sur la magnitude après FFT :

- Python : `|FFT|²` (power spectrum), puis filterbank
- Certaines impls C : `|FFT|` (magnitude seule), puis filterbank → décalage
  logarithmique

Vérifier qu'on compare des power spectra des deux côtés.

### 6.6 — Log

- Python : `np.log` (népérien) ou `np.log10` selon config ; librosa MFCC
  utilise `np.log` (natural log) sur le spectrogramme mel puis DCT.
- C : utiliser `logf()` (natural log), pas `log10f()`.

Si C utilise `log10f()`, tous les MFCC sont divisés par `ln(10) ≈ 2.303` →
échelle différente mais ratios préservés → partial match mais max_err ~3e-1.

## Créer le debug_mfcc_mode (si absent)

Dans `firmware/platformio.ini`, ajouter un environment :

```ini
[env:debug_mfcc_mode]
extends = env:base
build_flags =
    ${env:base.build_flags}
    -DLEXA_MODE_DEBUG_MFCC=1
build_src_filter = +<main/app_main_debug_mfcc.c>
```

Créer `firmware/main/app_main_debug_mfcc.c` qui :

1. Include le blob `golden_input.h` (généré par `xxd -i -n lexa_golden_input tool/tests/data/golden_input.s16 > firmware/main/golden_input.h`)
2. Appelle `preemphasis()` puis `mfcc_compute()` de `mfcc_dsp/`
3. Dumpe sur série avec markers
4. `while(1) vTaskDelay(pdMS_TO_TICKS(1000))` pour rester en vie

Exemple squelette :

```c
#include "mfcc_dsp.h"
#include "golden_input.h"    // lexa_golden_input[16000*2] = 16000 int16

void app_main(void) {
    ESP_LOGI(TAG, "debug_mfcc_mode start");

    // Convertir int16 → float32 normalisé [-1, 1]
    float *sig_f32 = heap_caps_malloc(16000 * sizeof(float), MALLOC_CAP_SPIRAM);
    configASSERT(sig_f32);
    const int16_t *src = (const int16_t *)lexa_golden_input;
    for (int i = 0; i < 16000; i++) sig_f32[i] = src[i] / 32768.0f;

    // Pré-emphase in place
    preemphasis(sig_f32, 16000, 0.97f);

    // MFCC
    float mfcc[13 * 62];    // ajuster selon n_frames réel
    mfcc_compute(sig_f32, 16000, mfcc);

    // Dump série avec markers
    printf("<MFCC_START>\n");
    fwrite(mfcc, sizeof(float), 13 * 62, stdout);
    fflush(stdout);
    printf("\n<MFCC_END>\n");

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
```

## Après correction

Une fois la dérive passée sous 1e-3 :

1. Commit les deux côtés ensemble (`mfcc_dsp/` + `tool/trainer/mfcc.py`)
2. Regénérer `golden_mfcc.npy`
3. Relancer le test une fois pour confirmer
4. **Retrainer** les modèles existants (les anciens `.tflite` ont été entraînés sur les anciens MFCC — s'ils étaient déjà à 95 % en Python c'est qu'ils n'ont pas besoin de changer, mais s'il y avait dérive, c'est qu'ils étaient entraînés sur le BON MFCC Python et déployés sur le MAUVAIS MFCC C → rien à retrainer, juste fix le C)
5. Redéployer

## Journal

Conseil : maintenir un petit fichier `tool/tests/mfcc_validation_log.md` avec
une ligne par exécution (date, max_err, version git) pour traquer la stabilité
dans le temps.
