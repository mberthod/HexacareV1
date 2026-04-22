<!-- Source: _sources/api-reference/system/console.rst.txt (ESP-IDF v6.0 documentation) -->

# Console

ESP-IDF provides `console` component, which includes building blocks needed to develop an interactive console over serial port. This component includes the following features:

- Line editing, provided by [linenoise]() library. This includes handling of backspace and arrow keys, scrolling through command history, command auto-completion, and argument hints.
- Splitting of command line into arguments.
- Argument parsing, provided by [argtable3]() library. This library includes APIs used for parsing GNU style command line arguments.
- Functions for registration and dispatching of commands.
- Functions to establish a basic REPL (Read-Evaluate-Print-Loop) environment.

> **Note**
>
> > **Note**
>
> ## Line Editing

Line editing feature lets users compose commands by typing them, erasing symbols using the `backspace` key, navigating within the command using the left/right keys, navigating to previously typed commands using the up/down keys, and performing autocompletion using the `tab` key.

> **Note**
>
> Here is an overview of functions provided by [linenoise](https://github.com/antirez/linenoise) library.

### Configuration

Linenoise library does not need explicit initialization. However, some configuration defaults may need to be changed before invoking the main line editing function.

- `linenoiseClearScreen`

  Clear terminal screen using an escape sequence and position the cursor at the top left corner.

- `linenoiseSetMultiLine`

  Switch between single line and multi line editing modes. In single line mode, if the length of the command exceeds the width of the terminal, the command text is scrolled within the line to show the end of the text. In this case the beginning of the text is hidden. Single line mode needs less data to be sent to refresh screen on each key press, so exhibits less glitching compared to the multi line mode. On the flip side, editing commands and copying command text from terminal in single line mode is harder. Default is single line mode.

- `linenoiseAllowEmpty`

  Set whether linenoise library returns a zero-length string (if `true`) or `NULL` (if `false`) for empty lines. By default, zero-length strings are returned.

- `linenoiseSetMaxLineLen`

  Set maximum length of the line for linenoise library. Default length is 4096 bytes. The default value can be updated to optimize RAM memory usage.

- `linenoiseSetReadFunction`

  Set the read function to be used by linenoise.

- `linenoiseSetReadCharacteristics`

  Set the characteristics of the read file descriptor (e.g., blocking or non-blocking mode). The function has a weak definition in `linenoise.c` that can be overridden by providing a strong definition of the function.

### Main Loop

- `linenoise`

  In most cases, console applications have some form of read/eval loop. `linenoise` is the single function which handles user's key presses and returns the completed line once the `enter` key is pressed. As such, it handles the `read` part of the loop.

- `linenoiseFree`

  This function must be called to release the command line buffer obtained from `linenoise` function.

### Hints and Completions

- `linenoiseSetCompletionCallback`

  When the user presses the `tab` key, linenoise library invokes the completion callback. The callback should inspect the contents of the command typed so far and provide a list of possible completions using calls to `linenoiseAddCompletion` function. `linenoiseSetCompletionCallback` function should be called to register this completion callback, if completion feature is desired.

  `console` component provides a ready made function to provide completions for registered commands, `esp_console_get_completion` (see below).

- `linenoiseAddCompletion`

  Function to be called by completion callback to inform the library about possible completions of the currently typed command.

- `linenoiseSetHintsCallback`

  Whenever user input changes, linenoise invokes the hints callback. This callback can inspect the command line typed so far, and provide a string with hints (which can include list of command arguments, for example). The library then displays the hint text on the same line where editing happens, possibly with a different color.

- `linenoiseSetFreeHintsCallback`

  If the hint string returned by hints callback is dynamically allocated or needs to be otherwise recycled, the function which performs such cleanup should be registered via `linenoiseSetFreeHintsCallback`.

### History

- `linenoiseHistorySetMaxLen`

  This function sets the number of most recently typed commands to be kept in memory. Users can navigate the history using the up/down arrows keys.

- `linenoiseHistoryAdd`

  Linenoise does not automatically add commands to history. Instead, applications need to call this function to add command strings to the history.

- `linenoiseHistorySave`

  Function saves command history from RAM to a text file, for example on an SD card or on a filesystem in flash memory.

- `linenoiseHistoryLoad`

  Counterpart to `linenoiseHistorySave`, loads history from a file.

- `linenoiseHistoryFree`

  Releases memory used to store command history. Call this function when done working with linenoise library.

## Splitting of Command Line into Arguments

`console` component provides `esp_console_split_argv` function to split command line string into arguments. The function returns the number of arguments found (`argc`) and fills an array of pointers which can be passed as `argv` argument to any function which accepts arguments in `argc, argv` format.

The command line is split into arguments according to the following rules:

- Arguments are separated by spaces
- If spaces within arguments are required, they can be escaped using `\` (backslash) character.
- Other escape sequences which are recognized are `\\` (which produces literal backslash) and `\"`, which produces a double quote.
- Arguments can be quoted using double quotes. Quotes may appear only in the beginning and at the end of the argument. Quotes within the argument must be escaped as mentioned above. Quotes surrounding the argument are stripped by `esp_console_split_argv` function.

Examples:

- `abc def 1 20 .3` \> \[ `abc`, `def`, `1`, `20`, `.3` \]
- `abc "123 456" def` \> \[ `abc`, `123 456`, `def` \]
- `` `a\ b\\c\" `` \> \[ `a b\c"` \]

## Argument Parsing

For argument parsing, `console` component includes [argtable3](https://www.argtable.org/) library. Please see [tutorial](https://www.argtable.org/tutorial/) for an introduction to [argtable3](https://www.argtable.org/). Github repository also includes [examples](https://github.com/argtable/argtable3/tree/master/examples).

## Command Registration and Dispatching

`console` component includes utility functions which handle registration of commands, matching commands typed by the user to registered ones, and calling these commands with the arguments given on the command line.

Application first initializes command registration module using a call to `esp_console_init`, and calls `esp_console_cmd_register` function to register command handlers.

For each command, application provides the following information (in the form of `esp_console_cmd_t` structure):

- Command name (string without spaces)
- Help text explaining what the command does
- Optional hint text listing the arguments of the command. If application uses Argtable3 for argument parsing, hint text can be generated automatically by providing a pointer to argtable argument definitions structure instead.
- Command handler function (without context), or
- Command handler function (with context). If this function is given, an additional call to `esp_console_cmd_set_context` must follow *before* the command may be called to initialize the context.

> **Note**
>
> A few other functions are provided by the command registration module:

- `esp_console_run`

  This function takes the command line string, splits it into argc/argv argument list using `esp_console_split_argv`, looks up the command in the list of registered components, and if it is found, executes its handler.

- `esp_console_register_help_command`

  Adds `help` command to the list of registered commands. This command prints the list of all the registered commands, along with their arguments and help texts.

- `esp_console_get_completion`

  Callback function to be used with `linenoiseSetCompletionCallback` from linenoise library. Provides completions to linenoise based on the list of registered commands.

- `esp_console_get_hint`

  Callback function to be used with `linenoiseSetHintsCallback` from linenoise library. Provides argument hints for registered commands to linenoise.

## Initialize Console REPL Environment

To establish a basic REPL environment, `console` component provides several useful APIs, combining those functions described above.

In a typical application, you only need to call `esp_console_new_repl_uart` to initialize the REPL environment based on UART device, including driver install, basic console configuration, spawning a thread to do REPL task and register several useful commands (e.g., <span class="title-ref">help</span>).

After that, you can register your own commands with `esp_console_cmd_register`. The REPL environment keeps in init state until you call `esp_console_start_repl`.

SOC_USB_SERIAL_JTAG_SUPPORTED

Likewise, if your REPL environment is based on USB_SERIAL_JTAG device, you only need to call `esp_console_new_repl_usb_serial_jtag` at first step. Then call other functions as usual.

## Application Examples

- `system/console/basic` demonstrates how to use the REPL (Read-Eval-Print Loop) APIs of the Console Component to create an interactive shell on {IDF_TARGET_NAME}, which can be controlled over a serial interface, supporting UART and USB interfaces, and can serve as a basis for applications requiring a command-line interface.
- `system/console/advanced` demonstrates how to use the Console Component to create an interactive shell on {IDF_TARGET_NAME}, which can be controlled over a serial interface, supporting UART and USB interfaces, providing a basis for applications that require a command-line interface.

## API Reference

inc/esp_console.inc

