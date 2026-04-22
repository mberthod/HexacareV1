<!-- Source: _sources/get-started/eim-install-idf.rst.txt (ESP-IDF v6.0 documentation) -->

You can install ESP-IDF and the required tools using one of the following methods, depending on your preference:

- [Online Installation Using EIM GUI](#online-installation-using-eim-gui)

  Recommended for most users. Installs ESP-IDF and tools via a graphical interface with internet access.

- [Online Installation Using EIM CLI](#online-installation-using-eim-cli)

  Installs ESP-IDF and tools from the command line with internet access.

- [Online Installation Using a Loaded Configuration](#online-installation-using-a-loaded-configuration)

  Installs ESP-IDF and tools using a pre-saved configuration file copied from another PC. This method works with both the GUI and CLI, but requires internet access.

- [Offline Installation](#offline-installation)

  Installs ESP-IDF and tools from a local package, without internet access.

# Online Installation Using EIM GUI

Open the ESP-IDF Installation Manager application <span class="title-ref">eim</span>.

Under `New Installation` click `Start Installation`.

<figure>
<img src="../../_static/get-started-eim-gui.png" class="align-center" alt="../../_static/get-started-eim-gui.png" />
<figcaption>EIM Start Installation</figcaption>
</figure>

> **Note**
>
> Under `Easy Installation`, click `Start Easy Installation` to install the latest stable version of ESP-IDF with default settings.

<figure>
<img src="../../_static/get-started-eim-gui-install.png" class="align-center" alt="../../_static/get-started-eim-gui-install.png" />
<figcaption>EIM Easy Installation</figcaption>
</figure>

If all prerequisites and path checks pass, you will see the `Ready to Install` page. Click `Start Installation` to begin the installation.

<figure>
<img src="../../_static/get-started-eim-gui-ready-install.png" class="align-center" alt="../../_static/get-started-eim-gui-ready-install.png" />
<figcaption>EIM Ready to Install</figcaption>
</figure>

During the installation, you can monitor the progress directly in the interface.

<figure>
<img src="../../_static/get-started-eim-gui-installing.png" class="align-center" alt="../../_static/get-started-eim-gui-installing.png" />
<figcaption>EIM Installing</figcaption>
</figure>

Once finished, the `Installation Complete` page will appear.

<figure>
<img src="../../_static/get-started-eim-gui-install-complete.png" class="align-center" alt="../../_static/get-started-eim-gui-install-complete.png" />
<figcaption>EIM Installation Complete</figcaption>
</figure>

If the installation fails, you can:

- Click `Logs` at the bottom of the interface to view error details. Resolve the issues and click `Try Again` to restart the installation.
- Alternatively, use [Custom Installation](https://docs.espressif.com/projects/idf-im-ui/en/latest/expert_installation.html).

> **Note**
>
> # Online Installation Using EIM CLI

Run the following command to install the latest stable version of ESP-IDF with default settings in non-interactive mode:

``` bash
eim install
```

If you encounter issues running the above command, or if you want to customize the installation path, select ESP-IDF versions, or modify other options, launch the interactive installation wizard and follow the on-screen prompts:

``` bash
eim wizard
```

If the ESP-IDF version you want to install is not available in the interactive wizard, run the following command to install any available [versions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/versions.html#releases). For example, to install ESP-IDF v5.4.2, run:

``` bash
eim install -i v5.4.2
```

Once the installation is complete, you will see the following message in the terminal:

``` bash
2025-11-03T15:54:12.537993300+08:00 - INFO - Wizard result: %{r}
2025-11-03T15:54:12.544174+08:00 - INFO - Successfully installed IDF
2025-11-03T15:54:12.545913900+08:00 - INFO - Now you can start using IDF tools
```

> **Note**
>
> # Online Installation Using a Loaded Configuration

When you install ESP-IDF, the installer automatically saves your setup to a configuration file named `eim_config.toml` in the installation directory. This configuration file can be reused on other computers to reproduce the same installation setup.

To install ESP-IDF using an existing `eim_config.toml` file, refer to the [EIM documentation \> Configuration Files](https://docs.espressif.com/projects/idf-im-ui/en/latest/gui_configuration.html#configuration-files).

# Offline Installation

Both the GUI and CLI installers support offline installation. For instructions, refer to [EIM documentation \> Offline Installation](https://docs.espressif.com/projects/idf-im-ui/en/latest/offline_installation.html).

## Next Steps

You are now ready to start developing with ESP-IDF. To begin building and running your first application, continue with the `get-started-build` section.
