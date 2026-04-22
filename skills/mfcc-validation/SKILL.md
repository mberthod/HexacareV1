# SKILL : mfcc-validation

**LE skill le plus critique du projet LexaCare.** 90 % des bugs "marche en
Python mais pas sur ESP32" viennent d'une dérive entre le MFCC Python
(training) et le MFCC C (inférence). Ce skill décrit la procédure pour
garantir qu'ils sont bit-équivalents à tolérance près.

## Pourquoi c'est critique

Le modèle KWS apprend sur des vecteurs MFCC produits par `librosa` en Python.
À l'inférence, ESP32 produit des MFCC via `mfcc_dsp/` (ESP-DSP + code C
custom). Si les deux implémentations divergent (fenêtre différente, bank mel
différent, arrondi différent), le modèle voit des entrées hors distribution
et sort n'importe quoi — même si l'accuracy Python est à 98 %.

## Paramètres à synchroniser

Tous ces paramètres **doivent** être strictement identiques entre Python et C :

| Paramètre | Valeur LexaCare V1 | Python (`librosa`) | C (`mfcc_dsp/`) |
|-----------|-------------------|--------------------|-----------------|
| Sample rate | 16000 Hz | `sr=16000` | `LEXA_MFCC_SAMPLE_RATE` |
| FFT size | 512 | `n_fft=512` | `LEXA_MFCC_N_FFT` |
| Hop length | 256 | `hop_length=256` | `LEXA_MFCC_HOP` |
| Window | Hann | `window='hann'` | précalculé, table Hann |
| Mel bins | 40 | `n_mels=40` | `LEXA_MFCC_N_MEL` |
| MFCC count | 13 | `n_mfcc=13` | `LEXA_MFCC_N_COEFF` |
| Freq min | 20 Hz | `fmin=20` | `LEXA_MFCC_FMIN_HZ` |
| Freq max | 8000 Hz | `fmax=8000` | `LEXA_MFCC_FMAX_HZ` |
| Pre-emphasis | 0.97 | `librosa.effects.preemphasis(..., coef=0.97)` | `LEXA_MFCC_PREEMPH` |
| DCT type | II (ortho) | `dct_type=2, norm='ortho'` | DCT-II table précalc |

**Tout changement d'un de ces paramètres doit être fait simultanément dans les
deux côtés ET suivi de la validation ci-dessous.**

## Procédure de validation

### 1. Générer un input de test reproductible

```python
# tool/tests/golden_input.py
import numpy as np
np.random.seed(42)
# 1 seconde de bruit rose band-limité (plus représentatif que du bruit blanc)
sig = np.random.randn(16000).astype(np.float32) * 0.1
np.save("tool/tests/golden_input.npy", sig)
sig.astype(np.int16).tofile("tool/tests/golden_input.s16")  # pour ESP32
```

### 2. Calculer le MFCC Python de référence

```python
# tool/tests/compute_golden_mfcc.py
import librosa, numpy as np
sig = np.load("golden_input.npy")
sig = librosa.effects.preemphasis(sig, coef=0.97)
mfcc = librosa.feature.mfcc(
    y=sig, sr=16000, n_mfcc=13, n_fft=512, hop_length=256,
    n_mels=40, fmin=20, fmax=8000, dct_type=2, norm='ortho'
)
np.save("golden_mfcc.npy", mfcc)
print(f"shape: {mfcc.shape}, range: [{mfcc.min():.3f}, {mfcc.max():.3f}]")
```

### 3. Ajouter un debug-mfcc au firmware

Dans `firmware/`, un 3ᵉ environment PlatformIO `debug_mfcc_mode` qui :

1. lit `golden_input.s16` depuis SPIFFS (ou hardcodé dans un `.h`)
2. applique la même pré-emphase
3. calcule le MFCC via `mfcc_dsp/`
4. dumpe le résultat binaire sur série
5. quitte (pas d'infini loop)

### 4. Commande `lexacare validate-mfcc`

```bash
lexacare validate-mfcc -p /dev/ttyACM0
```

Attend le dump série, parse en float32, compare à `golden_mfcc.npy` :

```python
max_abs_err = np.abs(esp_mfcc - golden_mfcc).max()
mean_abs_err = np.abs(esp_mfcc - golden_mfcc).mean()
print(f"max abs err: {max_abs_err:.6f}")
print(f"mean abs err: {mean_abs_err:.6f}")
assert max_abs_err < 1e-3, "MFCC drift trop grande — investiguer"
```

## Interpréter les résultats

| `max_abs_err` | Diagnostic |
|---------------|------------|
| < 1e-4 | Parfait. Implémentations bit-équivalentes (à l'arrondi float près). |
| 1e-4 à 1e-3 | Acceptable. Différences numériques normales (ordre des sommations, etc.). |
| 1e-3 à 1e-2 | Suspect. Vérifier pré-emphase et fenêtrage. Souvent = type de fenêtre différent (Hann symétrique vs periodic). |
| 1e-2 à 1e-1 | Grave. Un paramètre majeur diverge. Vérifier `n_fft`, bank mel (warping), formule DCT. |
| > 1e-1 | Implémentation totalement différente. Repartir du pseudo-code librosa et réimplémenter pas-à-pas. |

## Points les plus subtils

### Fenêtre de Hann : symétrique vs périodique

`librosa.filters.get_window('hann', N)` est **symétrique** par défaut
(`sym=True`), mais pour le traitement de signal continu on veut **périodique**
(`sym=False`). Vérifier quelle variante est dans `mfcc_dsp/window.c` et
matcher.

```python
# librosa utilise scipy.signal.get_window en interne
from scipy.signal import get_window
w_sym = get_window('hann', 512, fftbins=False)  # symétrique
w_per = get_window('hann', 512, fftbins=True)   # périodique
# librosa.feature.mfcc utilise fftbins=True (périodique) par défaut
```

### Bank mel : Slaney vs HTK

Deux formules de warping mel existent :

- **Slaney** : ce que librosa utilise par défaut (`htk=False`)
- **HTK** : ce que pas mal d'implémentations C font par défaut (formule plus ancienne)

Les deux donnent des résultats différents de ~2 %. S'assurer que C utilise
Slaney. Ou basculer Python en HTK : `librosa.feature.mfcc(..., htk=True)`.

### DCT : ortho-normalization

`librosa.feature.mfcc` applique `norm='ortho'` par défaut sur la DCT. La
formule C doit inclure les facteurs `sqrt(1/N)` pour le premier coefficient
et `sqrt(2/N)` pour les autres. Oublier ça divise les MFCC par un facteur
constant et casse la quantization.

## Quand refaire la validation

- À chaque merge dans `mfcc_dsp/` ou `tool/trainer/mfcc.py`
- À chaque changement de `lexa_config.h` touchant aux paramètres MFCC
- Avant chaque release firmware destinée à tester un nouveau modèle
- **Jamais** skip ce test "parce que le dernier run était bon"
