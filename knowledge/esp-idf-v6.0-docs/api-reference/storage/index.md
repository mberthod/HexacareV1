<!-- Source: _sources/api-reference/storage/index.rst.txt (ESP-IDF v6.0 documentation) -->

# Storage API

This section contains reference of the high-level storage APIs. They are based on low-level drivers such as SPI flash, SD/MMC.

- `Partitions API <partition>` allow block based access to SPI flash according to the `/api-guides/partition-tables`.
- `Non-Volatile Storage library (NVS) <nvs_flash>` implements a fault-tolerant wear-levelled key-value storage in SPI NOR flash.
- `Virtual File System (VFS) <vfs>` library provides an interface for registration of file system drivers. SPIFFS, FAT and various other file system libraries are based on the VFS.
- `SPIFFS <spiffs>` is a wear-levelled file system optimized for SPI NOR flash, well suited for small partition sizes and low throughput
- `FAT <fatfs>` is a standard file system which can be used in SPI flash or on SD/MMC cards
- `Wear Levelling <wear-levelling>` library implements a flash translation layer (FTL) suitable for SPI NOR flash. It is used as a container for FAT partitions in flash.

For information about storage security, please refer to `Storage Security <storage-security>`.

> **Note**
>
> fatfs fatfsgen mass_mfg.rst nvs_flash nvs_bootloader nvs_encryption nvs_partition_gen.rst nvs_partition_parse.rst sdmmc partition spiffs vfs wear-levelling storage-security.rst

## Examples

|                                                   |                                                                                                                                                               |
|---------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Code Example**                                  | **Description**                                                                                                                                               |
| `nvs_rw_blob <storage/nvs/nvs_rw_blob>`           | Shows the use of the C-style API to read and write blob data types in NVS flash.                                                                              |
| `nvs_rw_value <storage/nvs/nvs_rw_value>`         | Shows the use of the C-style API to read and write integer data types in NVS flash.                                                                           |
| `nvs_rw_value_cxx <storage/nvs/nvs_rw_value_cxx>` | Shows the use of the C++-style API to read and write integer data types in NVS flash.                                                                         |
| `nvs_statistics <storage/nvs/nvs_statistics>`     | Shows the use of the C-style API to obtain NVS usage statistics: free/used/available/total number of entries and number of namespaces in given NVS partition. |
| `nvs_iteration <storage/nvs/nvs_iteration>`       | Shows the use of the C-style API to iterate over entries of specific (or any) NVS data type and how to obtain info about such entries.                        |
| `nvs_bootloader <storage/nvs/nvs_bootloader>`     | Shows the use of the API available to the bootloader code to read NVS data.                                                                                   |
| `nvsgen <storage/nvs/nvsgen>`                     | Demonstrates how to use the Python-based NVS image generation tool to create an NVS partition image from the contents of a CSV file.                          |
| `nvs_console <storage/nvs/nvs_console>`           | Demonstrates how to use NVS through an interactive console interface.                                                                                         |

NVS API examples

|                                                         |                                                                                                  |
|---------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| **Code Example**                                        | **Description**                                                                                  |
| `fatfs/getting_started <storage/fatfs/getting_started>` | Demonstrates basic common file API (stdio.h) usage over internal flash using FATFS.              |
| `fatfs/fs_operations <storage/fatfs/fs_operations>`     | Demonstrates POSIX API for filesystem manipulation, such as moving, removing and renaming files. |

Common Filesystem API

|                                             |                                                                                                     |
|---------------------------------------------|-----------------------------------------------------------------------------------------------------|
| **Code Example**                            | **Description**                                                                                     |
| `fatfsgen <storage/fatfs/fatfsgen>`         | Demonstrates the capabilities of Python-based tooling for FATFS images available on host computers. |
| `ext_flash_fatfs <storage/fatfs/ext_flash>` | Demonstrates using FATFS over wear leveling on external flash.                                      |
| `wear_leveling <storage/wear_levelling>`    | Demonstrates using FATFS over wear leveling on internal flash.                                      |

FATFS API examples

|                                 |                                                                                                         |
|---------------------------------|---------------------------------------------------------------------------------------------------------|
| **Code Example**                | **Description**                                                                                         |
| `spiffs <storage/spiffs>`       | Shows the use of the SPIFFS API to initialize the filesystem and work with files using POSIX functions. |
| `spiffsgen <storage/spiffsgen>` | Demonstrates the capabilities of Python-based tooling for SPIFFS images available on host computers.    |

SPIFFS API examples

|                                         |                                                                                                                                                  |
|-----------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
| **Code Example**                        | **Description**                                                                                                                                  |
| `partition_api <storage/partition_api>` | Provides an overview of API functions to look up particular partitions, perform basic I/O operations, and use partitions via CPU memory mapping. |
| `parttool <storage/parttool>`           | Demonstrates the capabilities of Python-based tooling for partition images available on host computers.                                          |

Partition API examples

|                                       |                                                                                                                              |
|---------------------------------------|------------------------------------------------------------------------------------------------------------------------------|
| **Code Example**                      | **Description**                                                                                                              |
| `littlefs <storage/littlefs>`         | Shows the use of the LittleFS component to initialize the filesystem and work with a file using POSIX functions.             |
| `semihost_vfs <storage/semihost_vfs>` | Demonstrates the use of the VFS API to let an ESP-based device access a file on a JTAG-connected host using POSIX functions. |

VFS related examples
