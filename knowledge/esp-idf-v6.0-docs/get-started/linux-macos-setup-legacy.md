<!-- Source: _sources/get-started/linux-macos-setup-legacy.rst.txt (ESP-IDF v6.0 documentation) -->

# Standard Toolchain Setup for Linux and macOS (Legacy)

> **Warning**
>
> ## Installation Step by Step

This is a detailed roadmap to walk you through the installation process.

### Setting up Development Environment

These are the steps for setting up the ESP-IDF for your {IDF_TARGET_NAME}.

- `get-started-prerequisites-legacy`
- `get-started-get-esp-idf-legacy`
- `get-started-set-up-tools-legacy`
- `get-started-set-up-env-legacy`

## Step 1. Install Prerequisites

In order to use ESP-IDF with the {IDF_TARGET_NAME}, you need to install some software packages based on your Operating System. This setup guide helps you on getting everything installed on Linux and macOS based systems.

### For Linux Users

To compile using ESP-IDF, you need to get the following packages. The command to run depends on which distribution of Linux you are using:

- Ubuntu and Debian:

      sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

- CentOS 7 & 8:

      sudo yum -y update && sudo yum install git wget flex bison gperf python3 cmake ninja-build ccache dfu-util libusbx

CentOS 7 is still supported but CentOS version 8 is recommended for a better user experience.

- Arch:

      sudo pacman -S --needed gcc git make flex bison gperf python cmake ninja ccache dfu-util libusb python-pip

> **Note**
>
> ### For macOS Users

ESP-IDF uses the version of Python installed by default on macOS.

- Install CMake & Ninja build:
  - If you have [HomeBrew](https://brew.sh/), you can run:

        brew install cmake ninja dfu-util

  - If you have [MacPorts](https://www.macports.org/install.php), you can run:

        sudo port install cmake ninja dfu-util

  - Otherwise, consult the [CMake](https://cmake.org/) and [Ninja](https://ninja-build.org/) home pages for macOS installation downloads.
- It is strongly recommended to also install [ccache](https://ccache.dev/) for faster builds. If you have [HomeBrew](https://brew.sh/), this can be done via `brew install ccache` or `sudo port install ccache` on [MacPorts](https://www.macports.org/install.php).

> **Note**
>
> ### Apple M1 Users

If you use Apple M1 platform and see an error like this:

    WARNING: directory for tool xtensa-esp32-elf version esp-2021r2-patch3-8.4.0 is present, but tool was not found
    ERROR: tool xtensa-esp32-elf has no installed versions. Please run 'install.sh' to install it.

or:

    zsh: bad CPU type in executable: ~/.espressif/tools/xtensa-esp32-elf/esp-2021r2-patch3-8.4.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-gcc

Then you need to install Apple Rosetta 2 by running

``` bash
/usr/sbin/softwareupdate --install-rosetta --agree-to-license
```

### Installing Python 3

Ensure that you have Python 3.10 or newer installed, as this is the minimum version supported by ESP-IDF.

Note that most of the recent versions of macOS include Python 3.9 (or older) by default, which is no longer supported. You will need to install Python 3.10 or later.

To install supported Python 3 on macOS:

- With [HomeBrew](https://brew.sh/), run:

      brew install python3

- With [MacPorts](https://www.macports.org/install.php), run:

      sudo port install python313

> **Note**
>
> ## Step 2. Get ESP-IDF

To build applications for the {IDF_TARGET_NAME}, you need the software libraries provided by Espressif in [ESP-IDF repository](https://github.com/espressif/esp-idf).

To get ESP-IDF, navigate to your installation directory and clone the repository with `git clone`, following instructions below specific to your operating system.

Open Terminal, and run the following commands:

inc/git-clone-bash.inc

ESP-IDF is downloaded into `~/esp/esp-idf`.

Consult `/versions` for information about which ESP-IDF version to use in a given situation.

## Step 3. Set up the Tools

Aside from the ESP-IDF, you also need to install the tools used by ESP-IDF, such as the compiler, debugger, Python packages, etc, for projects supporting {IDF_TARGET_NAME}.

``` bash
cd ~/esp/esp-idf
./install.sh {IDF_TARGET_PATH_NAME}
```

or with Fish shell

``` fish
cd ~/esp/esp-idf
./install.fish {IDF_TARGET_PATH_NAME}
```

The above commands install tools for {IDF_TARGET_NAME} only. If you intend to develop projects for more chip targets then you should list all of them and run for example:

``` bash
cd ~/esp/esp-idf
./install.sh esp32,esp32s2
```

or with Fish shell

``` fish
cd ~/esp/esp-idf
./install.fish esp32,esp32s2
```

In order to install tools for all supported targets please run the following command:

``` bash
cd ~/esp/esp-idf
./install.sh all
```

or with Fish shell

``` fish
cd ~/esp/esp-idf
./install.fish all
```

> **Note**
>
> >
> Note

For macOS users, if an error like this is shown during any step:

    <urlopen error [SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed: unable to get local issuer certificate (_ssl.c:xxx)

You may run `Install Certificates.command` in the Python folder of your computer to install certificates. For details, see [Download Error While Installing ESP-IDF Tools](https://github.com/espressif/esp-idf/issues/4775).

### Alternative File Downloads

The tools installer downloads a number of files attached to GitHub Releases. If accessing GitHub is slow then it is possible to set an environment variable to prefer Espressif's download server for GitHub asset downloads.

> **Note**
>
> To prefer the Espressif download server when installing tools, use the following sequence of commands when running `install.sh`:

``` bash
cd ~/esp/esp-idf
export IDF_GITHUB_ASSETS="dl.espressif.com/github_assets"
./install.sh
```

> **Note**
>
> ### Customizing the Tools Installation Path

The scripts introduced in this step install compilation tools required by ESP-IDF inside the user home directory: `$HOME/.espressif` on Linux. If you wish to install the tools into a different directory, **export the environment variable IDF_TOOLS_PATH before running the installation scripts**. Make sure that your user account has sufficient permissions to read and write this path.

``` bash
export IDF_TOOLS_PATH="$HOME/required_idf_tools_path"
./install.sh

. ./export.sh
```

If changing the `IDF_TOOLS_PATH`, make sure it is exported in the environment before running any ESP-IDF tools or scripts.

> **Note**
>
> ## Step 4. Set up the Environment Variables

The installed tools are not yet added to the PATH environment variable. To make the tools usable from the command line, some environment variables must be set. ESP-IDF provides another script which does that.

In the terminal where you are going to use ESP-IDF, run:

``` bash
. $HOME/esp/esp-idf/export.sh
```

or for fish (supported only since fish version 3.0.0):

``` bash
. $HOME/esp/esp-idf/export.fish
```

Note the space between the leading dot and the path!

If you plan to use esp-idf frequently, you can create an alias for executing `export.sh`:

1.  Copy and paste the following command to your shell's profile (`.profile`, `.bashrc`, `.zprofile`, etc.)

    ``` bash
    alias get_idf='. $HOME/esp/esp-idf/export.sh'
    ```

2.  Refresh the configuration by restarting the terminal session or by running `source [path to profile]`, for example, `source ~/.bashrc`.

Now you can run `get_idf` to set up or refresh the esp-idf environment in any terminal session.

Technically, you can add `export.sh` to your shell's profile directly; however, it is not recommended. Doing so activates IDF virtual environment in every terminal session (including those where IDF is not needed), defeating the purpose of the virtual environment and likely affecting other software.

## Updating ESP-IDF and Python Packages in the ESP-IDF Environment

It is recommended to update ESP-IDF from time to time, as newer versions fix bugs and/or provide new features. Please note that each ESP-IDF major and minor release version has an associated support period, and when one release branch is approaching end of life (EOL), all users are encouraged to upgrade their projects to more recent ESP-IDF releases, to find out more about support periods, see `ESP-IDF Versions <../versions>`.

The simplest way to do the update is to delete the existing `esp-idf` folder and clone it again, as if performing the initial installation described in `get-started-get-esp-idf-legacy`.

Another solution is to update only what has changed. For specific instructions, please visit `Updating ESP-IDF <updating-master>` page.

After updating ESP-IDF, execute the install script again (`./install.sh` in your `$IDF_PATH`), in case the new ESP-IDF version requires different versions of tools. See instructions at `get-started-set-up-tools-legacy`.

Once all the new tools are installed, enter the ESP-IDF environment using the export script as described in `get-started-set-up-env-legacy`.

### Updating Python Packages in the ESP-IDF Environment Without Updating ESP-IDF

Some features in ESP-IDF are not included directly in the ESP-IDF repository. Instead, they are provided by Python packages such as `esp-idf-monitor` or `esptool`, which are installed in the ESP-IDF environment by the install script. These packages can be updated independently of ESP-IDF. To update them, simply re-run the install script (`./install.sh` in your `$IDF_PATH`). If the ESP-IDF environment already exists, the script will update all Python packages in it to the latest versions compatible with the current ESP-IDF version — without updating the ESP-IDF itself.

> **Note**
>
> 