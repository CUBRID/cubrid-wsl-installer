#pragma once
#include <string>
#include <windows.h>
#include <msi.h>
#include <msiquery.h>

class SystemUtil
{
  public:
    static std::string ExecuteCommand (const std::string &command);
    static std::string ExecuteCommandWithTimeout (const std::string &command, DWORD timeoutMs = 5000);
    static std::string ExecutePowerShellCommand (const std::string &command);
    static std::string ExecuteCommandWithPopen (const std::string &command);
    static bool ExecuteCommandWithoutResult (const std::string &command);
    static bool ExecuteCommandWithoutResultWithTimeout (const std::string &command, DWORD timeoutMs = 10000);
    static HANDLE ExecuteCommandWithOutResultAsync (const std::string &command);
    static bool IsCommandAvailable (const std::string &command);

    static const std::string &GetSystemDir();

    static bool CheckRegistryKeyExists (const HKEY rootKey, const std::string &keyPath);
    static bool CheckRegistryValueExists (const HKEY rootKey, const std::string &keyPath, const std::string &valueName);
    static bool GetRegistryValueString (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
					std::string &outValue);
    static bool GetRegistryValueDWORD (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
				       DWORD &outValue);
    static bool SetRegistryValueString (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
					const std::string &value);
    static bool SetRegistryValueDWORD (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
				       DWORD value);
    static bool SetRegistryValueBoolean (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
					 bool value);
    static bool DeleteRegistryValue (const HKEY rootKey, const std::string &keyPath,
                                     const std::string &valueName);

    static void KillProcessByName (const std::string &processName);
    static bool CalculateDirectorySize (const std::string &dirPath, ULONGLONG &outSize);

    static void BlockChildConsoleInput (DWORD pid);

    static bool SetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName, const std::string &value);
    static std::string GetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName);
};

