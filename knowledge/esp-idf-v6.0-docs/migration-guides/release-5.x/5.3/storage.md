<!-- Source: _sources/migration-guides/release-5.x/5.3/storage.rst.txt (ESP-IDF v6.0 documentation) -->

# Storage

## VFS

The UART implementation of VFS operators has been moved from <span class="title-ref">vfs</span> component to <span class="title-ref">esp_driver_uart</span> component.

APIs with <span class="title-ref">esp_vfs_dev_uart\_</span> prefix are all deprecated, replaced with new APIs in <span class="title-ref">uart_vfs.h</span> starting with <span class="title-ref">uart_vfs_dev\_</span> prefix. Specifically, - `esp_vfs_dev_uart_register` has been renamed to `uart_vfs_dev_register` - `esp_vfs_dev_uart_port_set_rx_line_endings` has been renamed to `uart_vfs_dev_port_set_rx_line_endings` - `esp_vfs_dev_uart_port_set_tx_line_endings` has been renamed to `uart_vfs_dev_port_set_tx_line_endings` - `esp_vfs_dev_uart_use_nonblocking` has been renamed to `uart_vfs_dev_use_nonblocking` - `esp_vfs_dev_uart_use_driver` has been renamed to `uart_vfs_dev_use_driver`

For compatibility, <span class="title-ref">vfs</span> component still registers <span class="title-ref">esp_driver_uart</span> as its private dependency. In other words, you do not need to modify the CMake file of an existing project.
