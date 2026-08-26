# ![CUBRID Logo](resources/cubrid_logo.png) CUBRID For WSL Installer

[![License](https://img.shields.io/badge/License-Apache%202.0%20%7C%20BSD%203--Clause-blue.svg)](resources/license.txt)  [![Release](https://img.shields.io/github/v/release/CUBRID/cubrid-wsl-installer)](https://github.com/CUBRID/cubrid-wsl-installer/releases)

**CUBRID For WSL Installer** is a Windows installer that deploys the CUBRID database server onto Windows Subsystem for Linux 2 (WSL2) with a single click.

The installer validates the host system environment, automatically enables the required Windows optional features (`Microsoft-Windows-Subsystem-Linux` and `VirtualMachinePlatform`), imports a pre-built CUBRID WSL2 image, creates the demo database, and installs a Windows system-tray application that lets users start, stop, and monitor the CUBRID service without leaving the host.

## Key Features

*   **One-click WSL2 deployment**
    * Bundles WSL distro registration and CUBRID deployment into a single MSI/Burn-based experience.
    * Imports a pre-built CUBRID image into WSL2.
    * Creates the `demodb` sample database automatically after install.
*   **Automatic environment validation**
    * Checks Windows build (>= 19041), BIOS-level virtualization, hypervisor presence, and administrator rights.
    * Detects whether `WSL` and `VirtualMachinePlatform` are already enabled and turns them on when missing.
*   **System Tray Application**
    * A native Windows tray application (`cubrid_tray_app.exe`) for starting/stopping the CUBRID service, checking status, and viewing version information.

## Downloads
You can download the latest binaries from the following links:
- **Official Downloads**: [http://www.cubrid.org/downloads](http://www.cubrid.org/downloads)
- **FTP Server**: [http://ftp.cubrid.org](http://ftp.cubrid.org/CUBRID_Tools/CUBRID_For_WSL/)

## Build from Source

### 1. Prerequisites
Ensure you have the following installed before building:
-  **Windows**: Windows 10 (build 19041 or later) or Windows 11, 64-bit
-  **Visual Studio**: Visual Studio 2017 or later (or Build Tools for Visual Studio) with the C++ desktop workload
*   **CMake**: Version 3.31.8 or higher
*   **WiX Toolset**: v3.14.1 (installed to the default location `C:\Program Files (x86)\WiX Toolset v3.14`)
*   **Git**: Required to embed the build revision into the installer version string
*   **(Optional) Docker Desktop**: Required only when rebuilding the CUBRID WSL image from scratch (see `make_image/`)
*   **(Optional) Pandoc**: Version 3.10 or later — required only to regenerate the developer documentation HTML (`dev_docs/*.md` → HTML via `dev_docs/convert.bat`)

### 2. How to Build

**Before building — prepare the CUBRID WSL2 image (required):**
`build.bat` does **not** build the Linux image itself. It bundles a **pre-built CUBRID WSL2 image** (`cubrid-wsl2-latest.tar.gz`) that must already exist; during the build it is copied automatically from `make_image/targz_images/` into `os_image/<cubrid-version>`.

If you do not have the image yet, create it once using **Docker Desktop** (run from the project root):
```cmd
cd make_image
powershell -ExecutionPolicy Bypass -File build_image.ps1
powershell -ExecutionPolicy Bypass -File create_image.ps1
cd ..
```
*   `build_image.ps1` — clones `CUBRID/cubrid-docker` and builds the `cubrid-wsl2:<version>` Docker image. (use option `-Clean` for a fresh rebuild, `-Tags` or `-v` for other versions)
*   `create_image.ps1` — exports the image's root filesystem to `os_image/version/cubrid-wsl2-latest.tar.gz`, which `build.bat` then consumes. (use option `-Tags` or `-v` for other versions)

Then build the installer with the `build.bat` script in the root directory from a Developer Command Prompt for Visual Studio.
> **Note**: The script automatically locates `cmake.exe`, `candle.exe`, `light.exe`, and `torch.exe` from `PATH` or their default install locations.

*   **Build (default)**
    ```cmd
    build.bat
    ```

 The build artifact (`CUBRID-<cubrid-version>-For-WSL-<wsl-version>-<product-version>-<git-revision>-win64.exe`) is generated in the `build` directory at the project root.


## License
This project is open source. The CUBRID For WSL Installer is distributed under a combination of **Apache License 2.0** (CUBRID Server Engine) and **BSD 3-Clause License** (CUBRID tools and connectors). See [`resources/license.txt`](resources/license.txt) for the full text.  
For more information about CUBRID license policy, please visit https://www.cubrid.org/license

## Getting Help
If you encounter any difficulties, have questions, find bugs, or want to share suggestions, please visit our community:
- Reddit: [https://www.reddit.com/r/CUBRID/](https://www.reddit.com/r/CUBRID/)
- Jira: http://jira.cubrid.org/projects/TOOLS/issues
