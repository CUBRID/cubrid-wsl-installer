#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <iostream>
#include <filesystem>
#include <thread>
#include <fstream>

#include "cubrid_installer.h"
#include "environment_util.h"
#include "logger.h"
#include "system_util.h"

static Logger &logger = Logger::GetInstance();

void SetupMsiContext (MSIHANDLE hInstall)
{
  SetGlobalMsiHandle (hInstall);
  Logger::GetInstance().Initialize();
}

extern "C" {

  __declspec (dllexport) UINT __stdcall RunAllEnvironmentChecks (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    return EnvironmentUtil::RunAllEnvironmentChecks (hInstall);
  }

  __declspec (dllexport) UINT __stdcall OpenFileDialog (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    return EnvironmentUtil::OpenFileDialog (hInstall);
  }

  __declspec (dllexport) UINT __stdcall StartWSL (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Starting WSL...");

    std::string wslName;

    logger.LogInfo ("Reading configuration from registry: " + std::string (REGISTRY_KEY_PATH));

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_WSL_NAME, wslName))
      {
	logger.LogError ("Failed to get WslName from registry");
	return ERROR_INSTALL_FAILURE;
      }

    CUBRIDInstaller installer;
    if (installer.StartWSL (wslName))
      {
	logger.LogInfo ("WSL started successfully");
	return ERROR_SUCCESS;
      }
    else
      {
	logger.LogError ("Failed to start WSL");
	return ERROR_INSTALL_FAILURE;
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall CreateShortcut (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Creating Desktop Shortcut...");

    std::string installDir;
    std::string wslName;
    std::string trayAppFile;
    std::string trayAppLinkFile;
    std::string desktopDir;

    logger.LogInfo ("Reading configuration from registry: " + std::string (REGISTRY_KEY_PATH));

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_INSTALL_DIR, installDir))
      {
	logger.LogError ("Failed to get InstallDir from registry");
	return ERROR_INSTALL_FAILURE;
      }

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_WSL_NAME, wslName))
      {
	logger.LogError ("Failed to get WslName from registry");
	return ERROR_INSTALL_FAILURE;
      }

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_TRAY_APP_FILE, trayAppFile))
      {
	logger.LogError ("Failed to get TrayAppFile from registry");
	return ERROR_INSTALL_FAILURE;
      }

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_TRAY_APP_LINK_FILE, trayAppLinkFile))
      {
	logger.LogError ("Failed to get TrayAppLinkFile from registry");
	return ERROR_INSTALL_FAILURE;
      }

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_USERS_DESKTOP_FOLDER, desktopDir))
      {
	logger.LogError ("Failed to get UsersDesktopFolder from registry");
	return ERROR_INSTALL_FAILURE;
      }

    CUBRIDInstaller installer;
    std::string wslPath = installer.GetWslPath();
    if (wslPath.empty())
      {
	logger.LogError ("Failed to find wsl.exe");
	return ERROR_INSTALL_FAILURE;
      }

    std::string wslShortcutTargetPath = desktopDir + "\\" + wslName + ".lnk";
    std::string wslShortcutExePath = wslPath + "\\wsl.exe";
    std::string wslShortcutArguments = "-d " + wslName + " -u cubrid --cd ~";
    std::string wslShortcutWorkingDir = wslPath;
    std::string wslShortcutIconPath = wslShortcutExePath;
    if (!installer.CreateShortcut (wslShortcutTargetPath, wslShortcutExePath, wslShortcutArguments, wslShortcutWorkingDir,
				   wslShortcutIconPath))
      {
	logger.LogError ("Failed to create shortcut for WSL");
	return ERROR_INSTALL_FAILURE;
      }

    std::string trayAppShortcutTargetPath = desktopDir + "\\" + trayAppLinkFile;
    std::string trayAppShortcutExePath = installDir + "\\" + trayAppFile;
    std::string trayAppShortcutArguments = "";
    std::string trayAppShortcutWorkingDir = installDir;
    std::string trayAppShortcutIconPath = trayAppShortcutExePath;
    if (!installer.CreateShortcut (trayAppShortcutTargetPath, trayAppShortcutExePath, trayAppShortcutArguments,
				   trayAppShortcutWorkingDir, trayAppShortcutIconPath))
      {
	logger.LogError ("Failed to create shortcut for tray app");
	return ERROR_INSTALL_FAILURE;
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall RegisterTrayApp (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Registering Tray Application...");

    std::string installDir;
    std::string trayAppFile;

    logger.LogInfo ("Reading configuration from registry: " + std::string (REGISTRY_KEY_PATH));

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_INSTALL_DIR, installDir))
      {
	logger.LogError ("Failed to get InstallDir from registry");
	return ERROR_INSTALL_FAILURE;
      }

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_TRAY_APP_FILE, trayAppFile))
      {
	logger.LogError ("Failed to get TrayAppFile from registry");
	return ERROR_INSTALL_FAILURE;
      }

    std::string trayAppPath = (std::filesystem::path (installDir) / trayAppFile).string();
    std::string workingDir = installDir;

    logger.LogInfo ("RegisterTrayApp: Tray App Path: " + trayAppPath);
    logger.LogInfo ("RegisterTrayApp: Working Dir: " + workingDir);

    if (std::filesystem::exists (trayAppPath))
      {
	logger.LogInfo ("Tray application found at: " + trayAppPath);
	logger.LogInfo ("Registering Tray Application to Windows Startup...");

	CUBRIDInstaller installer;
	if (installer.RegisterTrayApp (trayAppPath))
	  {
	    logger.LogInfo ("Tray application registered successfully to Windows Startup.");
	    return ERROR_SUCCESS;
	  }
	else
	  {
	    logger.LogError ("Failed to register tray application to Windows Startup.");
	    return ERROR_INSTALL_FAILURE;
	  }
      }
    else
      {
	logger.LogError ("Tray application not found at: " + trayAppPath);
	return ERROR_INSTALL_FAILURE;
      }
  }

  __declspec (dllexport) UINT __stdcall LaunchTrayApp (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Launching Tray Application...");

    std::string installDir;
    std::string trayAppFile;

    logger.LogInfo ("Reading configuration from registry: " + std::string (REGISTRY_KEY_PATH));

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_INSTALL_DIR, installDir))
      {
	logger.LogError ("Failed to get InstallDir from registry");
	return ERROR_INSTALL_FAILURE;
      }

    if (installDir.empty())
      {
	logger.LogError ("Install directory string is empty.");
	return ERROR_INSTALL_FAILURE;
      }

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_TRAY_APP_FILE, trayAppFile))
      {
	logger.LogError ("Failed to get TrayAppFile from registry");
	return ERROR_INSTALL_FAILURE;
      }

    std::string trayAppPath = (std::filesystem::path (installDir) / trayAppFile).string();
    std::string workingDir = installDir;

    logger.LogInfo ("LaunchTrayApp: Tray App Path: " + trayAppPath);
    logger.LogInfo ("LaunchTrayApp: Working Dir: " + workingDir);

    if (std::filesystem::exists (trayAppPath))
      {
	STARTUPINFOA si = { sizeof (si) };
	PROCESS_INFORMATION pi = { 0 };
	std::string cmd = "\"" + trayAppPath + "\"";

	if (CreateProcessA (NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, workingDir.c_str(), &si, &pi))
	  {
	    logger.LogInfo ("Tray application launched successfully via CreateProcess.");
	    CloseHandle (pi.hProcess);
	    CloseHandle (pi.hThread);
	  }
	else
	  {
	    logger.LogError ("Failed to launch tray application via CreateProcess. Error: " + std::to_string (GetLastError()));
	  }
      }
    else
      {
	logger.LogError ("Tray application not found at: " + trayAppPath);
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall UninstallWsl (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);

    CUBRIDInstaller installer;
    InstallOptions options;
    std::string wslName = "";
    std::string installPath = "";
    std::string cubridImageFile = "";
    std::string trayAppFile = "";
    std::string trayAppLinkFile = "";
    InstallOptions installOptions;

    char buffer[MAX_PATH * 4];
    DWORD bufSize = sizeof (buffer);
    bool hasRet = false;

    if (MsiGetPropertyA (hInstall, "CustomActionData", buffer, &bufSize) == ERROR_SUCCESS)
      {
	std::string data = buffer;
	logger.LogInfo ("Received CustomActionData: " + data);
	if (!CUBRIDInstaller::ParseInstallOptions (data, installOptions))
	  {
	    logger.LogError ("Failed to parse install options");
	    return ERROR_INSTALL_FAILURE;
	  }
      }

    if (installOptions.registryKeyPath.empty())
      {
	installOptions.registryKeyPath = REGISTRY_KEY_PATH;
	logger.LogWarning ("Registry key path not provided, using default: " + installOptions.registryKeyPath);
      }

    logger.LogInfo ("Reading configuration from registry: " + installOptions.registryKeyPath);

    std::string regValue;
    if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, installOptions.registryKeyPath, REGISTRY_VALUE_NAME_WSL_NAME, regValue))
      {
	wslName = regValue;
      }
    else
      {
	logger.LogWarning ("Failed to get WSL name from registry");
      }

    if (wslName.empty())
      {
	logger.LogError ("WSL name is empty and cannot be determined");
	return ERROR_INSTALL_FAILURE;
      }

    logger.LogInfo ("Uninstalling WSL distro: " + wslName);
    installer.UninstallWsl (wslName);

    if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, installOptions.registryKeyPath, REGISTRY_VALUE_NAME_INSTALL_DIR, regValue))
      {
	installPath = regValue;
      }
    else
      {
	logger.LogWarning ("Failed to get install path from registry");
      }

    if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, installOptions.registryKeyPath, REGISTRY_VALUE_NAME_IMAGE_FILE, regValue))
      {
	cubridImageFile = regValue;
      }
    else
      {
	logger.LogWarning ("Failed to get image file from registry");
      }

    if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, installOptions.registryKeyPath, REGISTRY_VALUE_NAME_TRAY_APP_FILE, regValue))
      {
	trayAppFile = regValue;
      }
    else
      {
	logger.LogWarning ("Failed to get tray app file from registry");
      }

    if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, installOptions.registryKeyPath, REGISTRY_VALUE_NAME_TRAY_APP_LINK_FILE,
					    regValue))
      {
	trayAppLinkFile = regValue;
      }
    else
      {
	logger.LogWarning ("Failed to get tray app link file from registry");
      }

    if (!trayAppFile.empty())
      {
	logger.LogInfo ("Attempting to kill tray app: " + trayAppFile);
	SystemUtil::KillProcessByName (trayAppFile);
	std::this_thread::sleep_for (std::chrono::seconds (1));
      }

    installer.UnregisterTrayApp();

    if (!installPath.empty())
      {
	logger.LogInfo ("Cleaning up residual files in: " + installPath);
	try
	  {
	    std::filesystem::path tarGz = std::filesystem::path (installPath) / cubridImageFile;
	    if (std::filesystem::exists (tarGz))
	      {
		std::filesystem::remove (tarGz);
		logger.LogInfo ("Removed " + cubridImageFile);
	      }

	    std::filesystem::path extractedTar = std::filesystem::path (installPath) / CUB_WSL_EXTRACT_IMAGE_FILE;
	    if (std::filesystem::exists (extractedTar))
	      {
		std::filesystem::remove (extractedTar);
		logger.LogInfo ("Removed extracted tar file.");
	      }
	  }
	catch (const std::exception &e)
	  {
	    logger.LogWarning ("Error during cleanup: " + std::string (e.what()));
	  }
      }

    std::string desktopDir = "";
    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, installOptions.registryKeyPath, REGISTRY_VALUE_NAME_USERS_DESKTOP_FOLDER,
	desktopDir))
      {
	logger.LogError ("Failed to get Desktop directory from registry");
      }
    else
      {
	std::filesystem::path trayAppLink = desktopDir + "\\" + trayAppLinkFile;
	if (std::filesystem::exists (trayAppLink))
	  {
	    std::filesystem::remove (trayAppLink);
	    logger.LogInfo ("Removed " + trayAppLinkFile);
	  }

	std::filesystem::path wslShortcut = desktopDir + "\\" + (wslName + ".lnk");
	if (std::filesystem::exists (wslShortcut))
	  {
	    std::filesystem::remove (wslShortcut);
	    logger.LogInfo ("Removed " + wslName + ".lnk");
	  }
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall EnableWSLFeatures (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Enabling WSL features...");

    CUBRIDInstaller installer;
    char buffer[MAX_PATH * 4];
    DWORD bufSize = sizeof (buffer);
    InstallOptions options;

    if (MsiGetPropertyA (hInstall, "CustomActionData", buffer, &bufSize) == ERROR_SUCCESS)
      {
	std::string data = buffer;
	if (!CUBRIDInstaller::ParseInstallOptions (data, options))
	  {
	    logger.LogError ("Failed to parse install options");
	    return ERROR_INSTALL_FAILURE;
	  }
      }

    logger.LogInfo ("  Virt. Platform Enabled: " + std::to_string (options.virtMachinePlatformEnabled));
    logger.LogInfo ("  WSL Feature Enabled: " + std::to_string (options.windowsSubsystemLinuxEnabled));
    logger.LogInfo ("  WSL2 Mode: " + std::to_string (options.isWSL2Mode));

    int result = installer.EnableWindowsFeatures (hInstall, options);
    logger.LogInfo ("EnableWindowsFeatures result: " + std::to_string (result));
    if (result == TIMEOUT)
      {
	MSIHANDLE hRecord = MsiCreateRecord (1);
	MsiRecordSetInteger (hRecord, 1, TIMEOUT);
	MsiProcessMessage (hInstall, INSTALLMESSAGE_ERROR, hRecord);
	MsiCloseHandle (hRecord);
	return ERROR_INSTALL_FAILURE;
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall InstallWslAndCubrid (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);

    CUBRIDInstaller installer;
    InstallOptions options;

    char buffer[MAX_PATH * 4];
    DWORD bufSize = sizeof (buffer);

    if (MsiGetPropertyA (hInstall, "CustomActionData", buffer, &bufSize) == ERROR_SUCCESS)
      {
	std::string data = buffer;
	if (!CUBRIDInstaller::ParseInstallOptions (data, options))
	  {
	    logger.LogError ("Failed to parse install options");
	    return ERROR_INSTALL_FAILURE;
	  }
      }

    logger.LogInfo ("  WSL2 Mode: " + std::to_string (options.isWSL2Mode));

    if (!options.registryKeyPath.empty())
      {
	logger.LogInfo ("Reading configuration from registry: " + options.registryKeyPath);

	std::string regValue;
	if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, options.registryKeyPath, REGISTRY_VALUE_NAME_INSTALL_DIR, regValue))
	  {
	    options.installPath = regValue;
	  }
	if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, options.registryKeyPath, REGISTRY_VALUE_NAME_WSL_NAME, regValue))
	  {
	    options.wslName = regValue;
	  }
	if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, options.registryKeyPath, REGISTRY_VALUE_NAME_IMAGE_FILE, regValue))
	  {
	    options.cubridImageFile = regValue;
	  }
	if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, options.registryKeyPath, REGISTRY_VALUE_NAME_TRAY_APP_FILE, regValue))
	  {
	    options.trayAppFile = regValue;
	  }
	if (SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, options.registryKeyPath, REGISTRY_VALUE_NAME_TRAY_APP_LINK_FILE, regValue))
	  {
	    options.trayAppLinkFile = regValue;
	  }
      }

    logger.LogInfo ("Parsed Options:");
    logger.LogInfo ("  Install Path: " + options.installPath);
    logger.LogInfo ("  WSL Name: " + options.wslName);

    if (options.installPath.empty())
      {
	logger.LogError ("Install Path is empty!");
	return ERROR_INSTALL_FAILURE;
      }

    std::filesystem::path p (options.cubridImageFile);
    if (p.is_relative())
      {
	std::filesystem::path installedImage = std::filesystem::path (options.installPath) / options.cubridImageFile;
	options.cubridImageFile = installedImage.string();
      }

    if (!installer.InstallWslAndCubrid (options))
      {
	return ERROR_INSTALL_FAILURE;
      }

    std::filesystem::path imagePath (options.cubridImageFile);
    std::filesystem::path installDir (options.installPath);

    std::string imagePathStr = imagePath.string();
    std::string installDirStr = installDir.string();

    if (imagePathStr.find (installDirStr) != std::string::npos && std::filesystem::exists (imagePath))
      {
	logger.LogInfo ("Cleaning up installed image file: " + imagePathStr);
	try
	  {
	    std::filesystem::remove (imagePath);
	  }
	catch (const std::exception &e)
	  {
	    logger.LogWarning ("Failed to remove image file: " + std::string (e.what()));
	  }
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall UpdateInstalledSize (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Updating installed size...");

    std::string installDir;
    std::string productCode;
    std::string bundleCode;
    std::string msiUninstallKey;
    std::string bundleUninstallKey;

    logger.LogInfo ("Reading configuration from registry: " + std::string (REGISTRY_KEY_PATH));

    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_INSTALL_DIR, installDir))
      {
	logger.LogError ("Failed to get InstallDir from registry");
	return ERROR_SUCCESS;
      }

    logger.LogInfo ("Calculating directory size for: " + installDir);
    ULONGLONG installDirSize = 0;
    if (!SystemUtil::CalculateDirectorySize (installDir, installDirSize))
      {
	logger.LogError ("Failed to calculate install directory size (exception while scanning)");
	return ERROR_SUCCESS;
      }
    DWORD estimatedSizeKB = static_cast<DWORD> (installDirSize / 1024);
    logger.LogInfo ("Install directory size: " + std::to_string (installDirSize) + " bytes");
    logger.LogInfo ("Total size: " + std::to_string (installDirSize) + " bytes (" + std::to_string (
			    estimatedSizeKB) + " KB)");

    char msiKeyBuffer[256];
    DWORD msiKeyBufSize = sizeof (msiKeyBuffer);
    if (MsiGetPropertyA (hInstall, "ProductCode", msiKeyBuffer, &msiKeyBufSize) == ERROR_SUCCESS)
      {
	productCode = msiKeyBuffer;
	logger.LogInfo ("ProductCode: " + productCode);
      }
    else
      {
	logger.LogError ("Failed to get ProductCode");
	return ERROR_SUCCESS;
      }

    char bundleKeyBuf[256];
    DWORD bundleKeyLen = sizeof (bundleKeyBuf);
    if (MsiGetPropertyA (hInstall, "BUNDLEPROVIDERKEY", bundleKeyBuf, &bundleKeyLen) == ERROR_SUCCESS)
      {
	bundleCode = bundleKeyBuf;
	logger.LogInfo ("BUNDLEPROVIDERKEY: " + bundleCode);
      }
    else
      {
	logger.LogError ("Failed to get BUNDLEPROVIDERKEY");
      }

    if (!productCode.empty())
      {
	msiUninstallKey = CUBRIDInstaller::MSI_INSTALL_REG_KEY_PATH + "\\" + productCode;
	if (SystemUtil::SetRegistryValueDWORD (HKEY_CURRENT_USER, msiUninstallKey, "EstimatedSize", estimatedSizeKB))
	  {
	    logger.LogInfo ("Successfully updated EstimatedSize in HKCU");
	  }
      }
    if (!bundleCode.empty())
      {
	bundleUninstallKey = CUBRIDInstaller::BUNDLE_INSTALL_REG_KEY_PATH + "\\" + bundleCode;
	if (SystemUtil::SetRegistryValueDWORD (HKEY_LOCAL_MACHINE, bundleUninstallKey, "EstimatedSize", estimatedSizeKB))
	  {
	    logger.LogInfo ("Successfully updated EstimatedSize in HKLM");
	  }
      }

    return ERROR_SUCCESS;
  }

  __declspec (dllexport) UINT __stdcall CreateDemodb (MSIHANDLE hInstall)
  {
    SetupMsiContext (hInstall);
    logger.LogInfo ("Starting Demo Database creation (Async)...");

    std::string wslName = "";
    if (!SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_WSL_NAME, wslName))
      {
	logger.LogError ("Failed to get WSL name from registry");
	return ERROR_SUCCESS;
      }
    logger.LogInfo ("WSL Name: " + wslName);

    std::string scriptContent =
	    "#!/bin/bash\n"
	    ". ~/.cubrid.sh\n"
	    "DEMODB_NAME=demodb\n"
	    "\n"
	    "chown -R cubrid:cubrid \"$CUBRID_DATABASES\"\n"
	    "echo \"Initializing database $DEMODB_NAME...\"\n"
	    "\n"
	    "if [ ! -d \"$CUBRID_DATABASES/$DEMODB_NAME\" ]; then\n"
	    "    echo \"Demo database directory does not exist. Creating it...\" >&2\n"
	    "    mkdir -p \"$CUBRID_DATABASES/$DEMODB_NAME\"\n"
	    "fi\n"
	    "\n"
	    "cd \"$CUBRID_DATABASES/$DEMODB_NAME\"\n"
	    "\n"
	    "if ! cubrid createdb --db-volume-size=100M --log-volume-size=100M $DEMODB_NAME en_US.utf8 > /dev/null 2>&1; then\n"
	    "    echo \"Failed to create demo database ($DEMODB_NAME).\" >&2\n"
	    "    exit 1\n"
	    "fi\n"
	    "\n"
	    "if ! cubrid loaddb -u dba -s $CUBRID/demo/demodb_schema -d $CUBRID/demo/demodb_objects $DEMODB_NAME > /dev/null 2>&1; then\n"
	    "    echo \"Failed to load demo database data.\" >&2\n"
	    "    exit 1\n"
	    "fi\n"
	    "\n"
	    "exit 0\n";

    char tempPath[MAX_PATH];
    GetTempPathA (MAX_PATH, tempPath);
    std::string scriptPath = std::string (tempPath) + "create_demodb.sh";

    std::ofstream scriptFile (scriptPath, std::ios::binary);
    if (!scriptFile.is_open())
      {
	logger.LogError ("Failed to create temporary script file: " + scriptPath);
	return ERROR_SUCCESS;
      }
    scriptFile.write (scriptContent.c_str(), scriptContent.size());
    scriptFile.close();

    logger.LogInfo ("Created temporary script: " + scriptPath);

    std::thread t (CUBRIDInstaller::CreateDemodbWorker, wslName, scriptPath);
    t.detach();

    return ERROR_SUCCESS;
  }
}
