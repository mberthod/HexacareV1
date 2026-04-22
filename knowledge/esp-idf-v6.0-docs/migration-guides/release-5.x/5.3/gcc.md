<!-- Source: _sources/migration-guides/release-5.x/5.3/gcc.rst.txt (ESP-IDF v6.0 documentation) -->

# GCC

## Common Porting Problems and Fixes

### `sys/dirent.h` No Longer Includes Function Prototypes

#### Issue

Compilation errors may occur in code that previously worked with the old toolchain. For example:

``` c
#include <sys/dirent.h>
/* .... */
DIR* dir = opendir("test_dir");
/* .... */
/**
 * Compile error:
 * test.c: In function 'test_opendir':
 * test.c:100:16: error: implicit declaration of function 'opendir' [-Werror=implicit-function-declaration]
 *   100 |     DIR* dir = opendir(path);
 *       |                ^~~~~~~
 */
```

#### Solution

To resolve this issue, the correct header must be included. Refactor the code like this:

``` c
#include <dirent.h>
/* .... */
DIR* dir = opendir("test_dir");
```
