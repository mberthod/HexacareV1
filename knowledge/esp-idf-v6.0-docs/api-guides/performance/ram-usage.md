<!-- Source: _sources/api-guides/performance/ram-usage.rst.txt (ESP-IDF v6.0 documentation) -->

# Minimizing RAM Usage

{IDF_TARGET_STATIC_MEANS_HEAP:default="Wi-Fi library, Bluetooth controller", esp32s2="Wi-Fi library", esp32c6="Wi-Fi library, Bluetooth controller, IEEE 802.15.4 library", esp32c61="Wi-Fi library, Bluetooth controller", esp32h2="Bluetooth controller, IEEE 802.15.4 library", esp32h21="Bluetooth controller, IEEE 802.15.4 library", esp32h4="Bluetooth controller, IEEE 802.15.4 library"}

In some cases, a firmware application's available RAM may run low or run out entirely. In these cases, it is necessary to tune the memory usage of the firmware application.

In general, firmware should aim to leave some headroom of free internal RAM to deal with extraordinary situations or changes in RAM usage in future updates.

## Background

Before optimizing ESP-IDF RAM usage, it is necessary to understand the basics of {IDF_TARGET_NAME} memory types, the difference between static and dynamic memory usage in C, and the way ESP-IDF uses stack and heap. This information can all be found in `/api-reference/system/mem_alloc`.

## Measuring Static Memory Usage

The `idf.py` tool can be used to generate reports about the static memory usage of an application, see `idf.py-size`.

## Measuring Dynamic Memory Usage

ESP-IDF contains a range of heap APIs for measuring free heap at runtime, see `/api-reference/system/heap_debug`.

> **Note**
>
> ## Reducing Static Memory Usage

- Reducing the static memory usage of the application increases the amount of RAM available for heap at runtime, and vice versa.
- Generally speaking, minimizing static memory usage requires monitoring the `.data` and `.bss` sizes. For tools to do this, see `idf.py-size`.
- Internal ESP-IDF functions do not make heavy use of static RAM in C. In many instances (such as {IDF_TARGET_STATIC_MEANS_HEAP}), static buffers are still allocated from the heap. However, the allocation is performed only once during feature initialization and will be freed if the feature is deinitialized. This approach is adopted to optimize the availability of free memory at various stages of the application's life cycle.

To minimize static memory use:

\- Constant data can be stored in flash memory instead of RAM, thus it is recommended to declare structures, buffers, or other variables as `const`. This approach may require modifying firmware functions to accept `const *` arguments instead of mutable pointer arguments. These changes can also help reduce the stack usage of certain functions. :SOC_BT_SUPPORTED: - If using Bluedroid, setting the option `CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY` will cause Bluedroid to allocate memory on initialization and free it on deinitialization. This does not necessarily reduce the peak memory usage, but changes it from static memory usage to runtime memory usage. - If using OpenThread, enabling the option `CONFIG_OPENTHREAD_PLATFORM_MSGPOOL_MANAGEMENT` will cause OpenThread to allocate message pool buffers from PSRAM, which will reduce static memory use.

## Determining Stack Size

In FreeRTOS, task stacks are usually allocated from the heap. The stack size for each task is fixed and passed as an argument to `xTaskCreate`. Each task can use up to its allocated stack size, but using more than this will cause an otherwise valid program to crash, with a stack overflow or heap corruption.

Therefore, determining the optimum sizes of each task stack, minimizing the required size of each task stack, and minimizing the number of task stacks as whole, can all substantially reduce RAM usage.

### Configuration Options for Stack Overflow Detection

SOC_ASSIST_DEBUG_SUPPORTED

#### Hardware Stack Guard

The Hardware Stack Guard is a reliable method for detecting stack overflow. This method uses the hardware's Debug Assistant module to monitor the CPU's stack pointer register. A panic is immediately triggered if the stack pointer register goes beyond the bounds of the current stack (see `Hardware-Stack-Guard` for more details). The Hardware Stack Guard can be enabled via the `CONFIG_ESP_SYSTEM_HW_STACK_GUARD` option.

#### End of Stack Watchpoint

The End of Stack Watchpoint feature places a CPU watchpoint at the end of the current stack. If that word is overwritten (such as in a stack overflow), a panic is triggered immediately. End of Stack Watchpoints can be enabled via the `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK` option, but can only be used if debugger watchpoints are not already being used.

#### Stack Canary Bytes

The Stack Canary Bytes feature adds a set of magic bytes at the end of each task's stack, and checks if those magic bytes have changed on every context switch. If those magic bytes are overwritten, a panic is triggered. Stack Canary Bytes can be enabled via the `CONFIG_FREERTOS_CHECK_STACKOVERFLOW` option.

> **Note**
>
> >
> Note

When using the End of Stack Watchpoint or Stack Canary Bytes, it is possible that a stack pointer skips over the watchpoint or canary bytes on a stack overflow and corrupts another region of RAM instead. Thus, these methods cannot detect all stack overflows.

SOC_ASSIST_DEBUG_SUPPORTED

Recommended and default option is `CONFIG_ESP_SYSTEM_HW_STACK_GUARD` which avoids this disadvantage.

</div>

### Run-time Methods to Determine Stack Size

- The `uxTaskGetStackHighWaterMark` returns the minimum free stack memory of a task throughout the task's lifetime in bytes, which gives a good indication of how much stack memory is left unused by a task. Note that on {IDF_TARGET_NAME}, the function returns the value in bytes (not words as stated in the standard FreeRTOS documentation).
  - The easiest time to call `uxTaskGetStackHighWaterMark` is from the task itself: call `uxTaskGetStackHighWaterMark(NULL)` to get the current task's high water mark after the time that the task has achieved its peak stack usage, i.e., if there is a main loop, execute the main loop a number of times with all possible states, and then call `uxTaskGetStackHighWaterMark`.
  - Often, it is possible to subtract almost the entire value returned here from the total stack size of a task, but allow some safety margin to account for unexpected small increases in stack usage at runtime.
- Call `uxTaskGetSystemState` to get a summary of all tasks in the system. This includes their individual stack high watermark values.

## Reducing Stack Sizes

- Avoid stack heavy functions. String formatting functions (like `printf()`) are particularly heavy users of the stack, so any task which does not ever call these can usually have its stack size reduced.
  - Using experimental `picolibc-instead-of-newlib` reduces the stack usage of `printf()` calls significantly.
  - Enabling `newlib-nano-formatting` reduces the stack usage of any task that calls `printf()` or other C string formatting functions.
- Avoid allocating large variables on the stack. In C, any large structures or arrays allocated as an automatic variable (i.e., default scope of a C declaration) uses space on the stack. To minimize the sizes of these, allocate them statically and/or see if you can save memory by dynamically allocating them from the heap only when they are needed.
- Avoid deep recursive function calls. Individual recursive function calls do not always add a lot of stack usage each time they are called, but if each function includes large stack-based variables then the overhead can get quite high.

### Reducing Task Count

Combine tasks. If a particular task is never created, the task's stack is never allocated, thus reducing RAM usage significantly. Unnecessary tasks can typically be removed if those tasks can be combined with another task. In an application, tasks can typically be combined or removed if:

- The work done by the tasks can be structured into multiple functions that are called sequentially.
- The work done by the tasks can be structured into smaller jobs that are serialized (via a FreeRTOS queue or similar) for execution by a worker task.

### Internal Task Stack Sizes

ESP-IDF allocates a number of internal tasks for housekeeping purposes or operating system functions. Some are created during the startup process, and some are created at runtime when particular features are initialized.

The default stack sizes for these tasks are usually set conservatively high to allow all common usage patterns. Many of the stack sizes are configurable, and it may be possible to reduce them to match the real runtime stack usage of the task.

> **Important**
>
> - `app-main-task` has stack size `CONFIG_ESP_MAIN_TASK_STACK_SIZE`.
- `/api-reference/system/esp_timer` system task which executes callbacks has stack size `CONFIG_ESP_TIMER_TASK_STACK_SIZE`.
- FreeRTOS Timer Task to handle FreeRTOS timer callbacks has stack size `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH`.
- `/api-reference/system/esp_event` system task to execute callbacks for the default system event loop has stack size `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`.

\- `/api-guides/lwip` TCP/IP task has stack size `CONFIG_LWIP_TCPIP_TASK_STACK_SIZE`. :SOC_BT_SUPPORTED: - `/api-reference/bluetooth/index` have task stack sizes `CONFIG_BT_BTC_TASK_STACK_SIZE`, `CONFIG_BT_BTU_TASK_STACK_SIZE`. :SOC_BT_SUPPORTED: - `/api-reference/bluetooth/nimble/index` has task stack size `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`. - The Ethernet driver creates a task for the MAC to receive Ethernet frames. If using the default config `ETH_MAC_DEFAULT_CONFIG` then the task stack size is 4 KB. This setting can be changed by passing a custom `eth_mac_config_t` struct when initializing the Ethernet MAC. - FreeRTOS idle task stack size is configured by `CONFIG_FREERTOS_IDLE_TASK_STACKSIZE`. - To see how to optimize RAM usage when using `mDNS`, please check [Minimizing RAM Usage](https://docs.espressif.com/projects/esp-protocols/mdns/docs/latest/en/index.html#minimizing-ram-usage).

> **Note**
>
> ## Reducing Heap Usage

For functions that assist in analyzing heap usage at runtime, see `/api-reference/system/heap_debug`.

Normally, optimizing heap usage consists of analyzing the usage and removing calls to `malloc()` that are not being used, reducing the corresponding sizes, or freeing previously allocated buffers earlier.

There are some ESP-IDF configuration options that can reduce heap usage at runtime:

\- lwIP documentation has a section to configure `lwip-ram-usage`. :SOC_WIFI_SUPPORTED: - `wifi-buffer-usage` describes options to either reduce the number of static buffers or reduce the maximum number of dynamic buffers in use, so as to minimize memory usage at a possible cost of performance. Note that static Wi-Fi buffers are still allocated from the heap when Wi-Fi is initialized, and will be freed if Wi-Fi is deinitialized. :esp32: - The Ethernet driver allocates DMA buffers for the internal Ethernet MAC when it is initialized - configuration options are `CONFIG_ETH_DMA_BUFFER_SIZE`, `CONFIG_ETH_DMA_RX_BUFFER_NUM`, `CONFIG_ETH_DMA_TX_BUFFER_NUM`. - Several Mbed TLS configuration options can be used to reduce heap memory usage. See the `reducing_ram_usage_mbedtls` docs for details. :esp32: - In single-core mode only, it is possible to use IRAM as byte-accessible memory added to the regular heap by enabling `CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY`. Note that this option carries a performance penalty, and the risk of security issues caused by executable data. If this option is enabled, then it is possible to set other options to prefer certain buffers allocated from this memory: `CONFIG_MBEDTLS_MEM_ALLOC_MODE`, `NimBLE <CONFIG_BT_NIMBLE_MEM_ALLOC_MODE>`. :esp32: - Reduce `CONFIG_BTDM_CTRL_BLE_MAX_CONN` if using Bluetooth LE. :esp32: - Reduce `CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN` if using Bluetooth Classic.

> **Note**
>
> ## Optimizing IRAM Usage

<!-- Only for: not esp32 -->
The available DRAM at runtime for heap usage is also reduced by the static IRAM usage. Therefore, one way to increase available DRAM is to reduce IRAM usage.

If the app allocates more static IRAM than available, then the app will fail to build, and linker errors such as `section '.iram0.text' will not fit in region 'iram0_0_seg'`, `IRAM0 segment data does not fit`, and `region 'iram0_0_seg' overflowed by 84-bytes` will be seen. If this happens, it is necessary to find ways to reduce static IRAM usage in order to link the application.

To analyze the IRAM usage in the firmware binary, use `idf.py-size`. If the firmware failed to link, steps to analyze are shown at `idf-size-linker-failed`.

The following options will reduce IRAM usage of some ESP-IDF features:

- Disable `CONFIG_FREERTOS_IN_IRAM` if enabled to place FreeRTOS functions in flash instead of IRAM. By default, FreeRTOS functions are already placed in Flash to save IRAM.
- Disable `CONFIG_RINGBUF_IN_IRAM` if enabled to place ring buffer functions in Flash instead of IRAM. By default, ring buffer functions are already placed in Flash to save IRAM.

\- Enable `CONFIG_RINGBUF_PLACE_ISR_FUNCTIONS_INTO_FLASH`. This option is not safe to use if the ISR ringbuf functions are used from an IRAM interrupt context, e.g., if `CONFIG_UART_ISR_IN_IRAM` is enabled. For the ESP-IDF drivers where this is the case, you can get an error at run-time when installing the driver in question. :SOC_WIFI_SUPPORTED: - Disabling Wi-Fi options `CONFIG_ESP_WIFI_IRAM_OPT` and/or `CONFIG_ESP_WIFI_RX_IRAM_OPT` options frees available IRAM at the cost of Wi-Fi performance. :CONFIG_ESP_ROM_HAS_SPI_FLASH: - Enabling `CONFIG_SPI_FLASH_ROM_IMPL` frees some IRAM but means that esp_flash bugfixes and new flash chip support are not available, see `/api-reference/peripherals/spi_flash/spi_flash_idf_vs_rom` for details. :esp32: - If the application uses PSRAM and is based on ESP32 rev. 3 (ECO3), setting `CONFIG_ESP32_REV_MIN` to `3` disables PSRAM bug workarounds, saving 10 KB or more of IRAM. - Disabling `CONFIG_ESP_EVENT_POST_FROM_IRAM_ISR` prevents posting `esp_event` events from `iram-safe-interrupt-handlers` but saves some IRAM. :SOC_GPSPI_SUPPORTED: - Disabling `CONFIG_SPI_MASTER_ISR_IN_IRAM` prevents spi_master interrupts from being serviced while writing to flash, and may otherwise reduce spi_master performance, but saves some IRAM. :SOC_GPSPI_SUPPORTED: - Disabling `CONFIG_SPI_SLAVE_ISR_IN_IRAM` prevents spi_slave interrupts from being serviced while writing to flash, which saves some IRAM. - Setting `CONFIG_HAL_DEFAULT_ASSERTION_LEVEL` to disable assertion for HAL component saves some IRAM, especially for HAL code who calls `HAL_ASSERT` a lot and resides in IRAM. - Refer to the sdkconfig menu `Auto-detect Flash chips`, and you can disable flash drivers which you do not need to save some IRAM. :SOC_GPSPI_SUPPORTED: - Enable `CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH`. Provided that `CONFIG_SPI_MASTER_ISR_IN_IRAM` is not enabled and the heap functions are not incorrectly used from ISRs, this option is safe to enable in all configurations. :esp32c2: - Enable `CONFIG_BT_RELEASE_IRAM`. Release BT text section and merge BT data, bss & text into a large free heap region when `esp_bt_mem_release` is called. This makes Bluetooth unavailable until the next restart, but saving ~22 KB or more of IRAM. - Disable `CONFIG_LIBC_LOCKS_PLACE_IN_IRAM` if no ISRs that run while cache is disabled (i.e. IRAM ISRs) use libc lock APIs. :CONFIG_ESP_ROM_HAS_SUBOPTIMAL_NEWLIB_ON_MISALIGNED_MEMORY: - Disable `CONFIG_LIBC_OPTIMIZED_MISALIGNED_ACCESS` to save approximately 1000 bytes of IRAM, at the cost of reduced performance.

<!-- Only for: esp32 -->
### Using SRAM1 for IRAM

The SRAM1 memory area is normally used for DRAM, but it is possible to use parts of it for IRAM with `CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM`. This memory would previously be reserved for DRAM data usage (e.g., `.bss`) by the ESP-IDF second stage bootloader and later added to the heap. After this option was introduced, the bootloader DRAM size was reduced to a value closer to what it normally actually needs.

To use this option, ESP-IDF should be able to recognize that the new SRAM1 area is also a valid load address for an image segment. If the second stage bootloader was compiled before this option existed, then the bootloader will not be able to load the app that has code placed in this new extended IRAM area. This would typically happen if you are doing an OTA update, where only the app would be updated.

If the IRAM section were to be placed in an invalid area, then this would be detected during the boot up process, and result in a failed boot:

``` text
E (204) esp_image: Segment 5 0x400845f8-0x400a126c invalid: bad load address range
```

> **Warning**
>
> SOC_SPI_MEM_SUPPORT_AUTO_SUSPEND

### Flash Suspend Feature

When using SPI flash driver API and other APIs based on the former (NVS, Partition APIs, etc.), the Cache will be disabled. During this period, any code executed must reside in internal RAM, see `concurrency-constraints-flash`. Hence, interrupt handlers that are not in internal RAM will not be executed.

To achieve this, ESP-IDF drivers usually have the following two options:

- Place the driver's internal ISR handler in the internal RAM.
- Place some control functions in the internal RAM.

User ISR callbacks and involved variables have to be in internal RAM if they are also used in interrupt contexts.

Placing additional code into IRAM will exacerbate IRAM usage. For this reason, there is `CONFIG_SPI_FLASH_AUTO_SUSPEND`, which can alleviate the aforementioned kinds of IRAM usage. By enabling this feature, the Cache will not be disabled when SPI flash driver APIs and SPI flash driver-based APIs are used. Therefore, code and data in flash can be executed or accessed normally, but with some minor delay. See `auto-suspend` for more details about this feature.

IRAM usage for flash driver can be declined with `CONFIG_SPI_FLASH_AUTO_SUSPEND` enabled. Please refer to `internal_memory_saving_for_flash_driver` for more detailed information.

Regarding the flash suspend feature usage, and corresponding response time delay, please also see this example `system/flash_suspend`.

<!-- Only for: esp32 -->
### Putting C Library in Flash

When compiling for ESP32 revisions older than ECO3 (`CONFIG_ESP32_REV_MIN`), the PSRAM Cache bug workaround (`CONFIG_SPIRAM_CACHE_WORKAROUND`) option is enabled, and the C library functions normally located in ROM are recompiled with the workaround and placed into IRAM instead. For most applications, it is safe to move many of the C library functions into flash, reclaiming some IRAM. Corresponding options include:

- `CONFIG_SPIRAM_CACHE_LIBJMP_IN_IRAM`: affects the functions `longjmp` and `setjump`.
- `CONFIG_SPIRAM_CACHE_LIBMATH_IN_IRAM`: affects the functions `abs`, `div`, `labs`, `ldiv`, `quorem`, `fpclassify` and `nan`.
- `CONFIG_SPIRAM_CACHE_LIBNUMPARSER_IN_IRAM`: affects the functions `utoa`, `itoa`, `atoi`, `atol`, `strtol`, and `strtoul`.
- `CONFIG_SPIRAM_CACHE_LIBIO_IN_IRAM`: affects the functions `wcrtomb`, `fvwrite`, `wbuf`, `wsetup`, `fputwc`, `wctomb_r`, `ungetc`, `makebuf`, `fflush`, `refill`, and `sccl`.
- `CONFIG_SPIRAM_CACHE_LIBTIME_IN_IRAM`: affects the functions `asctime`, `asctime_r`, `ctime`, `ctime_r`, `lcltime`, `lcltime_r`, `gmtime`, `gmtime_r`, `strftime`, `mktime`, `tzset_r`, `tzset`, `time`, `gettzinfo`, `systimes`, `month_lengths`, `timelocal`, `tzvars`, `tzlock`, `tzcalc_limits`, and `strptime`.
- `CONFIG_SPIRAM_CACHE_LIBCHAR_IN_IRAM`: affects the functions `ctype_`, `toupper`, `tolower`, `toascii`, `strupr`, `bzero`, `isalnum`, `isalpha`, `isascii`, `isblank`, `iscntrl`, `isdigit`, `isgraph`, `islower`, `isprint`, `ispunct`, `isspace`, and `isupper`.
- `CONFIG_SPIRAM_CACHE_LIBMEM_IN_IRAM`: affects the functions `memccpy`, `memchr`, `memmove`, and `memrchr`.
- `CONFIG_SPIRAM_CACHE_LIBSTR_IN_IRAM`: affects the functions `strcasecmp`, `strcasestr`, `strchr`, `strcoll`, `strcpy`, `strcspn`, `strdup`, `strdup_r`, `strlcat`, `strlcpy`, `strlen`, `strlwr`, `strncasecmp`, `strncat`, `strncmp`, `strncpy`, `strndup`, `strndup_r`, `strrchr`, `strsep`, `strspn`, `strstr`, `strtok_r, and`strupr\`\`.
- `CONFIG_SPIRAM_CACHE_LIBRAND_IN_IRAM`: affects the functions `srand`, `rand`, and `rand_r`.
- `CONFIG_SPIRAM_CACHE_LIBENV_IN_IRAM`: affects the functions `environ`, `envlock`, and `getenv_r`.
- `CONFIG_SPIRAM_CACHE_LIBFILE_IN_IRAM`: affects the functions `lock`, `isatty`, `fclose`, `open`, `close`, `creat`, `read`, `rshift`, `sbrk`, `stdio`, `syssbrk`, `sysclose`, `sysopen`, `creat`, `sysread`, `syswrite`, `impure`, `fwalk`, and `findfp`.
- `CONFIG_SPIRAM_CACHE_LIBMISC_IN_IRAM`: affects the functions `raise` and `system`.

The exact amount of IRAM saved will depend on how much C library code is actually used by the application. In addition, the following options may be used to move more of the C library code into flash, however note that this may result in reduced performance. Be careful not to use the C library function allocated with `ESP_INTR_FLAG_IRAM` flag from interrupts when cache is disabled, refer to `iram-safe-interrupt-handlers` for more details. For these reasons, the functions `itoa`, `memcmp`, `memcpy`, `memset`, `strcat`, `strcmp`, and `strlen` are always put in IRAM.

> **Note**
>
> > **Note**
>
> <!-- Only for: esp32s2 or esp32s3 or esp32p4 -->
### Change cache size

The {IDF_TARGET_NAME} RAM memory available size is dependent on the size of cache. Decreasing the cache size in the Kconfig options listed below will result in increasing the available RAM.

esp32s2  
- `CONFIG_ESP32S2_INSTRUCTION_CACHE_SIZE`

esp32s2  
- `CONFIG_ESP32S2_DATA_CACHE_SIZE`

esp32s3  
- `CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE`

esp32s3  
- `CONFIG_ESP32S3_DATA_CACHE_SIZE`

esp32p4  
- `CONFIG_CACHE_L2_CACHE_SIZE`

