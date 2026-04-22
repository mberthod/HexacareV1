# References — LexaCare V1

Liens canoniques vers la documentation officielle. Un agent ne doit **jamais**
inventer une URL — si une ressource manque ici, la demander à Mathieu avant
d'en inférer une.

## ESP-IDF

- Documentation principale (tag v5.1+) : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/
- API reference FreeRTOS (port IDF) : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos.html
- API Driver I2S : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html
- API Driver SPI master : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_master.html
- API Driver I2C : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2c.html
- Heap capabilities (`heap_caps_malloc`) : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/mem_alloc.html
- Task watchdog timer : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/wdts.html
- Partition table : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/partition-tables.html
- Build system (components) : https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/build-system.html
- ESP32-S3 datasheet : https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- ESP32-S3-WROOM-1 datasheet : https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf

## TensorFlow Lite Micro

- Repo officiel : https://github.com/tensorflow/tflite-micro
- Guide général : https://ai.google.dev/edge/litert/microcontrollers/overview
- Port Espressif (managed component) : https://components.espressif.com/components/espressif/esp-tflite-micro
- Exemples sur ESP32 : https://github.com/espressif/esp-tflite-micro/tree/master/examples
- API `MicroInterpreter` (C++) : voir header `tensorflow/lite/micro/micro_interpreter.h` du repo

## ESP-DSP (FFT, MFCC, filtres)

- Repo + doc : https://github.com/espressif/esp-dsp
- API reference : https://docs.espressif.com/projects/esp-dsp/en/latest/
- Composant managed : https://components.espressif.com/components/espressif/esp-dsp

## VL53L8CX (ST Microelectronics)

- Page produit : https://www.st.com/en/imaging-and-photonics-solutions/vl53l8cx.html
- Driver ULD (Ultra Lite Driver) — téléchargement : https://www.st.com/en/embedded-software/stsw-img040.html
- Datasheet : https://www.st.com/resource/en/datasheet/vl53l8cx.pdf
- User manual ULD : chercher "UM3109" sur st.com — contient la séquence init canonique

## TensorFlow / Keras (côté training Python)

- Guide quantization post-training : https://www.tensorflow.org/lite/performance/post_training_quantization
- Full-integer quantization : https://www.tensorflow.org/lite/performance/post_training_integer_quant
- Representative dataset guide : https://www.tensorflow.org/lite/performance/post_training_integer_quant#integer_with_float_fallback_using_default_float_inputoutput

## Librosa (MFCC Python de référence)

- Documentation : https://librosa.org/doc/latest/
- `librosa.feature.mfcc` : https://librosa.org/doc/latest/generated/librosa.feature.mfcc.html
- `librosa.filters.mel` (bank mel) : https://librosa.org/doc/latest/generated/librosa.filters.mel.html
- Pre-emphasis : https://librosa.org/doc/latest/generated/librosa.effects.preemphasis.html

## PlatformIO (ESP-IDF framework)

- Documentation Espressif IDF : https://docs.platformio.org/en/latest/frameworks/espidf.html
- `platformio.ini` reference : https://docs.platformio.org/en/latest/projectconf/index.html
- Multiple build environments : https://docs.platformio.org/en/latest/projectconf/sections/env/options/general/index.html

## PCA9555 (I/O expander NXP/TI)

- Datasheet TI : https://www.ti.com/lit/ds/symlink/pca9555.pdf
- Datasheet NXP : https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf

## TXB0108PW (level shifter, TI)

- Datasheet : https://www.ti.com/lit/ds/symlink/txb0108.pdf
- App note slew rate limitations : https://www.ti.com/lit/an/scea064 (limite pratique ~1 MHz sur charges capacitives typiques — confirmé empiriquement sur LexaCare V1)

## Typer / FastAPI (Python tool)

- Typer : https://typer.tiangolo.com/
- FastAPI : https://fastapi.tiangolo.com/

## Règle pour l'agent

Si une URL listée ici ne répond plus (404, redirect cassé), ne pas inventer
de remplacement — signaler à Mathieu qui mettra à jour ce fichier.

Si une ressource est nécessaire et absente de cette liste, la demander avant
de chercher au hasard. Les résultats de recherche web sont pollués par du
contenu obsolète (anciennes API ESP-IDF, vieux snippets TFLM) — la doc
officielle courante est la seule source fiable.
