# SKILL : esp-idf-component

Pattern pour créer un nouveau composant dans `firmware/components/`.

## Quand utiliser

- Encapsuler un driver matériel (nouveau capteur, IC auxiliaire)
- Isoler une brique fonctionnelle testable indépendamment (MFCC, parser protocole, etc.)
- Préparer du code qui pourrait être réutilisé sur un autre projet

## Structure standard

```
firmware/components/my_component/
├── CMakeLists.txt
├── Kconfig                     (optionnel, pour menuconfig)
├── include/
│   └── my_component.h          API publique
├── src/
│   ├── my_component.c
│   └── internal.h              (privé, non exporté)
├── test/                       (optionnel, tests Unity)
│   └── test_my_component.c
└── README.md
```

## `CMakeLists.txt` minimal

```cmake
idf_component_register(
    SRCS "src/my_component.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES driver esp_timer    # dépendances publiques
    PRIV_REQUIRES esp_system     # dépendances privées
)
```

Règle : les `REQUIRES` listent les composants dont les headers apparaissent
dans l'API publique (`include/my_component.h`). Les `PRIV_REQUIRES` sont pour
l'implémentation seulement. Ne pas tout mettre dans `REQUIRES` — ça casse la
résolution cyclique.

## `Kconfig` (si options configurables)

```
menu "LexaCare - My Component"

    config MY_COMPONENT_BUFFER_SIZE
        int "Buffer size (bytes)"
        default 1024
        range 256 16384
        help
            Taille du buffer interne. 1024 suffit pour le cas nominal.

    config MY_COMPONENT_ENABLE_DEBUG_LOG
        bool "Enable verbose debug log"
        default n

endmenu
```

Accessible ensuite via `idf.py menuconfig` sous "LexaCare - My Component", et
dans le code via `CONFIG_MY_COMPONENT_BUFFER_SIZE`.

## API publique (`include/my_component.h`)

```c
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct my_component_handle_s *my_component_handle_t;

typedef struct {
    int gpio_cs;
    uint32_t spi_freq_hz;
} my_component_config_t;

esp_err_t my_component_init(const my_component_config_t *cfg,
                            my_component_handle_t *out_handle);

esp_err_t my_component_read(my_component_handle_t h, uint8_t *buf, size_t len);

esp_err_t my_component_deinit(my_component_handle_t h);

#ifdef __cplusplus
}
#endif
```

Règles :
- Toute fonction publique retourne `esp_err_t` (sauf accesseurs triviaux).
- Handle opaque (`struct *`) — jamais de struct exposé.
- Config par struct, pas par paramètres multiples.
- `extern "C"` pour compatibilité C++ (on peut linker depuis du code .cpp).

## `README.md`

Contenu minimum :
1. Rôle du composant en une phrase
2. Dépendances hardware (GPIO, bus, tensions)
3. Exemple d'utilisation (10 lignes)
4. Limitations connues / TODO

## Gotchas

- Le dossier doit s'appeler **exactement** comme le composant (`my_component`), et c'est ce nom qui apparaît dans les `REQUIRES` d'autres composants.
- Si un composant utilise ESP-DSP ou esp-tflite-micro (managed components), ajouter dans son `CMakeLists.txt` :
  ```cmake
  REQUIRES espressif__esp-dsp espressif__esp-tflite-micro
  ```
- Pour tester en isolation en hôte (hors ESP32), le composant doit compiler sans dépendre de `driver/gpio.h` ou autre — isoler la logique pure dans des fichiers sans include matériel.
