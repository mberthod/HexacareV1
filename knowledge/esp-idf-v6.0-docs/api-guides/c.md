<!-- Source: _sources/api-guides/c.rst.txt (ESP-IDF v6.0 documentation) -->

# C Support

ESP-IDF is primarily written in C and provides C APIs. ESP-IDF can use one of the following C Standard Library implementations:

- [Newlib](https://sourceware.org/newlib/) (default)
- [Picolibc](https://keithp.com/picolibc/) (enabled with `CONFIG_LIBC_PICOLIBC<CONFIG_LIBC_PICOLIBC>` Kconfig option)

The Newlib version is specified in `esp_libc/sbom.yml`.

In general, all C features supported by the compiler (currently GCC) can be used in ESP-IDF, unless otherwise noted in `unsupported_c_features` below.

## C Version

**GNU dialect of ISO C23** (`--std=gnu23`) is the current default C version in ESP-IDF.

To compile the source code of a certain component using a different language standard, set the desired compiler flag in the component's `CMakeLists.txt` file:

``` cmake
idf_component_register( ... )
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu11)
```

If the public header files of the component also need to be compiled with the same language standard, replace the flag `PRIVATE` with `PUBLIC`.

## Unsupported C Features

The following features are not supported in ESP-IDF.

### Nested Function Pointers

The **GNU dialect of ISO C23** supports [nested functions](https://gcc.gnu.org/onlinedocs/gcc/Nested-Functions.html). However, ESP-IDF does not support referencing nested functions as pointers. This is due to the fact that the GCC compiler generates a [trampoline](https://gcc.gnu.org/onlinedocs/gccint/Trampolines.html) (i.e., small piece of executable code) on the stack when a pointer to a nested function is referenced. ESP-IDF does not permit executing code from a stack, thus use of pointers to nested functions is not supported.
