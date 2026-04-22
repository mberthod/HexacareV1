<!-- Source: _sources/get-started/macos-setup.rst.txt (ESP-IDF v6.0 documentation) -->

# Installation of ESP-IDF and Tools on macOS

This section describes how to install ESP-IDF and its required tools on macOS using the Espressif Installation Manager (EIM).

> **Note**
>
> >
> Note

This document describes the default and recommended way to install ESP-IDF v6.0 and newer versions. ESP-IDF also supports the `legacy installation method on macOS <linux-macos-setup-legacy>`, which was the default before ESP-IDF v6.0.

## Step 1: Install the Prerequisites

Install the required prerequisites via [Homebrew](https://brew.sh/):

``` bash
brew install libgcrypt glib pixman sdl2 libslirp dfu-util cmake python
```

> **Note**
>
> ## Step 2: Install the EIM

You can install the EIM using one of the following methods:

- [Download the EIM](#download-the-eim)
- [Install the EIM with Package Manager Homebrew](#install-the-eim-with-package-manager-homebrew)

Installing EIM with Homebrew allows you to easily keep EIM up to date with a single command.

### Download the EIM

<figure>
<img src="../../_static/get-started-eim-download.drawio.png" class="align-left" style="width:35.0%" alt="../../_static/get-started-eim-download.drawio.png" />
</figure>

You can choose either an online or offline installer, available in Graphical User Interface (GUI) or Command Line Interface (CLI) versions.

### Install the EIM with Package Manager Homebrew

Add the EIM repository to the [Homebrew](https://brew.sh/) to make it available for installation:

``` bash
brew tap espressif/eim
```

Then, install the EIM Graphical User Interface (GUI) or Command Line Interface (CLI) via Homebrew:

- GUI:  
  ``` bash
  brew install --cask eim-gui
  ```

- CLI:  
  ``` bash
  brew install eim
  ```

## Step 3: Install ESP-IDF Using EIM

linux-macos-setup-legacy

