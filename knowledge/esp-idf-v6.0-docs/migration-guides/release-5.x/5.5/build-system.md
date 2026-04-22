<!-- Source: _sources/migration-guides/release-5.x/5.5/build-system.rst.txt (ESP-IDF v6.0 documentation) -->

# Build System

## Examples Built with MINIMAL_BUILD Property Enabled

Most of the examples in ESP-IDF are now being built with the `MINIMAL_BUILD` property enabled in their project `CMakeLists.txt` by using `idf_build_set_property(MINIMAL_BUILD ON)`. This reduces the build time by including only the `main` component and its transitive dependencies.

As a side effect, only these components appear in menuconfig, as noted in the `Components config` menu. With `MINIMAL_BUILD` enabled, to make other components visible and compiled, add them as dependencies of the `main` component dependency or as its transitive dependencies. For more information, please see `Including Components in the Build <including-components-in-the-build>`.
