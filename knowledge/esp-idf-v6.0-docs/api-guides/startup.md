<!-- Source: _sources/api-guides/startup.rst.txt (ESP-IDF v6.0 documentation) -->

# Application Startup Flow

This note explains various steps which happen before `app_main` function of an ESP-IDF application is called.

The high level view of startup process is as follows:

1.  `first-stage-bootloader` loads the second stage bootloader image to RAM (IRAM & DRAM) from flash offset {IDF_TARGET_CONFIG_BOOTLOADER_OFFSET_IN_FLASH}.
2.  `second-stage-bootloader` loads partition table and main app image from flash. Main app incorporates both RAM segments and read-only segments mapped via flash cache.

SOC_HP_CPU_HAS_MULTIPLE_CORES  
3.  `application-startup` executes. At this point, the second CPU and RTOS scheduler are started, which then run the `main_task`, leading to the execution of `app_main`.

not SOC_HP_CPU_HAS_MULTIPLE_CORES  
3.  `application-startup` executes. At this point, the RTOS scheduler is started, which then runs the `main_task`, leading to the execution of `app_main`.

This process is explained in detail in the following sections.

## First stage (ROM) bootloader

SOC_HP_CPU_HAS_MULTIPLE_CORES

After SoC reset, PRO CPU will start running immediately, executing reset vector code, while APP CPU will be held in reset. During startup process, PRO CPU does all the initialization. APP CPU reset is de-asserted in the `call_start_cpu0` function of application startup code. Reset vector code is located in the mask ROM of the {IDF_TARGET_NAME} chip and cannot be modified.

not SOC_HP_CPU_HAS_MULTIPLE_CORES

After SoC reset, the CPU will start running immediately to perform initialization. The reset vector code is located in the mask ROM of the {IDF_TARGET_NAME} chip and cannot be modified.

Startup code called from the reset vector determines the boot mode by checking `GPIO_STRAP_REG` register for bootstrap pin states. Depending on the reset reason, the following takes place:

ESP_ROM_SUPPORT_DEEP_SLEEP_WAKEUP_STUB  
1.  Reset from deep sleep: if the value in `RTC_CNTL_STORE6_REG` is non-zero, and CRC value of RTC memory in `RTC_CNTL_STORE7_REG` is valid, use `RTC_CNTL_STORE6_REG` as an entry point address and jump immediately to it. If `RTC_CNTL_STORE6_REG` is zero, or `RTC_CNTL_STORE7_REG` contains invalid CRC, or once the code called via `RTC_CNTL_STORE6_REG` returns, proceed with boot as if it was a power-on reset. **Note**: to run customized code at this point, a deep sleep stub mechanism is provided. Please see `deep sleep <deep-sleep-stub>` documentation for this.

1.  For power-on reset, software SoC reset, and watchdog SoC reset: check the `GPIO_STRAP_REG` register if a custom boot mode (such as UART Download Mode) is requested. If this is the case, this custom loader mode is executed from ROM. Otherwise, proceed with boot as if it was due to software CPU reset. Consult {IDF_TARGET_NAME} datasheet for a description of SoC boot modes and how to execute them.
2.  For software CPU reset and watchdog CPU reset: configure SPI flash based on EFUSE values, and attempt to load the code from flash. This step is described in more detail in the next paragraphs.

> **Note**
>
> <!-- Only for: esp32 -->
Second stage bootloader binary image is loaded from flash starting at address {IDF_TARGET_CONFIG_BOOTLOADER_OFFSET_IN_FLASH}. If `/security/secure-boot-v1` is in use then the first 4 kB sector of flash is used to store secure boot IV and digest of the bootloader image. Otherwise, this sector is unused.

<!-- Only for: esp32s2 -->
Second stage bootloader binary image is loaded from flash starting at address {IDF_TARGET_CONFIG_BOOTLOADER_OFFSET_IN_FLASH}. The 4 kB sector of flash before this address is unused.

SOC_KEY_MANAGER_SUPPORTED

Second stage bootloader binary image is loaded from flash starting at address {IDF_TARGET_CONFIG_BOOTLOADER_OFFSET_IN_FLASH}. The 8 kB sector of flash before this address is reserved for the key manager for use with flash encryption (AES-XTS).

not (esp32 or esp32s2 or SOC_KEY_MANAGER_SUPPORTED)

Second stage bootloader binary image is loaded from the start of flash at offset {IDF_TARGET_CONFIG_BOOTLOADER_OFFSET_IN_FLASH}.

## Second Stage Bootloader

In ESP-IDF, the binary image which resides at offset {IDF_TARGET_CONFIG_BOOTLOADER_OFFSET_IN_FLASH} in flash is the second stage bootloader. Second stage bootloader source code is available in `components/bootloader` directory of ESP-IDF. Second stage bootloader is used in ESP-IDF to add flexibility to flash layout (using partition tables), and allow for various flows associated with flash encryption, secure boot, and over-the-air updates (OTA) to take place.

When the first stage (ROM) bootloader is finished checking and loading the second stage bootloader, it jumps to the second stage bootloader entry point found in the binary image header.

Second stage bootloader reads the partition table found by default at offset {IDF_TARGET_CONFIG_PARTITION_TABLE_OFFSET} (`configurable value <CONFIG_PARTITION_TABLE_OFFSET>`). See `partition tables <partition-tables>` documentation for more information. The bootloader finds factory and OTA app partitions. If OTA app partitions are found in the partition table, the bootloader consults the `otadata` partition to determine which one should be booted. See `/api-reference/system/ota` for more information.

For a full description of the configuration options available for the ESP-IDF bootloader, see `bootloader`.

For the selected partition, second stage bootloader reads the binary image from flash one segment at a time:

- For segments with load addresses in internal `iram` or `dram`, the contents are copied from flash to the load address.
- For segments which have load addresses in `drom` or `irom` regions, the flash MMU is configured to provide the correct mapping from the flash to the load address.

<!-- Only for: esp32 -->
Note that the second stage bootloader configures flash MMU for both PRO and APP CPUs, but it only enables flash MMU for PRO CPU. Reason for this is that second stage bootloader code is loaded into the memory region used by APP CPU cache. The duty of enabling cache for APP CPU is passed on to the application.

Once all segments are processed - meaning code is loaded and flash MMU is set up, second stage bootloader verifies the integrity of the application and then jumps to the application entry point found in the binary image header.

## Application Startup

Application startup covers everything that happens after the app starts executing and before the `app_main` function starts running inside the main task. This is split into three stages:

- Port initialization of hardware and basic C runtime environment.
- System initialization of software services and FreeRTOS.
- Running the main task and calling `app_main`.

> **Note**
>
> ### Port Initialization

ESP-IDF application entry point is `call_start_cpu0` function found in `components/esp_system/port/cpu_start.c`. This function is executed by the second stage bootloader, and never returns.

This port-layer initialization function initializes the basic C Runtime Environment ("CRT") and performs initial configuration of the SoC's internal hardware:

- Reconfigure CPU exceptions for the app (allowing app interrupt handlers to run, and causing `fatal-errors` to be handled using the options configured for the app rather than the simpler error handler provided by ROM).
- If the option `CONFIG_BOOTLOADER_WDT_ENABLE` is not set then the RTC watchdog timer is disabled.
- Initialize internal memory (data & bss).

\- Finish configuring the MMU cache. :SOC_SPIRAM_SUPPORTED: - Enable PSRAM if configured. - Set the CPU clocks to the frequencies configured for the project. :SOC_MEMPROT_SUPPORTED: - Initialize memory protection if configured. :esp32: - Reconfigure the main SPI flash based on the app header settings (necessary for compatibility with bootloader versions before ESP-IDF V4.0, see `bootloader-compatibility`). :SOC_HP_CPU_HAS_MULTIPLE_CORES: - If the app is configured to run on multiple cores, start the other core and wait for it to initialize as well (inside the similar "port layer" initialization function `call_start_cpu1`).

SOC_HP_CPU_HAS_MULTIPLE_CORES

Once `call_start_cpu0` completes running, it calls the "system layer" initialization function `start_cpu0` found in `components/esp_system/startup.c`. Other cores will also complete port-layer initialization and call `start_other_cores` found in the same file.

not SOC_HP_CPU_HAS_MULTIPLE_CORES

Once `call_start_cpu0` completes running, it calls the "system layer" initialization function `start_cpu0` found in `components/esp_system/startup.c`.

### System Initialization

The main system initialization function is `start_cpu0`. By default, this function is weak-linked to the function `start_cpu0_default`. This means that it is possible to override this function to add some additional initialization steps.

The primary system initialization stage includes:

- Log information about this application (project name, `app-version`, etc.) if default log level enables this.
- Initialize the heap allocator (before this point all allocations must be static or on the stack).
- Initialize esp_libc component syscalls and time functions.
- Configure the brownout detector.

\- Setup libc stdin, stdout, and stderr according to the `serial console configuration <CONFIG_ESP_CONSOLE_UART>`. :esp32: - Perform any security-related checks, including burning efuses that should be burned for this configuration (including `disabling ROM download mode on ESP32 V3 <CONFIG_SECURE_UART_ROM_DL_MODE>`, `CONFIG_ESP32_DISABLE_BASIC_ROM_CONSOLE`). :not esp32: - Perform any security-related checks, including burning efuses that should be burned for this configuration (including `permanently limiting ROM download modes <CONFIG_SECURE_UART_ROM_DL_MODE>`). - Initialize SPI flash API support. - Call global C++ constructors and any C functions marked with `__attribute__((constructor))`.

Secondary system initialization allows individual components to be initialized. If a component has an initialization function annotated with the `ESP_SYSTEM_INIT_FN` macro, it will be called as part of secondary initialization. Component initialization functions have priorities assigned to them to ensure the desired initialization order. The priorities are documented in `esp_system/system_init_fn.txt` and `ESP_SYSTEM_INIT_FN` definition in source code are checked against this file.

### Running the Main Task

After all other components are initialized, the main task is created and the FreeRTOS scheduler starts running.

After doing some more initialization tasks (that require the scheduler to have started), the main task runs the application-provided function `app_main` in the firmware.

The main task that runs `app_main` has a fixed RTOS priority (one higher than the minimum) and a `configurable stack size <CONFIG_ESP_MAIN_TASK_STACK_SIZE>`.

SOC_HP_CPU_HAS_MULTIPLE_CORES

The main task core affinity is also configurable: `CONFIG_ESP_MAIN_TASK_AFFINITY`.

Unlike normal FreeRTOS tasks (or embedded C `main` functions), the `app_main` task is allowed to return. If this happens, The task is cleaned up and the system will continue running with other RTOS tasks scheduled normally. Therefore, it is possible to implement `app_main` as either a function that creates other application tasks and then returns, or as a main application task itself.

SOC_HP_CPU_HAS_MULTIPLE_CORES

### Second Core Startup

A similar but simpler startup process happens on the APP CPU:

When running system initialization, the code on PRO CPU sets the entry point for APP CPU, de-asserts APP CPU reset, and waits for a global flag to be set by the code running on APP CPU, indicating that it has started. Once this is done, APP CPU jumps to `call_start_cpu1` function in `components/esp_system/port/cpu_start.c`.

While PRO CPU does initialization in `start_cpu0` function, APP CPU runs `start_cpu_other_cores` function. Similar to `start_cpu0`, this function is weak-linked and defaults to the `start_cpu_other_cores_default` function but can be replaced with a different function by the application.

The `start_cpu_other_cores_default` function does some core-specific system initialization and then waits for the PRO CPU to start the FreeRTOS scheduler, at which point it executes `esp_startup_start_app_other_cores` which is another weak-linked function defaulting to `esp_startup_start_app_other_cores_default`.

By default `esp_startup_start_app_other_cores_default` does nothing but spin in a busy-waiting loop until the scheduler of the PRO CPU triggers an interrupt to start the RTOS scheduler on the APP CPU.

