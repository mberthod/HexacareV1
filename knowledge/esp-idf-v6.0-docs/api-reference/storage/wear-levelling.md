<!-- Source: _sources/api-reference/storage/wear-levelling.rst.txt (ESP-IDF v6.0 documentation) -->

# See Also

- `./fatfs`
- `../../api-guides/partition-tables`

# Application Examples

- `storage/wear_levelling` demonstrates how to use the wear levelling library and FatFS library to store files in a partition, as well as write and read data from these files using POSIX and C library APIs.

# High-level API Reference

## Header Files

- `fatfs/vfs/esp_vfs_fat.h`

High-level wear levelling functions `esp_vfs_fat_spiflash_mount_rw_wl`, `esp_vfs_fat_spiflash_unmount_rw_wl` and struct `esp_vfs_fat_mount_config_t` are described in `./fatfs`.

# Mid-level API Reference

inc/wear_levelling.inc

