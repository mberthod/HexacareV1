<!-- Source: _sources/api-reference/storage/partition.rst.txt (ESP-IDF v6.0 documentation) -->

# Partitions API

## Overview

The `esp_partition` component has higher-level API functions which work with partitions defined in the `/api-guides/partition-tables`. These APIs are based on lower level API provided by `/api-reference/peripherals/spi_flash/index`.

## Partition Table API

ESP-IDF projects use a partition table to maintain information about various regions of SPI flash memory (bootloader, various application binaries, data, filesystems). More information can be found in `/api-guides/partition-tables`.

This component provides API functions to enumerate partitions found in the partition table and perform operations on them. These functions are declared in `esp_partition.h`:

- `esp_partition_find` checks a partition table for entries with specific type, returns an opaque iterator.
- `esp_partition_get` returns a structure describing the partition for a given iterator.
- `esp_partition_next` shifts the iterator to the next found partition.
- `esp_partition_iterator_release` releases iterator returned by `esp_partition_find`.
- `esp_partition_find_first` is a convenience function which returns the structure describing the first partition found by `esp_partition_find`.
- `esp_partition_read`, `esp_partition_write`, `esp_partition_erase_range` are equivalent to `esp_flash_read`, `esp_flash_write`, `esp_flash_erase_region`, but operate within partition boundaries.

## Application Examples

- `storage/partition_api/partition_ops` demonstrates how to perform read, write, and erase operations on a partition table.
- `storage/parttool` demonstrates how to use the partitions tool to perform operations such as reading, writing, erasing partitions, retrieving partition information, and dumping the entire partition table.
- `storage/partition_api/partition_find` demonstrates how to search the partition table and return matching partitions based on set constraints such as partition type, subtype, and label/name.
- `storage/partition_api/partition_mmap` demonstrates how to configure the MMU, map a partition into memory address space for read operations, and verify the data written and read.

## See Also

- `../../api-guides/partition-tables`
- `../system/ota` provides high-level API for updating applications stored in flash.
- `nvs_flash` provides a structured API for storing small pieces of data in SPI flash.

## API Reference - Partition Table

inc/esp_partition.inc

