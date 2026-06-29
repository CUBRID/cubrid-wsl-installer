cmake_minimum_required(VERSION 3.31.8)
project(CUBRID_For_WSL)

# Installer version — passed from build.bat via -DCUB_VERSION / -DCUB_EXTRA_VERSION.
# Falls back to VERSION file when cmake is invoked directly.
if(NOT DEFINED CUB_VERSION)
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" _ver_raw)
    string(STRIP "${_ver_raw}" CUB_VERSION)
endif()
if(NOT DEFINED CUB_EXTRA_VERSION)
    set(CUB_EXTRA_VERSION "0000")
endif()

# Set C++ standard for Visual Studio 2017 compatibility
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DEBUG_LOG option - set to 1 to enable file logging to TEMP/CUBRID/log
option(DEBUG_LOG "Enable debug file logging" 0)
if(DEBUG_LOG)
    add_compile_definitions(DEBUG_LOG=1)
endif()

# ============================================================
# Project constants — single source of truth
# All values flow into config.h (C++) and cubrid_wsl.wxs (WiX).
# ============================================================

# Registry key path (backslashes escaped for CMake; raw literal in config.h)
set(CUB_REGISTRY_KEY_PATH "Software\\CUBRID\\CUBRID_FOR_WSL"
    CACHE STRING "CUBRID WSL registry key path")

# Registry value names
set(CUB_REG_VAL_INSTALLED            "Installed")
set(CUB_REG_VAL_WSL_NAME             "WslName")
set(CUB_REG_VAL_INSTALL_DIR          "InstallDir")
set(CUB_REG_VAL_IMAGE_FILE           "ImageFile")
set(CUB_REG_VAL_TRAY_APP_FILE        "TrayAppFile")
set(CUB_REG_VAL_TRAY_APP_LINK_FILE   "TrayAppLinkFile")
set(CUB_REG_VAL_IMAGE_INSTALLED      "ImageInstalled")
set(CUB_REG_VAL_TRAY_APP_INSTALLED   "TrayAppInstalled")
set(CUB_REG_VAL_STARTER_INSTALLED    "StarterInstalled")
set(CUB_REG_VAL_DOCS_INSTALLED       "DocsInstalled")
set(CUB_REG_VAL_USERS_DESKTOP_FOLDER "UsersDesktopFolder")

# File / application names
set(CUB_WSL_IMAGE_FILE         "cubrid-wsl2-latest.tar.gz")
set(CUB_WSL_EXTRACT_IMAGE_FILE "cubrid-wsl2.tar")
set(CUB_TRAY_APP_FILE          "cubrid_tray_app.exe")
set(CUB_TRAY_APP_LINK_FILE     "cubrid_tray_app.lnk")
set(CUB_STARTER_APP_FILE       "cubrid_starter.exe")
set(CUB_GUIDE_FILE             "cubrid_guide.html")

# Generate config header for C++
configure_file(
    ${CMAKE_SOURCE_DIR}/src/include/config.h.in
    ${CMAKE_BINARY_DIR}/include/config.h
    @ONLY
)

# Generate WiX file with registry path
configure_file(
    ${CMAKE_SOURCE_DIR}/wix_src/cubrid_wsl.wxs.in
    ${CMAKE_BINARY_DIR}/cubrid_wsl.wxs
    @ONLY
)

# Make generated header available to all targets
include_directories(${CMAKE_BINARY_DIR}/include)

# Source files
add_executable(cubrid_tray_app WIN32
    src/cubrid_tray_app.cpp
    src/system_util.cpp
    src/environment_util.cpp
    src/cubrid_installer.cpp
    src/logger.cpp
    src/cubrid_tray_app.rc
)

# Header files inclusion
target_include_directories(cubrid_tray_app PRIVATE include src/include)

# Windows libraries linking
target_link_libraries(cubrid_tray_app 
    shell32
    user32
    gdi32
    winspool
    comdlg32
    advapi32
    ole32
    oleaut32
    uuid
    odbc32
    odbccp32
    msi
    wbemuuid
)

add_executable(cubrid_starter WIN32
    src/cubrid_starter.cpp
    src/system_util.cpp
    src/logger.cpp
)
target_include_directories(cubrid_starter PRIVATE include src/include)
target_link_libraries(cubrid_starter
    advapi32
    user32
    shell32
    msi
)

# Custom Action DLL for MSI
add_library(CubridCustomActions SHARED
    src/CubridCustomActions.cpp
    src/system_util.cpp
    src/environment_util.cpp
    src/cubrid_installer.cpp
    src/logger.cpp
    src/CubridCustomActions.def
)

# Header files inclusion for DLL
target_include_directories(CubridCustomActions PRIVATE include src/include)

# Windows libraries linking for Custom Action DLL
target_link_libraries(CubridCustomActions 
    msi
    shell32
    user32
    advapi32
    ole32
    oleaut32
    uuid
    wbemuuid
)

# Define DLL exports
target_compile_definitions(CubridCustomActions PRIVATE 
    BUILDING_DLL=1
)

install(TARGETS cubrid_tray_app
    RUNTIME DESTINATION "."
)

install(TARGETS cubrid_starter
    RUNTIME DESTINATION "."
)

install(TARGETS CubridCustomActions
    RUNTIME DESTINATION "."
    LIBRARY DESTINATION "."
)

install(FILES 
    "resources/license.txt"
    "resources/cubrid_icon.ico"
    DESTINATION "resources"
)

install(FILES 
    "docs/cubrid_guide.html"
    DESTINATION "docs"
)