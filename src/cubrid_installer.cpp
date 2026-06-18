#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <regex>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

#include "cubrid_installer.h"
#include "logger.h"
#include "system_util.h"

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

const char *CUBRIDInstaller::START_UP_REGISTRY_KEY_PATH = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const std::string CUBRIDInstaller::MSI_INSTALL_REG_KEY_PATH = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
const std::string CUBRIDInstaller::BUNDLE_INSTALL_REG_KEY_PATH =
	"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
static Logger &logger = Logger::GetInstance();

static constexpr DWORD ENABLE_FEATURES_TIMEOUT_MS = 3 * 60 * 1000; // 3 minutes
static constexpr DWORD CREATE_DEMODB_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes
static const char *STARTUP_RUN_VALUE_NAME = "CUBRID_WSL_TrayApp";

namespace
{
constexpr int RPS_LAUNCH_FAILED = -1;
constexpr int RPS_TIMEOUT       = -2;

int RunPowerShellScript (const std::string &tag, const std::string &scriptBody,
                         DWORD timeoutMs, const char *errorActionPreference, bool showWindow)
{
  const char *tempEnv = std::getenv ("TEMP");
  const std::string tempDir = tempEnv ? tempEnv : ".";
  const std::string psLogPath = tempDir + "\\cubrid_ps_" + tag + ".log";
  const std::string tempScriptPath = tempDir + "\\cubrid_ps_" + tag + ".ps1";

  std::string psScript;
  psScript += std::string ("$ErrorActionPreference = '") + errorActionPreference + "'; ";
  psScript += "try { [Console]::TreatControlCAsInput = $true } catch {}; ";
  psScript += "trap [System.Management.Automation.PipelineStoppedException] { continue }; ";
  psScript += "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; ";
  psScript += "Start-Transcript -Path '" + psLogPath + "' -Force; ";
  psScript += scriptBody;
  psScript += " Stop-Transcript; ";

  {
    std::ofstream scriptFile (tempScriptPath);
    scriptFile << psScript;
  }

  std::string command = "powershell.exe -ExecutionPolicy Bypass -File \"" + tempScriptPath + "\"";

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = showWindow ? SW_SHOW : SW_HIDE;
  ZeroMemory (&pi, sizeof (pi));

  if (!CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL,
                       SystemUtil::GetSystemDir().c_str(), &si, &pi))
    {
      logger.LogError ("Failed to start PowerShell process [" + tag + "]. Error: "
                       + std::to_string (GetLastError()));
      return RPS_LAUNCH_FAILED;
    }
  SystemUtil::BlockChildConsoleInput (pi.dwProcessId);

  DWORD waitResult = WaitForSingleObject (pi.hProcess, timeoutMs == 0 ? INFINITE : timeoutMs);
  if (waitResult == WAIT_TIMEOUT)
    {
      logger.LogError ("PowerShell process [" + tag + "] timed out after "
                       + std::to_string (timeoutMs) + " ms. Terminating.");
      SystemUtil::ExecuteCommandWithoutResult ("taskkill /F /T /PID " + std::to_string (pi.dwProcessId));
      TerminateProcess (pi.hProcess, 1);
      WaitForSingleObject (pi.hProcess, 5000);
      CloseHandle (pi.hProcess);
      CloseHandle (pi.hThread);
      DeleteFileA (tempScriptPath.c_str());
      return RPS_TIMEOUT;
    }

  DWORD exitCode = 0;
  GetExitCodeProcess (pi.hProcess, &exitCode);
  CloseHandle (pi.hProcess);
  CloseHandle (pi.hThread);
  DeleteFileA (tempScriptPath.c_str());

  std::ifstream logFile (psLogPath);
  if (logFile.is_open())
    {
      std::string line;
      logger.LogInfo ("--- PowerShell [" + tag + "] Output Begin ---");
      while (std::getline (logFile, line))
        {
          logger.LogInfo (line);
        }
      logger.LogInfo ("--- PowerShell [" + tag + "] Output End ---");
      logFile.close();
      DeleteFileA (psLogPath.c_str());
    }

  return (int) exitCode;
}

std::string GetImportMarkerPath (const std::string &wslName)
{
  const char *tempEnv = std::getenv ("TEMP");
  const std::string tempDir = tempEnv ? tempEnv : ".";
  return tempDir + "\\cubrid_wsl_import_" + wslName + ".marker";
}
} // namespace

CUBRIDInstaller::CUBRIDInstaller()
{
}

CUBRIDInstaller::~CUBRIDInstaller()
{
}

bool CUBRIDInstaller::ExtractCubridImage (const std::string &imageFile, const std::string &targetPath)
{
  logger.LogInfo ("Extracting CUBRID image from " + imageFile + " to " + targetPath);
  std::filesystem::path targetDir (targetPath);
  std::filesystem::path destFile = targetDir / CUB_WSL_EXTRACT_IMAGE_FILE;

  if (!std::filesystem::exists (imageFile))
    {
      logger.LogError ("Source image file does not exist: " + imageFile);
      return false;
    }

  std::string cleanTargetPath = targetPath;
  if (!cleanTargetPath.empty() && cleanTargetPath.back() == '\\')
    {
      cleanTargetPath.pop_back();
    }

  std::string tarPath = SystemUtil::GetSystemDir() + "\\tar.exe";

  std::string command = "\"" + tarPath + "\" -xzf \"" + imageFile + "\" -C \"" + cleanTargetPath + "\"";
  logger.LogInfo ("Executing command: " + command);

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  ZeroMemory (&pi, sizeof (pi));

  if (!CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
      logger.LogError ("Failed to start tar process. Error: " + std::to_string (GetLastError()));
      return false;
    }

  WaitForSingleObject (pi.hProcess, INFINITE);
  DWORD exitCode;
  GetExitCodeProcess (pi.hProcess, &exitCode);
  CloseHandle (pi.hProcess);
  CloseHandle (pi.hThread);

  if (exitCode != 0)
    {
      logger.LogError ("Failed to extract image file. Exit code: " + std::to_string (exitCode));
      return false;
    }

  if (!std::filesystem::exists (destFile))
    {
      logger.LogError ("Extracted file not found: " + destFile.string());
      return false;
    }

  logger.LogInfo ("Image file extracted successfully.");
  return true;
}

void CUBRIDInstaller::CleanUpFile (const std::string &filePath)
{
  logger.LogInfo ("CleanUpFile: " + filePath);
  std::filesystem::path targetFile (filePath);
  if (std::filesystem::exists (targetFile))
    {
      logger.LogInfo ("Cleaning up file: " + targetFile.string());
      try
	{
	  std::filesystem::remove (targetFile);
	}
      catch (const std::filesystem::filesystem_error &e)
	{
	  logger.LogError ("Failed to cleanup file: " + std::string (e.what()));
	}
    }
}

int CUBRIDInstaller::EnableWindowsFeatures (MSIHANDLE hInstall, const InstallOptions &options)
{
  logger.LogInfo ("Enabling Windows Features...");
  if (options.windowsSubsystemLinuxEnabled && options.virtMachinePlatformEnabled)
    {
      logger.LogInfo ("No Windows Features to enable.");
      return NONE_ERROR;
    }

  std::string body;
  if (!options.windowsSubsystemLinuxEnabled)
    {
      body += "Write-Host 'Enabling Windows Subsystem for Linux...'; ";
      body += "dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart; ";
    }
  if (options.isWSL2Mode && !options.virtMachinePlatformEnabled)
    {
      body += "Write-Host 'Enabling Virtual Machine Platform...'; ";
      body += "dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart; ";
    }
  body += "Start-Sleep -Seconds 3; ";

  int rc = RunPowerShellScript ("features", body, 1000, "Stop", true);
  if (rc == RPS_TIMEOUT)
    {
      return TIMEOUT;
    }
  if (rc == RPS_LAUNCH_FAILED)
    {
      return OTHER_ERROR;
    }
  return rc == 0 ? NONE_ERROR : OTHER_ERROR;
}

bool CUBRIDInstaller::SetupWslDistro (const InstallOptions &options)
{
  logger.LogInfo ("Setting up CUBRID WSL Distro...");
  logger.LogInfo ("WSL Name: " + options.wslName);
  logger.LogInfo ("Install Path: " + options.installPath);

  std::string cleanInstallPath = options.installPath;
  if (!cleanInstallPath.empty() && cleanInstallPath.back() == '\\')
    {
      cleanInstallPath.pop_back();
    }

  std::filesystem::path installDir (cleanInstallPath);
  std::filesystem::path imagePath = installDir / CUB_WSL_EXTRACT_IMAGE_FILE;

  std::string body;
  if (options.isWSL2Mode)
    {
      body += "Write-Host 'Setting Default WSL Version to 2...'; ";
      body += "wsl --set-default-version 2; ";
    }
  else
    {
      body += "Write-Host 'Setting Default WSL Version to 1...'; ";
      body += "wsl --set-default-version 1; ";
    }
  body += "Write-Host 'Importing CUBRID WSL Distro (" + options.wslName + ")...'; ";
  body += "wsl --import " + options.wslName + " '" + cleanInstallPath + "' '" + imagePath.string() + "'; ";
  body += "if ($?) { Write-Host 'WSL Import Successful.' } else { Write-Host 'WSL Import Failed. '; Start-Sleep -Seconds 5; exit 1 }; ";
  body += "Start-Sleep -Seconds 5; ";

  int rc = RunPowerShellScript ("import", body, 0, "Stop", true);
  return rc == 0;
}

bool CUBRIDInstaller::UninstallWsl (const std::string &wslName)
{
  logger.LogInfo ("Uninstalling WSL Distro: " + wslName);

  std::string body;
  body += "Write-Host 'Unregistering WSL Distro (" + wslName + ")...'; ";
  body += "wsl --unregister " + wslName + "; Start-Sleep -Seconds 7; ";

  RunPowerShellScript ("uninstall", body, 0, "SilentlyContinue", true);
  return true;
}

void CUBRIDInstaller::ClearImportMarker (const std::string &wslName)
{
  std::error_code ec;
  std::filesystem::remove (GetImportMarkerPath (wslName), ec);
}

void CUBRIDInstaller::MarkImportSucceeded (const std::string &wslName)
{
  std::ofstream marker (GetImportMarkerPath (wslName));
  marker << "1";
}

bool CUBRIDInstaller::WasImportSucceeded (const std::string &wslName)
{
  std::error_code ec;
  return std::filesystem::exists (GetImportMarkerPath (wslName), ec);
}

bool CUBRIDInstaller::InstallWslAndCubrid (const InstallOptions &options)
{
  logger.LogInfo ("InstallWslAndCubrid...");

  ClearImportMarker (options.wslName);

  std::filesystem::path installDir (options.installPath);
  std::filesystem::path vhdxPath = installDir / "ext4.vhdx";

  if (std::filesystem::exists (vhdxPath))
    {
      logger.LogError ("WSL data file (ext4.vhdx) already exists at " + vhdxPath.string());
      return false;
    }

  try
    {
      std::filesystem::create_directories (options.installPath);
    }
  catch (const std::exception &e)
    {
      logger.LogError ("Failed to create install directory: " + std::string (e.what()));
      return false;
    }

  if (!ExtractCubridImage (options.cubridImageFile, options.installPath))
    {
      return false;
    }
  if (!SetupWslDistro (options))
    {
      logger.LogInfo ("Cleaning up extracted image file 11: " + options.installPath + "\\" + CUB_WSL_EXTRACT_IMAGE_FILE);
      logger.LogInfo ("Cleaning up extracted image file 11: " + options.cubridImageFile);
      CleanUpFile (options.cubridImageFile);
      CleanUpFile (options.installPath + "\\" + CUB_WSL_EXTRACT_IMAGE_FILE);
      return false;
    }

  MarkImportSucceeded (options.wslName);

  logger.LogInfo ("Cleaning up extracted image file : " + options.installPath + "\\" + CUB_WSL_EXTRACT_IMAGE_FILE);
  logger.LogInfo ("Cleaning up extracted image file : " + options.cubridImageFile);
  CleanUpFile (options.cubridImageFile);
  CleanUpFile (options.installPath + "\\" + CUB_WSL_EXTRACT_IMAGE_FILE);
  return true;
}

bool CUBRIDInstaller::RegisterTrayApp (const std::string &trayAppPath)
{
  logger.LogInfo ("Registering Tray Application to Windows Startup: " + trayAppPath);

  if (!std::filesystem::exists (trayAppPath))
    {
      logger.LogError ("Tray application not found at: " + trayAppPath);
      return false;
    }

  std::string regValue = "\"" + trayAppPath + "\"";
  if (SystemUtil::SetRegistryValueString (HKEY_CURRENT_USER, START_UP_REGISTRY_KEY_PATH,
                                          STARTUP_RUN_VALUE_NAME, regValue))
    {
      logger.LogInfo ("Successfully registered tray application to Windows Startup: " + regValue);
      return true;
    }

  logger.LogError ("Failed to register tray application to Windows Startup.");
  return false;
}

bool CUBRIDInstaller::UnregisterTrayApp()
{
  logger.LogInfo ("Unregistering Tray Application from Windows Startup");

  if (SystemUtil::DeleteRegistryValue (HKEY_CURRENT_USER, START_UP_REGISTRY_KEY_PATH,
                                       STARTUP_RUN_VALUE_NAME))
    {
      logger.LogInfo ("Successfully unregistered tray application from Windows Startup");
      return true;
    }

  logger.LogWarning ("Failed to unregister tray application from Windows Startup");
  return false;
}

std::string CUBRIDInstaller::GetWslPath()
{
  std::vector<std::string> wslPaths =
  {
    "C:\\Windows\\System32",
    "C:\\Windows\\SysWOW64"
  };

  for (const auto &path : wslPaths)
    {
      if (std::filesystem::exists (path + "\\wsl.exe"))
	{
	  return path;
	}
    }
  return "";
}

bool CUBRIDInstaller::CreateShortcut (const std::string &shortcutPath, const std::string &exePath,
				      const std::string &arguments, const std::string &workingDir, const std::string &iconPath)
{
  logger.LogInfo ("Creating Shortcut: " + shortcutPath);
  HRESULT hr = CoInitialize (NULL);
  bool comInitialized = SUCCEEDED (hr);

  if (FAILED (hr) && hr != RPC_E_CHANGED_MODE)
    {
      logger.LogError ("Failed to initialize COM. HRESULT: " + std::to_string (hr));
      return false;
    }

  IShellLinkA *pShellLink = NULL;
  hr = CoCreateInstance (CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkA, (LPVOID *)&pShellLink);

  if (SUCCEEDED (hr))
    {
      pShellLink->SetPath (exePath.c_str());

      if (!arguments.empty())
	{
	  pShellLink->SetArguments (arguments.c_str());
	}

      if (!workingDir.empty())
	{
	  pShellLink->SetWorkingDirectory (workingDir.c_str());
	}

      if (!iconPath.empty())
	{
	  pShellLink->SetIconLocation (iconPath.c_str(), 0);
	}

      IPersistFile *pPersistFile = NULL;
      hr = pShellLink->QueryInterface (IID_IPersistFile, (LPVOID *)&pPersistFile);

      if (SUCCEEDED (hr))
	{
	  int nwLen = MultiByteToWideChar (CP_ACP, 0, shortcutPath.c_str(), -1, NULL, 0);
	  wchar_t *wPath = new wchar_t[nwLen];
	  MultiByteToWideChar (CP_ACP, 0, shortcutPath.c_str(), -1, wPath, nwLen);

	  hr = pPersistFile->Save (wPath, TRUE);

	  delete[] wPath;
	  pPersistFile->Release();
	}

      pShellLink->Release();
    }

  if (comInitialized)
    {
      CoUninitialize();
    }

  if (FAILED (hr))
    {
      logger.LogError ("Failed to create shortcut. HRESULT: " + std::to_string (hr));
      return false;
    }

  return true;
}

bool CUBRIDInstaller::StartWSL (const std::string &wslName)
{
  logger.LogInfo ("Starting WSL: " + wslName);
  std::string command = "wsl -d " + wslName + " -u cubrid";
  bool result = SystemUtil::ExecuteCommandWithoutResult (command);
  if (!result)
    {
      logger.LogError ("Failed to start WSL");
      return false;
    }
  logger.LogInfo ("WSL started successfully");
  return true;
}

bool CUBRIDInstaller::ParseInstallOptions (const std::string &installOptions, InstallOptions &outOptions)
{
  std::string data = installOptions;

  logger.LogInfo ("ParseInstallOptions: " + data);
  if (data.empty())
    {
      logger.LogError ("Install options string is empty.");
      return false;
    }

  std::istringstream iss (data);
  std::string token;

  while (std::getline (iss, token, ';'))
    {
      size_t pos = token.find ('=');
      if (pos != std::string::npos)
	{
	  std::string key = token.substr (0, pos);
	  std::string value = token.substr (pos + 1);

	  key.erase (0, key.find_first_not_of (" \t\n\r\f\v"));
	  key.erase (key.find_last_not_of (" \t\n\r\f\v") + 1);
	  value.erase (0, value.find_first_not_of (" \t\n\r\f\v"));
	  value.erase (value.find_last_not_of (" \t\n\r\f\v") + 1);

	  if (key == "REGISTRY_KEY")
	    {
	      outOptions.registryKeyPath = value;
	    }
	  else if (key == "CUB_VIRT_MACHINE_PLATFORM_ENABLED")
	    {
	      outOptions.virtMachinePlatformEnabled = ( value == "1" );
	    }
	  else if (key == "CUB_WINDOWS_SUBSYSTEM_LINUX")
	    {
	      outOptions.windowsSubsystemLinuxEnabled = ( value == "1" );
	    }
	  else if (key == "IS_WSL2_MODE")
	    {
	      outOptions.isWSL2Mode = ( value == "1" );
	    }
	  else if (key == "IS_ROLLBACK")
	    {
	      outOptions.isRollback = ( value == "1" );
	    }
	}
    }

  return true;
}

void CUBRIDInstaller::CreateDemodbWorker (std::string wslName, std::string scriptPath)
{
  const std::string envSetup = ". ~/.cubrid.sh; ";
  std::string cmd = "wsl -d " + wslName + " -u cubrid --exec bash -c \"" + envSetup +
		    "bash \\\"$(wslpath '" + scriptPath + "')\\\" </dev/null\"";
  logger.LogInfo ("CreateDemodbWorker Executing command (async worker): " + cmd);

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  ZeroMemory (&pi, sizeof (pi));

  std::vector<char> cmdVec (cmd.begin(), cmd.end());
  cmdVec.push_back (0);

  bool success = false;
  DWORD exitCode = 1;

  if (CreateProcessA (NULL, cmdVec.data(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL,
		      SystemUtil::GetSystemDir().c_str(), &si, &pi))
    {
      DWORD waitResult = WaitForSingleObject (pi.hProcess, CREATE_DEMODB_TIMEOUT_MS);
      if (waitResult == WAIT_TIMEOUT)
	{
	  logger.LogError ("Demodb creation timed out after 10 minutes. Terminating WSL process.");
	  TerminateProcess (pi.hProcess, 1);
	  WaitForSingleObject (pi.hProcess, 5000);
	  exitCode = 1;
	}
      else
	{
	  GetExitCodeProcess (pi.hProcess, &exitCode);
	}
      CloseHandle (pi.hProcess);
      CloseHandle (pi.hThread);

      if (exitCode == 0)
	{
	  success = true;
	}
    }
  else
    {
      logger.LogError ("Failed to start WSL process. Error: " + std::to_string (GetLastError()));
    }

  std::filesystem::remove (scriptPath);

  if (success)
    {
      logger.LogInfo ("Demo database created successfully.");
    }
  else
    {
      std::string msg = "Failed to create demo database. (Exit Code: " + std::to_string (exitCode) + ")";
      logger.LogError (msg);
      MessageBoxA (NULL, msg.c_str(), "CUBRID Installation Warning", MB_OK | MB_ICONWARNING);
    }
}
