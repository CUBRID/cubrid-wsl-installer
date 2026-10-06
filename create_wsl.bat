@echo off
setlocal enabledelayedexpansion
REM CUBRID For WSL - build everything for one version in one go.
REM
REM Runs the three steps that are otherwise run by hand, in order, and stops
REM at the first failure:
REM
REM   1) make_image\build_image.ps1   build the cubrid-wsl2:<version> image
REM   2) make_image\create_image.ps1  export it to os_image\<version>\
REM   3) build.bat                    build the MSI and the installer EXE
REM
REM With -o, the finished installer EXE is also copied to that folder.
REM
REM Only -v and -o are accepted here. Anything more specific - MSI only,
REM reusing a staged image - is what build.bat's own options are for; call
REM it directly.

set SHELL_DIR=%~dp0
set CUBRID_VERSION=11.4
set OUTPUT_DIR=
set FULL_VERSION_STAMP=cubrid-wsl2-latest.fullversion

:PARSE_ARGS
if "%~1"=="" goto :END_PARSE_ARGS
if /i "%~1"=="-v" (
    if "%~2"=="" (
        echo [ERROR] Missing value for -v
        goto :ARG_ERROR
    )
    set CUBRID_VERSION=%~2
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="-o" (
    if "%~2"=="" (
        echo [ERROR] Missing value for -o
        goto :ARG_ERROR
    )
    REM Resolve to a full path here, while the current directory is still the
    REM one the user typed the command in: build.bat cd's into build\ later.
    for %%i in ("%~2") do set OUTPUT_DIR=%%~fi
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="-h" (
    call :USAGE
    exit /b 0
)
if "%~1"=="/?" (
    call :USAGE
    exit /b 0
)
echo [ERROR] Unknown option: %~1
goto :ARG_ERROR
:END_PARSE_ARGS

REM 'C:\out\' would otherwise build 'C:\out\\file.exe'. A drive root keeps
REM its backslash - 'C:' alone means the current directory on C:, not C:\.
if defined OUTPUT_DIR (
    if "!OUTPUT_DIR:~-1!"=="\" if not "!OUTPUT_DIR:~-2!"==":\" set OUTPUT_DIR=!OUTPUT_DIR:~0,-1!
)

echo ========================================
echo CUBRID For WSL - Full Build
echo ========================================
echo [INFO] CUBRID Version : %CUBRID_VERSION%
if defined OUTPUT_DIR echo [INFO] Output Folder  : !OUTPUT_DIR!
echo.

echo ----------------------------------------
echo [STEP 1/3] Building the cubrid-wsl2 Docker image...
echo ----------------------------------------
powershell -ExecutionPolicy Bypass -File "%SHELL_DIR%make_image\build_image.ps1" -v "%CUBRID_VERSION%"
if errorlevel 1 (
    echo.
    echo [ERROR] build_image.ps1 failed. Nothing was staged; fix the error and re-run.
    exit /b 1
)

echo.
echo ----------------------------------------
echo [STEP 2/3] Exporting the image to os_image\%CUBRID_VERSION%...
echo ----------------------------------------
powershell -ExecutionPolicy Bypass -File "%SHELL_DIR%make_image\create_image.ps1" -v "%CUBRID_VERSION%"
if errorlevel 1 (
    echo.
    echo [ERROR] create_image.ps1 failed. The Docker image from step 1 is still there,
    echo [ERROR] so re-running this script will not rebuild it from scratch for nothing.
    exit /b 1
)

echo.
echo ----------------------------------------
echo [STEP 3/3] Building the installer...
echo ----------------------------------------
call "%SHELL_DIR%build.bat" -v "%CUBRID_VERSION%"
if errorlevel 1 (
    echo.
    echo [ERROR] build.bat failed.
    exit /b 1
)

REM Step 2 has just written the stamp, so this names the EXE build.bat made.
set CUBRID_FULL_VERSION=%CUBRID_VERSION%
set FULL_VERSION_FILE=%SHELL_DIR%os_image\%CUBRID_VERSION%\%FULL_VERSION_STAMP%
if exist "%FULL_VERSION_FILE%" (
    for /f "usebackq delims=" %%i in ("%FULL_VERSION_FILE%") do set CUBRID_FULL_VERSION=%%i
)
set INSTALL_FILE_NAME=CUBRID-!CUBRID_FULL_VERSION!-For-WSL-win64.exe
set INSTALL_FILE_PATH=%SHELL_DIR%build\!INSTALL_FILE_NAME!

if not exist "!INSTALL_FILE_PATH!" (
    echo.
    echo [ERROR] build.bat reported success but the installer is not where it should be:
    echo [ERROR]   !INSTALL_FILE_PATH!
    exit /b 1
)

if defined OUTPUT_DIR (
    echo.
    echo ----------------------------------------
    echo [COPY] Copying the installer to !OUTPUT_DIR!...
    echo ----------------------------------------
    if not exist "!OUTPUT_DIR!" (
        echo [INFO] Creating !OUTPUT_DIR!
        mkdir "!OUTPUT_DIR!"
        if errorlevel 1 (
            echo [ERROR] Could not create !OUTPUT_DIR!
            exit /b 1
        )
    )
    REM Only the bundle EXE is published; the MSI stays an intermediate.
    copy /y "!INSTALL_FILE_PATH!" "!OUTPUT_DIR!\!INSTALL_FILE_NAME!" >nul
    if errorlevel 1 (
        echo [ERROR] Could not copy the installer to !OUTPUT_DIR!
        exit /b 1
    )
    echo [INFO] Copied.
)

echo.
echo ========================================
echo Build complete.
echo ========================================
echo   Engine    : !CUBRID_FULL_VERSION!
echo   Installer : !INSTALL_FILE_PATH!
if defined OUTPUT_DIR echo   Copied to : !OUTPUT_DIR!\!INSTALL_FILE_NAME!
GOTO :EOF

:ARG_ERROR
REM Reached by goto, never fallen into. The missing-value branch above jumps
REM here instead of calling :USAGE in place: in cmd.exe a 'call' followed by
REM 'exit /b N' inside a *nested* parenthesised block loses N, and the script
REM would report the error and still exit 0.
call :USAGE
exit /b 1

:USAGE
echo Usage: create_wsl.bat [-v ^<version^>] [-o ^<folder^>] [-h]
echo.
echo Builds the Docker image, exports it, and builds the installer EXE,
echo in that order, stopping at the first failure.
echo.
echo Options:
echo   -v ^<version^>  CUBRID version (default: 11.4). A whole line or one
echo                   exact nightly build:
echo                     11.4                  newest 11.4 nightly
echo                     11.5.0                newest 11.5.0.* nightly
echo                     11.5.0.2632           that build
echo                     11.5.0.2632-e17fdab   that exact directory
echo   -o ^<folder^>   Also copy the finished installer EXE into this folder.
echo                   Created if missing; an EXE already there is replaced.
echo                   A relative path is taken from the current directory.
echo                   Only the EXE is copied - not the MSI.
echo   -h, /?          Show this help message
echo.
echo Examples:
echo   create_wsl.bat
echo   create_wsl.bat -v 11.5.0
echo   create_wsl.bat -v 11.5.0.2632-e17fdab
echo   create_wsl.bat -v 11.4 -o D:\release
echo.
echo Needs Docker Desktop running, plus everything build.bat needs
echo (CMake, WiX 3.14, Visual Studio). For a published release instead of a
echo nightly, or to rebuild only part of this, run the three steps yourself:
echo   powershell -ExecutionPolicy Bypass -File make_image\build_image.ps1 -v 11.4 -Release
echo   powershell -ExecutionPolicy Bypass -File make_image\create_image.ps1 -v 11.4
echo   build.bat -v 11.4
GOTO :EOF
