@echo off
setlocal enabledelayedexpansion
REM CUBRID WSL Installer Build Script using WiX 3.14.1
REM This script builds the CUBRID WSL installer using CMake 3.31.8 and WiX 3.14.1

echo ========================================
echo CUBRID WSL Installer Build Script (WiX 3.14.1)
echo ========================================
echo.

set BUILD_TYPE=0
set CUBRID_VERSION=11.4
set IMAGE_COPY_OPTION=1
set SHELL_DIR=%~dp0
set BUILD_DIR=%SHELL_DIR%build\
set OS_IMAGE_DIR=%SHELL_DIR%os_image\
set VERSION_FILE=%SHELL_DIR%VERSION
REM make_image\create_image.ps1 publishes each version to os_image\<version>\,
REM always under the fixed name WiX harvests. Recomputed after -v is parsed.
set INSTALL_IMAGE_TAR_GZ_FILE=cubrid-wsl2-latest.tar.gz
set OS_IMAGE_VERSION_DIR=%OS_IMAGE_DIR%%CUBRID_VERSION%\
set IMAGE_STAMP_FILE=%OS_IMAGE_DIR%cubrid-wsl2-latest.version
set MSI_STAMP_FILE=%BUILD_DIR%CUBRID_Base.version
set WIX_DIR=C:\Program Files (x86)\WiX Toolset v3.14\bin
set WIX_SDK_DIR=C:\Program Files (x86)\WiX Toolset v3.14\SDK
call :FINDEXEC "git.exe" GIT_PATH "C:\Program Files\Git\bin\git.exe"
call :FINDEXEC "cmake.exe" CMAKE_PATH "C:\Program Files\CMake\bin\cmake.exe"
call :FINDEXEC "candle.exe" CANDLE_PATH "%WIX_DIR%\candle.exe"
call :FINDEXEC "light.exe" LIGHT_PATH "%WIX_DIR%\light.exe"
call :FINDEXEC "torch.exe" TORCH_PATH "%WIX_DIR%\torch.exe"

:PARSE_ARGS
if "%~1"=="" goto :END_PARSE_ARGS
if /i "%~1"=="-t" (
    if "%~2"=="" (
        echo [ERROR] Missing value for -t
        call :USAGE
        exit /b 1
    )
    set BUILD_TYPE=%~2
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="-v" (
    if "%~2"=="" (
        echo [ERROR] Missing value for -v
        call :USAGE
        exit /b 1
    )
    set CUBRID_VERSION=%~2
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="-c" (
    if "%~2"=="" (
        echo [ERROR] Missing value for -c
        call :USAGE
        exit /b 1
    )
    set IMAGE_COPY_OPTION=%~2
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
call :USAGE
exit /b 1
:END_PARSE_ARGS

REM Validate option values
if not "%BUILD_TYPE%"=="0" if not "%BUILD_TYPE%"=="1" if not "%BUILD_TYPE%"=="2" if not "%BUILD_TYPE%"=="3" (
    echo [ERROR] Invalid value for -t: "%BUILD_TYPE%" ^(must be 0, 1, 2, or 3^)
    call :USAGE
    exit /b 1
)
if not "%IMAGE_COPY_OPTION%"=="0" if not "%IMAGE_COPY_OPTION%"=="1" (
    echo [ERROR] Invalid value for -c: "%IMAGE_COPY_OPTION%" ^(must be 0 or 1^)
    call :USAGE
    exit /b 1
)

set OS_IMAGE_VERSION_DIR=%OS_IMAGE_DIR%%CUBRID_VERSION%\

echo [INFO] Checking CMake...
"%CMAKE_PATH%" --version >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] CMake is not installed or not in PATH.
    echo [ERROR] Please install CMake 3.31.8+ and add it to your system PATH.
    echo [ERROR] Download from: https://cmake.org/download/
    pause
    exit /b 1
)
echo [INFO] CMake is available.

for /f %%i IN (%VERSION_FILE%) do set VERSION=%%i
if EXIST "%SHELL_DIR%.git" (
  for /f "delims=" %%i in ('"%GIT_PATH%" rev-list --count --all') do set EXTRA_VERSION=0000%%i
  set EXTRA_VERSION=!EXTRA_VERSION:~-4!
) else (
  set EXTRA_VERSION=0000
)

echo [BUILD INFO] Build Type     : %BUILD_TYPE%
echo [BUILD INFO] CUBRID Version : %CUBRID_VERSION%
echo [BUILD INFO] WSL Version : v%VERSION%-%EXTRA_VERSION%
echo.

set INSTALL_FILE_NAME=CUBRID-%CUBRID_VERSION%-For-WSL-%VERSION%-%EXTRA_VERSION%-win64.exe
set MSI_FILE_NAME=CUBRID-%CUBRID_VERSION%-For-WSL-%VERSION%-%EXTRA_VERSION%-win64.msi

if "%BUILD_TYPE%"=="3" (
    call :EXE_BUILD
    if errorlevel 1 exit /b 1
    GOTO :EOF
) else if "%BUILD_TYPE%"=="2" (
    set IMAGE_COPY_OPTION=0
)
echo [INFO] Image Copy : %IMAGE_COPY_OPTION%

if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)

if not exist "%OS_IMAGE_DIR%" (
    mkdir "%OS_IMAGE_DIR%"
)

if "%IMAGE_COPY_OPTION%"=="1" (
    if exist "%OS_IMAGE_VERSION_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%" (
        if exist "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%" (
            move /y "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%" "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%".temp
        )
        echo [INFO] Copy local image... ^(%CUBRID_VERSION%^)
        copy /y "%OS_IMAGE_VERSION_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%" "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%" >nul
        if !errorLevel! neq 0 (
            echo [ERROR] Image copy fail. Please check ["%OS_IMAGE_VERSION_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%"]
            echo [ERROR] If you do not need to copy the image, use the '-c 0' option. e.g.^) build.bat -c 0
            if exist "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%".temp (
                move /y "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%".temp "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%"
            )
            pause
            exit /b 1
        ) else (
            if exist "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%".temp (
                del /f /q "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%".temp
            )
            > "%IMAGE_STAMP_FILE%" echo %CUBRID_VERSION%
            echo [INFO] %CUBRID_VERSION%\%INSTALL_IMAGE_TAR_GZ_FILE% copied.
        )
    ) else (
        echo [ERROR] %OS_IMAGE_VERSION_DIR%%INSTALL_IMAGE_TAR_GZ_FILE% does not exists.
        echo [ERROR] Create it first: powershell -File make_image\create_image.ps1 -v %CUBRID_VERSION%
        echo [ERROR] If you do not need to copy the image, use the '-c 0' option. e.g.^) build.bat -c 0
        pause
        exit /b 1
    )
)

if "%IMAGE_COPY_OPTION%"=="0" if not "%BUILD_TYPE%"=="2" (
    call :VERIFY_IMAGE_VERSION
    if errorlevel 1 (
        pause
        exit /b 1
    )
)

echo [INFO] Creating build directory...
if not exist "%BUILD_DIR%" (
    echo [INFO] Creating build directory...
    mkdir "%BUILD_DIR%"
)

echo [INFO] Changing to build directory...
cd "%BUILD_DIR%"

echo [INFO] Configuring project with CMake...
"%CMAKE_PATH%" .. -G "Visual Studio 15 2017" -A x64 -DCUB_VERSION=%VERSION% -DCUB_EXTRA_VERSION=%EXTRA_VERSION%
if %errorLevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

echo [INFO] Building project...
"%CMAKE_PATH%" --build . --config Release
if %errorLevel% neq 0 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

if "%BUILD_TYPE%"=="2" (
    GOTO :EOF
)

echo [INFO] Copying license file to build directory...
copy "%SHELL_DIR%resources\license.rtf" "license.rtf" >nul
if %errorLevel% neq 0 (
    echo [ERROR] Failed to copy license file.
    pause
    exit /b 1
)

echo [INFO] Copying Custom Action DLL to current directory for WiX...
copy "%BUILD_DIR%Release\CubridCustomActions.dll" "%BUILD_DIR%\CubridCustomActions.dll" >nul
if %errorLevel% neq 0 (
    echo [ERROR] Failed to copy Custom Action DLL.
    pause
    exit /b 1
)

echo [INFO] Creating wix directory for object files...
if not exist "%BUILD_DIR%wix" (
    echo [INFO] Creating wix directory for object files...
    mkdir "%BUILD_DIR%wix"
)

echo [INFO] Building MSI installer using WiX 3.14.1 (candle + light)...
echo [INFO] Building .wxs to .wixobj...
echo [INFO] Using generated cubrid_wsl.wxs from build directory...
"%CANDLE_PATH%" -arch x64 -out %BUILD_DIR%wix\ ^
  -ext WixUtilExtension ^
  -dSourceDir=%BUILD_DIR%Release -dProjectDir=%SHELL_DIR% -dConfiguration=Release ^
  %BUILD_DIR%cubrid_wsl.wxs %SHELL_DIR%wix_src\cubrid_wsl_ui.wxs
if %errorlevel% neq 0 goto :FAIL

echo [INFO] Building Base MSI (en-us)...
"%LIGHT_PATH%" -out %BUILD_DIR%CUBRID_Base.msi ^
  -ext WixUIExtension ^
  -ext WixUtilExtension ^
  -loc %SHELL_DIR%wix_src\strings\strings_en-us.wxl ^
  -pdbout %BUILD_DIR%wix\cubrid_base.pdb ^
  %BUILD_DIR%wix\cubrid_wsl.wixobj %BUILD_DIR%wix\cubrid_wsl_ui.wixobj
if %errorlevel% neq 0 goto :FAIL

echo [INFO] Building Target MSI (ko-kr) for transform...
"%LIGHT_PATH%" -out CUBRID_Ko.msi ^
  -ext WixUIExtension ^
  -ext WixUtilExtension ^
  -cultures:ko-kr ^
  -loc %SHELL_DIR%wix_src\strings\strings_ko-kr.wxl ^
  -pdbout %BUILD_DIR%\wix\cubrid_ko.pdb ^
  %BUILD_DIR%\wix\cubrid_wsl.wixobj %BUILD_DIR%\wix\cubrid_wsl_ui.wixobj
if %errorlevel% neq 0 goto :FAIL

echo [INFO] Building multi-language MSI (1033=en-us, 1042=ko-kr)...
cscript //nologo "%SHELL_DIR%\wix_src\embed_transforms.vbs" CUBRID_Base.msi CUBRID_Ko.msi "!TORCH_PATH!"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to build multi-language MSI.
    goto :FAIL
)

echo [INFO] Finalizing Multi-language MSI...
copy /Y CUBRID_Base.msi %MSI_FILE_NAME%
del CUBRID_Ko.msi wix\cubrid_base.pdb wix\cubrid_ko.pdb
> "%MSI_STAMP_FILE%" echo %CUBRID_VERSION%

echo [INFO] Multi-language MSI build complete.

if "%BUILD_TYPE%"=="1" (
    GOTO :EOF
)

call :EXE_BUILD
if errorlevel 1 exit /b 1

GOTO :EOF

:FAIL
echo [ERROR] WiX build failed.
exit /b 1

:FINDEXEC
set FOUNDINPATH=
if EXIST "%~3" set %2=%~3
if NOT EXIST "%~3" for %%X in (%1) do set FOUNDINPATH=%%~$PATH:X
if defined FOUNDINPATH set %2=%FOUNDINPATH:"=%
if NOT defined FOUNDINPATH if NOT EXIST "%~3" echo Executable [%1] is not found & GOTO :EOF
call echo Executable [%1] is found at [%%%2%%]
GOTO :EOF

:VERIFY_IMAGE_VERSION
if not exist "%OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%" (
    echo [ERROR] No staged install image: %OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%
    echo [ERROR] Drop the '-c 0' option so the %CUBRID_VERSION% image is staged.
    exit /b 1
)
set STAGED_VERSION=
if exist "%IMAGE_STAMP_FILE%" (
    for /f "usebackq delims=" %%i in ("%IMAGE_STAMP_FILE%") do set STAGED_VERSION=%%i
)
if not defined STAGED_VERSION (
    echo [ERROR] Cannot tell which CUBRID version the staged install image is:
    echo [ERROR]   %OS_IMAGE_DIR%%INSTALL_IMAGE_TAR_GZ_FILE%
    echo [ERROR] Run once without '-c 0' to stage %CUBRID_VERSION% and record it.
    exit /b 1
)
if /i not "%STAGED_VERSION%"=="%CUBRID_VERSION%" (
    echo [ERROR] Staged install image is CUBRID %STAGED_VERSION%, but this build is %CUBRID_VERSION%.
    echo [ERROR] Packaging it would label the installer %CUBRID_VERSION% while installing %STAGED_VERSION%.
    echo [ERROR] Drop '-c 0' to stage %CUBRID_VERSION%, or build with '-v %STAGED_VERSION%'.
    exit /b 1
)
echo [INFO] Staged install image verified: CUBRID %STAGED_VERSION%
exit /b 0

:VERIFY_MSI_VERSION
if not exist "%BUILD_DIR%CUBRID_Base.msi" (
    echo [ERROR] No MSI to bundle: %BUILD_DIR%CUBRID_Base.msi
    echo [ERROR] Build the MSI first. e.g.^) build.bat -t 1 -v %CUBRID_VERSION%
    exit /b 1
)
set BUILT_MSI_VERSION=
if exist "%MSI_STAMP_FILE%" (
    for /f "usebackq delims=" %%i in ("%MSI_STAMP_FILE%") do set BUILT_MSI_VERSION=%%i
)
if not defined BUILT_MSI_VERSION (
    echo [ERROR] Cannot tell which CUBRID version CUBRID_Base.msi was built from.
    echo [ERROR] Rebuild the MSI. e.g.^) build.bat -t 1 -v %CUBRID_VERSION%
    exit /b 1
)
if /i not "%BUILT_MSI_VERSION%"=="%CUBRID_VERSION%" (
    echo [ERROR] CUBRID_Base.msi was built for CUBRID %BUILT_MSI_VERSION%, but this bundle is %CUBRID_VERSION%.
    echo [ERROR] The EXE would be named %CUBRID_VERSION% while installing %BUILT_MSI_VERSION%.
    echo [ERROR] Rebuild with '-t 1 -v %CUBRID_VERSION%', or bundle with '-v %BUILT_MSI_VERSION%'.
    exit /b 1
)
echo [INFO] MSI to bundle verified: CUBRID %BUILT_MSI_VERSION%
exit /b 0

:EXE_BUILD
echo [INFO] Building Bundle EXE...
call :VERIFY_MSI_VERSION
if errorlevel 1 exit /b 1
cd "%BUILD_DIR%"
if exist "%INSTALL_FILE_NAME%" (
    echo [INFO] Bundle EXE already exists. Deleting...
    del "%INSTALL_FILE_NAME%"
)
if exist wix\bundle.wixobj (
    echo [INFO] wix\bundle.wixobj already exists. Deleting...
    del wix\bundle.wixobj
)
"%CANDLE_PATH%" -arch x64 -out wix\bundle.wixobj -ext WixBalExtension -ext WixUtilExtension -dProjectDir=%SHELL_DIR% %SHELL_DIR%wix_src\bundle.wxs
if %errorlevel% neq 0 goto :FAIL
"%LIGHT_PATH%" -out %INSTALL_FILE_NAME% -ext WixBalExtension -ext WixUtilExtension wix\bundle.wixobj
if %errorlevel% neq 0 goto :FAIL

echo [INFO] Bundle build complete: %INSTALL_FILE_NAME%

GOTO :EOF


:OS_IMAGE_NOT_EXIST

pause
exit /b 1
GOTO :EOF

:USAGE
echo Usage: build.bat [-t ^<0-3^>] [-v ^<version^>] [-c ^<0^|1^>] [-h]
echo.
echo Options:
echo   -t ^<0-3^>      Build type (default: 0)
echo                    0  Build MSI and Bundle EXE
echo                    1  Build only MSI
echo                    2  Build only C++ binaries
echo                    3  Build only Bundle EXE
echo   -v ^<version^>  CUBRID version (default: 11.4)
echo   -c ^<0^|1^>      Install-image copy option (default: 1)
echo                    0  Reuse the staged install image; the build aborts
echo                       unless it was staged for the same -v version
echo                    1  Delete and re-copy from os_image\^<version^>
echo   -h, /?          Show this help message
echo.
echo Examples:
echo   build.bat
echo   build.bat -t 1
echo   build.bat -v 11.4 -t 0 -c 0
GOTO :EOF
