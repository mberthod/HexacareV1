<!-- Source: _sources/get-started/windows-setup.rst.txt (ESP-IDF v6.0 documentation) -->

# Installation of ESP-IDF and Tools on Windows

This section describes how to install ESP-IDF and its required tools on Windows using the Espressif Installation Manager (EIM).

> **Note**
>
> >
> Note

This document describes the default and recommended way to install ESP-IDF v6.0 and newer versions. ESP-IDF also supports the `legacy method for updating ESP-IDF tools on Windows <windows-setup-update-legacy>`.

## Step 1: Install the Prerequisites (Optional)

During ESP-IDF installation, the EIM automatically checks for required prerequisites and prompts you to install any missing prerequisites.

If automatic installation fails, you can install these prerequisites manually:

- [Git](https://git-scm.com/install/windows)

- [Python](https://www.python.org/downloads/windows/)

  > **Note**
>
> Python 3.10 is the minimum supported version for ESP-IDF.
>
>   For the Python version required by the EIM, please refer to the [EIM documentation](https://docs.espressif.com/projects/idf-im-ui/en/latest/prerequisites.html#python-version).
>
>   </div>
>
> ## Step 2: Install the EIM
>
> You can install the EIM using one of the following methods:
>
> - Download the EIM
>
>   <figure>
>   <img src="../../_static/get-started-eim-download.drawio.png" class="align-left" style="width:35.0%" alt="../../_static/get-started-eim-download.drawio.png" />
>   </figure>
>
>   You can choose either an online or offline installer, available in Graphical User Interface (GUI) or Command Line Interface (CLI) versions.
>
> - Install the EIM with Package Manager [WinGet](https://learn.microsoft.com/en-us/windows/package-manager/winget/)
>
>   - Install the Graphical User Interface (GUI) or Command Line Interface (CLI):
>     - GUI:
>
>       ``` bash
>       winget install Espressif.EIM
>       ```
>
>     - CLI:
>
>       ``` bash
>       winget install Espressif.EIM-CLI
>       ```
>   - This method makes it easy to keep EIM up to date with a single command.
>
> ## Step 3: Install ESP-IDF Using EIM
>
> >
> windows-setup-update-legacy

