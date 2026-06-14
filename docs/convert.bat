@echo off
setlocal enabledelayedexpansion

set DOCS_DIR=%~dp0
call :FINDEXEC "pandoc.exe" PANDOC_DIR "F:\work_util\pandoc-3.10\pandoc.exe"

if not defined PANDOC_DIR (
    echo [ERROR] pandoc.exe was not found. Install Pandoc or update the fallback path.
    exit /b 1
)

echo [INFO] DOCS_DIR: %DOCS_DIR%
echo [INFO] PANDOC_DIR: %PANDOC_DIR%

if not exist "%DOCS_DIR%network_ko.md" (
    echo [ERROR] Korean markdown file not found: %DOCS_DIR%network_ko.md
    exit /b 1
)
if not exist "%DOCS_DIR%network_en.md" (
    echo [ERROR] English markdown file not found: %DOCS_DIR%network_en.md
    exit /b 1
)
if not exist "%DOCS_DIR%upgrade_ko.md" (
    echo [ERROR] Korean markdown file not found: %DOCS_DIR%upgrade_ko.md
    exit /b 1
)
if not exist "%DOCS_DIR%upgrade_en.md" (
    echo [ERROR] English markdown file not found: %DOCS_DIR%upgrade_en.md
    exit /b 1
)

echo [INFO] Creating combined HTML documentation...
"%PANDOC_DIR%" ^
    "%DOCS_DIR%network_ko.md" "%DOCS_DIR%network_en.md" ^
    "%DOCS_DIR%upgrade_ko.md" "%DOCS_DIR%upgrade_en.md" ^
    -s -f markdown -t html ^
    --toc --toc-depth=2 ^
    -H "%DOCS_DIR%style.html" ^
    -o "%DOCS_DIR%cubrid_guide.html"
if %errorlevel% neq 0 (
    echo [ERROR] Pandoc failed with exit code %errorlevel%.
    exit /b %errorlevel%
)

echo [INFO] Documentation generated: %DOCS_DIR%cubrid_guide.html
goto :EOF

:FINDEXEC
set FOUNDINPATH=
if EXIST "%~3" set %2=%~3
if NOT EXIST "%~3" for %%X in (%1) do set FOUNDINPATH=%%~$PATH:X
if defined FOUNDINPATH set %2=%FOUNDINPATH:"=%
if NOT defined FOUNDINPATH if NOT EXIST "%~3" echo Executable [%1] is not found & GOTO :EOF
call echo Executable [%1] is found at [%%%2%%]
GOTO :EOF