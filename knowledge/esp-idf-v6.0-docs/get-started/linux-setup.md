<!-- Source: _sources/get-started/linux-setup.rst.txt (ESP-IDF v6.0 documentation) -->

# Installation of ESP-IDF and Tools on Linux

This section describes how to install ESP-IDF and its required tools on Linux distributions (e.g., Ubuntu) using the ESP-IDF Installation Manager (EIM).

> **Note**
>
> >
> Note

This document describes the default and recommended way to install ESP-IDF v6.0 and newer versions. ESP-IDF also supports the `legacy installation method on Linux <linux-macos-setup-legacy>`, which was the default before ESP-IDF v6.0.

## Step 1: Install the Prerequisites (Optional)

Skip this step if you plan to install EIM using `APT <install-eim-linux-apt>`.

For other installation methods, install the [required prerequisites](https://docs.espressif.com/projects/idf-im-ui/en/latest/prerequisites.html#linux). These prerequisites may vary depending on your Linux distribution.

> **Note**
>
> ## Step 2: Install the EIM

You can install the EIM using one of the following methods:

- [Download the EIM](#download-the-eim)
- [Debian-Based Linux Installation via APT](#debian-based-linux-installation-via-apt)
- [RPM-Based Linux Installation via DNF](#rpm-based-linux-installation-via-dnf)

Installing via APT or DNF allows you to easily keep EIM up to date with a single command.

### Download the EIM

<figure>
<img src="../../_static/get-started-eim-download.drawio.png" class="align-left" style="width:35.0%" alt="../../_static/get-started-eim-download.drawio.png" />
</figure>

You can choose either an online or offline installer, available in Graphical User Interface (GUI) or Command Line Interface (CLI) versions.

### Debian-Based Linux Installation via APT

Add the EIM repository to your APT sources list to make it available for installation:

``` bash
echo "deb [trusted=yes] https://dl.espressif.com/dl/eim/apt/ stable main" | sudo tee /etc/apt/sources.list.d/espressif.list

sudo apt update
```

Then, install the EIM Command Line Interface (CLI) alone, or together with Graphical User Interface (GUI) via APT:

- GUI and CLI:  
  ``` bash
  sudo apt install eim
  ```

- CLI only:  
  ``` bash
  sudo apt install eim-cli
  ```

### RPM-Based Linux Installation via DNF

Add the EIM repository to your DNF sources list to make it available for installation:

``` bash
sudo tee /etc/yum.repos.d/espressif-eim.repo << 'EOF'
[eim]
name=ESP-IDF Installation Manager
baseurl=https://dl.espressif.com/dl/eim/rpm/$basearch
enabled=1
gpgcheck=0
EOF
```

Then, install the EIM Command Line Interface (CLI) alone, or together with Graphical User Interface (GUI) via DNF:

- GUI and CLI:  
  ``` bash
  sudo dnf install eim
  ```

- CLI only:  
  ``` bash
  sudo dnf install eim-cli
  ```

## Step 3: Install ESP-IDF Using EIM
