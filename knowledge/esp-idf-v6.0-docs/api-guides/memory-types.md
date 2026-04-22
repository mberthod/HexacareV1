<!-- Source: _sources/api-guides/memory-types.rst.txt (ESP-IDF v6.0 documentation) -->

# Memory Types

{IDF_TARGET_NAME} chip has multiple memory types and flexible memory mapping features. This section describes how ESP-IDF uses these features by default.

ESP-IDF distinguishes between instruction memory bus (IRAM, IROM, RTC FAST memory) and data memory bus (DRAM, DROM). Instruction memory is executable, and can only be read or written via 4-byte aligned words. Data memory is not executable and can be accessed via individual byte operations. For more information about the different memory buses consult the *{IDF_TARGET_NAME} Technical Reference Manual* \> *System and Memory* \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#sysmem)\].

## DRAM (Data RAM)

Non-constant static data (.data) and zero-initialized data (.bss) is placed by the linker into Internal SRAM as data memory. The remaining space in this region is used for the runtime heap.

SOC_SPIRAM_SUPPORTED

By applying the `EXT_RAM_BSS_ATTR` macro, zero-initialized data can also be placed into external RAM. To use this macro, the `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` needs to be enabled. See `external_ram_config_bss`.

<!-- Only for: esp32 -->
The available size of the internal DRAM region is reduced by 64 KB (by shifting start address to `0x3FFC0000`) if Bluetooth stack is used. Length of this region is also reduced by 16 KB or 32 KB if trace memory is used. Due to some memory fragmentation issues caused by ROM, it is also not possible to use all available DRAM for static allocations - however the remaining DRAM is still available as heap at runtime.

> **Note**
>
> <!-- Only for: not esp32 -->
> **Note**
>
> Constant data may also be placed into DRAM, for example if it is used in an non-flash-safe ISR (see explanation under `how-to-place-code-in-iram`).

### "noinit" DRAM

The macro `__NOINIT_ATTR` can be used as attribute to place data into `.noinit` section. The values placed into this section will not be initialized at startup and should keep its value after software restart.

SOC_SPIRAM_SUPPORTED

By applying the `EXT_RAM_NOINIT_ATTR` macro, non-initialized value could also be placed in external RAM. To do this, the `CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY` needs to be enabled. See `external_ram_config_noinit`. If the `CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY` is not enabled, `EXT_RAM_NOINIT_ATTR` will behave just as `__NOINIT_ATTR`, it will make data to be placed into `.noinit` segment in internal RAM.

Example:

    __NOINIT_ATTR uint32_t noinit_data;

## IRAM (Instruction RAM)

<!-- Only for: esp32 -->
ESP-IDF allocates part of the Internal SRAM0 region for instruction RAM. The region is defined in *{IDF_TARGET_NAME} Technical Reference Manual* \> *System and Memory* \> *Embedded Memory* \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#sysmem)\]. Except for the first 64 KB block which is used for PRO and APP MMU caches, the rest of this memory range (i.e., from `0x40080000` to `0x400A0000`) is used to store parts of the application which need to run from RAM.

<!-- Only for: esp32s2 -->
ESP-IDF allocates part of the Internal SRAM region for instruction RAM. The region is defined in *{IDF_TARGET_NAME} Technical Reference Manual* \> *System and Memory* \> *Internal Memory* \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#sysmem)\]. Except for the first block (up to 32 KB) which is used for MMU cache, the rest of this memory range is used to store parts of application which need to run from RAM.

<!-- Only for: not esp32 -->
> **Note**
>
> ### When to Place Code in IRAM

Cases when parts of the application should be placed into IRAM:

- Interrupt handlers must be placed into IRAM if `ESP_INTR_FLAG_IRAM` is used when registering the interrupt handler. For more information, see `iram-safe-interrupt-handlers`.
- Some timing critical code may be placed into IRAM to reduce the penalty associated with loading the code from flash. {IDF_TARGET_NAME} reads code and data from flash via the MMU cache. In some cases, placing a function into IRAM may reduce delays caused by a cache miss and significantly improve that function's performance.

### How to Place Code in IRAM

Some code is automatically placed into the IRAM region using the linker script.

If some specific application code needs to be placed into IRAM, it can be done by using the `linker-script-generation` feature and adding a linker script fragment file to your component that targets at the entire source files or functions with the `noflash` placement. See the `linker-script-generation` docs for more information.

Alternatively, it is possible to specify IRAM placement in the source code using the `IRAM_ATTR` macro:

    #include "esp_attr.h"

    void IRAM_ATTR gpio_isr_handler(void* arg)
    {
        // ...
    }

There are some possible issues with placement in IRAM, that may cause problems with IRAM-safe interrupt handlers:

- Strings or constants inside an `IRAM_ATTR` function may not be placed in RAM automatically. It is possible to use `DRAM_ATTR` attributes to mark these, or using the linker script method will cause these to be automatically placed correctly.

  ``` c
  void IRAM_ATTR gpio_isr_handler(void* arg)
  {
     const static DRAM_ATTR uint8_t INDEX_DATA[] = { 45, 33, 12, 0 };
     const static char *MSG = DRAM_STR("I am a string stored in RAM");
  }
  ```

Note that knowing which data should be marked with `DRAM_ATTR` can be hard, the compiler will sometimes recognize that a variable or expression is constant (even if it is not marked `const`) and optimize it into flash, unless it is marked with `DRAM_ATTR`.

- GCC optimizations that automatically generate jump tables or switch/case lookup tables place these tables in flash. IDF by default builds all files with `-fno-jump-tables -fno-tree-switch-conversion` flags to avoid this.

Jump table optimizations can be re-enabled for individual source files that do not need to be placed in IRAM. For instructions on how to add the `-fno-jump-tables -fno-tree-switch-conversion` options when compiling individual source files, see `component_build_control`.

## IROM (Code Executed from flash)

If a function is not explicitly placed into `iram` or RTC memory, it is placed into flash. As IRAM is limited, most of an application's binary code must be placed into IROM instead.

<!-- Only for: esp32 -->
The mechanism by which Flash MMU is used to allow code execution from flash is described in *{IDF_TARGET_NAME} Technical Reference Manual* \> *Memory Management and Protection Units (MMU, MPU)* \[[PDF](%7BIDF_TARGET_TRM_EN_URL%7D#mpummu)\].

During `startup`, the bootloader (which runs from IRAM) configures the MMU flash cache to map the app's instruction code region to the instruction space. Flash accessed via the MMU is cached using some internal SRAM and accessing cached flash data is as fast as accessing other types of internal memory.

## DROM (Data Stored in flash)

By default, constant data is placed by the linker into a region mapped to the MMU flash cache. This is the same as the `irom` section, but is for read-only data not executable code.

The only constant data not placed into this memory type by default are literal constants which are embedded by the compiler into application code. These are placed as the surrounding function's executable instructions.

The `DRAM_ATTR` attribute can be used to force constants from DROM into the `dram` section (see above).

SOC_RTC_SLOW_MEM_SUPPORTED

## RTC Slow Memory

ESP_ROM_SUPPORT_DEEP_SLEEP_WAKEUP_STUB

Global and static variables used by code which runs from RTC memory must be placed into RTC Slow memory. For example `deep sleep <deep-sleep-stub>` variables can be placed here instead of RTC FAST memory, or code and variables accessed by the `/api-reference/system/ulp`.

The attribute macro named `RTC_NOINIT_ATTR` can be used to place data into this type of memory. The values placed into this section keep their value after waking from deep sleep.

Example:

``` c
RTC_NOINIT_ATTR uint32_t rtc_noinit_data;
```

SOC_RTC_FAST_MEM_SUPPORTED

## RTC FAST Memory

<!-- Only for: esp32c6 or esp32h2 -->
> **Note**
>
> ESP_ROM_SUPPORT_DEEP_SLEEP_WAKEUP_STUB

The same region of RTC FAST memory can be accessed as both instruction and data memory. Code which has to run after wake-up from deep sleep mode has to be placed into RTC memory. Please check detailed description in `deep sleep <deep-sleep-stub>` documentation.

<!-- Only for: esp32 -->
In single core mode (`CONFIG_FREERTOS_UNICORE`), remaining RTC FAST memory is added to the heap, unless the option `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP` is disabled. This memory can be used interchangeably with `DRAM`, but is slightly slower to access and not DMA-capable.

This option is not available in dual core mode, because on {IDF_TARGET_NAME}, RTC FAST memory can only be accessed by the PRO CPU.

<!-- Only for: not esp32 -->
Remaining RTC FAST memory is added to the heap unless the option `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP` is disabled. This memory can be used interchangeably with `DRAM`, but is slightly slower to access.

SOC_MEM_TCM_SUPPORTED

## SPM (Scratchpad Memory)

SPM (Scratchpad Memory) is a dedicated on-chip memory located near the processor core, offering deterministic access timing. SPM does not rely on hardware caching mechanisms; instead, its access is explicitly managed by software, ensuring predictable and stable latency. The access latency of SPM is configuration dependent. When parity check is enabled, the latency is 4 clock cycles and memory bandwidth is reduced. When parity check is disabled, the latency is 1 clock cycle. This type of memory is typically used to store critical code and data that are sensitive to access timing, making it suitable for real-time systems or embedded applications with strict performance and response time requirements.

## DMA-Capable Requirement

Most peripheral DMA controllers (e.g., SPI, sdmmc, etc.) have requirements that sending/receiving buffers should be placed in DRAM and word-aligned. We suggest to place DMA buffers in static variables rather than in the stack. Use macro `DMA_ATTR` to declare global/local static variables like:

``` c
DMA_ATTR uint8_t buffer[]="I want to send something";

void app_main()
{
    // initialization code...
    spi_transaction_t temp = {
        .tx_buffer = buffer,
        .length = 8 * sizeof(buffer),
    };
    spi_device_transmit(spi, &temp);
    // other stuff
}
```

Or:

``` c
void app_main()
{
    DMA_ATTR static uint8_t buffer[] = "I want to send something";
    // initialization code...
    spi_transaction_t temp = {
        .tx_buffer = buffer,
        .length = 8 * sizeof(buffer),
    };
    spi_device_transmit(spi, &temp);
    // other stuff
}
```

It is also possible to allocate DMA-capable memory buffers dynamically by using the `MALLOC_CAP_DMA <dma-capable-memory>` capabilities flag.

## DMA Buffer in the Stack

Placing DMA buffers in the stack is possible but discouraged. If doing so, pay attention to the following:

SOC_SPIRAM_SUPPORTED  
- Placing DRAM buffers on the stack is not recommended if the stack may be in PSRAM. If the stack of a task is placed in the PSRAM, several steps have to be taken as described in `external-ram`.

- Use macro `WORD_ALIGNED_ATTR` in functions before variables to place them in proper positions like:

  ``` c
  void app_main()
  {
      uint8_t stuff;
      WORD_ALIGNED_ATTR uint8_t buffer[] = "I want to send something";   //or the buffer will be placed right after stuff.
      // initialization code...
      spi_transaction_t temp = {
          .tx_buffer = buffer,
          .length = 8 * sizeof(buffer),
      };
      spi_device_transmit(spi, &temp);
      // other stuff
  }
  ```

