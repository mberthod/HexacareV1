<!-- Source: _sources/get-started/linux-macos-start-project.rst.txt (ESP-IDF v6.0 documentation) -->

# Start a Project on Linux and macOS from Command Line

This guide helps you to start a new project on the {IDF_TARGET_NAME} and build, flash, and monitor the device output on Linux and macOS.

## Activate the Environment

> **Note**
>
> >
> Note

This section describes the default and recommended procedures to activate the environment from ESP-IDF v6.0. If you use the `legacy installation method on Linux and macOS <linux-macos-setup-legacy>`, skip this section.

Before using ESP-IDF tools in the terminal, you must activate the ESP-IDF environment. You can do this either via the GUI or CLI.

- [Activate Using EIM GUI](#activate-using-eim-gui)
- [Activate Using EIM CLI](#activate-using-eim-cli)

### Activate Using EIM GUI

### Activate Using EIM CLI

Upon successful installation of ESP-IDF, the EIM CLI prints a command to activate the ESP-IDF environment. For example:

``` bash
You have successfully installed ESP-IDF
for using the ESP-IDF tools inside the terminal, you will find activation scripts inside the base install folder
sourcing the activation script will setup environment in the current terminal session
============================================
to activate the environment, run the following command in your terminal:
    source "/Users/username/.espressif/tools/activate_idf_v5.4.2.sh"
============================================
```

Run the highlighted command in your terminal:

``` bash
source "/Users/username/.espressif/tools/activate_idf_v5.4.2.sh"
```

Once done, you have successfully activated the ESP-IDF environment in your terminal. All subsequent ESP-IDF commands should be run in this activated terminal.

## Start a Project

Now you are ready to prepare your application for {IDF_TARGET_NAME}. You can start with `get-started/hello_world` project from `examples` directory in ESP-IDF.

> **Important**
>
> Copy the project `get-started/hello_world` to `~/esp` directory:

``` bash
cd ~/esp
cp -r $IDF_PATH/examples/get-started/hello_world .
```

> **Note**
>
> ## Connect Your Device

Now connect your {IDF_TARGET_NAME} board to the computer and check under which serial port the board is visible.

Serial ports have the following naming patterns:

- **Linux**: starting with `/dev/tty`
- **macOS**: starting with `/dev/cu.`

If you are not sure how to check the serial port name, please refer to `establish-serial-connection` for full details.

> **Note**
>
> ## Configure Your Project

Navigate to your `hello_world` directory, set {IDF_TARGET_NAME} as the target, and run the project configuration utility `menuconfig`.

``` bash
cd ~/esp/hello_world
idf.py set-target {IDF_TARGET_PATH_NAME}
idf.py menuconfig
```

After opening a new project, you should first set the target with `idf.py set-target {IDF_TARGET_PATH_NAME}`. Note that existing builds and configurations in the project, if any, are cleared and initialized in this process. The target may be saved in the environment variable to skip this step at all. See `selecting-idf-target` for additional information.

If the previous steps have been done correctly, the following menu appears:

<figure>
<img src="../../_static/project-configuration.png" class="align-center" alt="../../_static/project-configuration.png" />
<figcaption>Project configuration - Home window</figcaption>
</figure>

You are using this menu to set up project specific variables, e.g., Wi-Fi network name and password, the processor speed, etc. Setting up the project with menuconfig may be skipped for "hello_world", since this example runs with default configuration.

<!-- Only for: esp32 -->
> **Attention**
>
> > **Note**
>
> <!-- Only for: esp32 or esp32s2 or esp32s3 -->
If you are using one of the supported development boards, you can speed up your development by using Board Support Package. See [Additional Tips](#additional-tips) for more information.

<!-- Only for: esp32s2 -->
#### Console Output Configuration

If you are using the USB for flashing the {IDF_TARGET_NAME}, you need to change the channel for the console output from UART (default) to USB.

1.  Navigate to the option `Channel for console output`.

    > `Component config` \> `ESP System Settings` \> `Channel for console output`

2.  Change to the option (the default is always UART):

    > `USB CDC`

3.  Save the new configuration and exit the `menuconfig` screen.

