<!-- Source: _sources/api-reference/template.rst.txt (ESP-IDF v6.0 documentation) -->

# API Documentation Template

> **Note**
>
> ## Overview

> **Note**
>
> ## Application Example

> **Note**
>
> ## API Reference

> **Note**
>
> >
> Note

*INSTRUCTIONS*

1.  ESP-IDF repository provides automatic update of API reference documentation using `code markup retrieved by Doxygen from header files <../contribute/documenting-code>`.

2.  Update is done on each documentation build by invoking Sphinx extension `esp_extensions/run_doxygen.py` for all header files listed in the `INPUT` statement of `docs/doxygen/Doxyfile`.

3.  Each line of the `INPUT` statement (other than a comment that begins with `##`) contains a path to header file `*.h` that is used to generate corresponding `*.inc` files:

    ``` none
    ##
    ## Wi-Fi - API Reference
    ##
    ../components/esp32/include/esp_wifi.h \
    ../components/esp32/include/esp_smartconfig.h \
    ```

4.  When the headers are expanded, any macros defined by default in `sdkconfig.h` as well as any macros defined in SOC-specific `include/soc/*_caps.h` headers will be expanded. This allows the headers to include or exclude material based on the `IDF_TARGET` value.

5.  The `*.inc` files contain formatted reference of API members generated automatically on each documentation build. All `*.inc` files are placed in Sphinx `_build` directory. To see directives generated, e.g., `esp_wifi.h`, run `python gen-dxd.py esp32/include/esp_wifi.h`.

6.  To show contents of `*.inc` file in documentation, include it as follows:

    ``` none
    .. include-build-file:: inc/esp_wifi.inc
    ```

    For example see `docs/en/api-reference/network/esp_wifi.rst`

7.  Optionally, rather that using `*.inc` files, you may want to describe API in you own way. See `docs/en/api-reference/storage/fatfs.rst` for example.

    Below is the list of common `.. doxygen...::` directives:

    > - Functions - `.. doxygenfunction:: name_of_function`
    > - Unions -`.. doxygenunion:: name_of_union`
    > - Structures -`.. doxygenstruct:: name_of_structure` together with `:members:`
    > - Macros - `.. doxygendefine:: name_of_define`
    > - Type Definitions - `.. doxygentypedef:: name_of_type`
    > - Enumerations - `.. doxygenenum:: name_of_enumeration`

    See [Breathe documentation](https://breathe.readthedocs.io/en/latest/directives.html) for additional information.

    To provide a link to header file, use the <span class="title-ref">link custom role</span> directive as follows:

    ``` none
    * :component_file:`path_to/header_file.h`
    ```

8.  In any case, to generate API reference, the file `docs/doxygen/Doxyfile` should be updated with paths to `*.h` headers that are being documented.

9.  When changes are committed and documentation is built, check how this section has been rendered. `Correct annotations <../contribute/documenting-code>` in respective header files, if required.

