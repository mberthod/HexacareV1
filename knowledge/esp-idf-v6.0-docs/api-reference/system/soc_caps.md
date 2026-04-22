<!-- Source: _sources/api-reference/system/soc_caps.rst.txt (ESP-IDF v6.0 documentation) -->

# SoC Capability Macros

Different models of ESP chips integrate various hardware modules. Even the same type of module may have subtle differences across different chips. ESP-IDF provides a small "database" to describe the differences between chips (please note, only differences are described, not commonalities). The contents of this "database" are defined as macros in the **soc/soc_caps.h** file, referred to as **SoC capability macros**. Users can utilize these macros in their code with conditional compilation directives (such as `#if`) to control which code is actually compiled.

> **Note**
>
> ## Using SoC Capability Macros

We recommend accessing SoC capability macros indirectly through the following macro functions:

| Macro Function | Description                                                  | Example                                                |
|----------------|--------------------------------------------------------------|--------------------------------------------------------|
| `SOC_IS`       | Determines the chip model                                    | `#if SOC_IS(ESP32)` checks if the chip is ESP32        |
| `SOC_HAS`      | Checks if the chip has a specific hardware module or feature | `#if SOC_HAS(DAC)` checks if the chip has a DAC module |

## API Reference

inc/soc_caps.inc

inc/soc_caps_eval.inc

