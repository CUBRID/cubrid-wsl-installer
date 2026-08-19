#pragma once
#include <string>
#include <vector>
#include <functional>
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include "config.h"

#define REGISTRY_KEY_PATH CUB_REGISTRY_KEY_PATH

struct InstallOptions
{
  std::string wslName;
  std::string cubridImageFile;
  std::string cubridExtractImageFile;
  std::string trayAppFile;
  std::string trayAppLinkFile;
  std::string installPath;
  bool virtMachinePlatformEnabled = false;
  bool windowsSubsystemLinuxEnabled = false;
  bool isWSL2Mode = true;
  bool isRollback = false;
  std::string registryKeyPath;
};

enum FeatureInstallResult
{
  NONE_ERROR = 0,
  TIMEOUT = 30000,
  OTHER_ERROR = 30001,
};

class CUBRIDInstaller
{
  public:
    static const char *START_UP_REGISTRY_KEY_PATH;
    static const char *STARTUP_APPROVED_REGISTRY_KEY_PATH;
    static const std::string MSI_INSTALL_REG_KEY_PATH;
    static const std::string BUNDLE_INSTALL_REG_KEY_PATH;

    CUBRIDInstaller();
    ~CUBRIDInstaller();

    int EnableWindowsFeatures (MSIHANDLE hInstall, const InstallOptions &options);
    bool InstallWslAndCubrid (const InstallOptions &options);
    bool UninstallWsl (const std::string &wslName);
    bool RegisterTrayApp (const std::string &trayAppPath);
    bool UnregisterTrayApp();
    bool RegisterStarterApp (const std::string &starterAppPath);
    bool UnregisterStarterApp();
    bool CreateShortcut (const std::string &shortcutPath, const std::string &exePath, const std::string &arguments,
			 const std::string &workingDir, const std::string &iconPath);
    std::string GetWslPath();
    bool StartWSL (const std::string &wslName);

    static bool ParseInstallOptions (const std::string &installOptions, InstallOptions &outOptions);
    static void CreateDemodbWorker (std::string wslName, std::string scriptPath);

    static void ClearImportMarker (const std::string &wslName);
    static void MarkImportSucceeded (const std::string &wslName);
    static bool WasImportSucceeded (const std::string &wslName);

  private:
    void ClearStartupApprovedFlag (const char *valueName);
    bool RegisterStartupEntry (const char *valueName, const std::string &exePath, const char *description);
    bool UnregisterStartupEntry (const char *valueName, const char *description);
    bool ExtractCubridImage (const std::string &imageFile, const std::string &targetPath);
    bool SetupWslDistro (const InstallOptions &options);
    void CleanUpFile (const std::string &filePath);
    void LogTranscript (const std::string &logPath, const std::string &header);
};