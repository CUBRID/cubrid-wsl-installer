#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include "system_util.h"

struct IWbemServices;

struct EnvironmentCheckResult
{
  bool canInstallWSL;
  std::string osVersion;
  std::string osArchitecture;
  std::string osBuildNumber;
  bool isBiosVirtualizationEnabled;
  bool isWindowsSubsystemLinuxEnabled;
  bool isVirtualMachinePlatformEnabled;
  bool wslInstalled;
  bool wsl2Enabled;
  std::vector<std::string> issues;
  std::vector<std::string> warnings;
  std::string environmentInfo;
};

class EnvironmentUtil
{
  public:
    EnvironmentUtil();
    ~EnvironmentUtil();

    static UINT RunAllEnvironmentChecks (MSIHANDLE hInstall);

  private:
    std::string logDir;
    std::string environmentInfo;

    static bool SetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName, const std::string &value);
    static std::string GetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName);

    static bool CheckSystemVirtualization (IWbemServices *pSvc);
    static bool CheckWindowsBuildNumber (IWbemServices *pSvc, std::string &buildNumber);
    static bool CheckAdministratorRights();
    static bool CheckWindowsOptionalFeature (IWbemServices *pSvc, const std::string &featureName);
    static bool CheckWSLRebootRequired();
    static bool CheckWSLInstalled();
};