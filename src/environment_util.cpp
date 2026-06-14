#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <shlobj.h>
#include <shellapi.h>
#include <intrin.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>

#include "environment_util.h"
#include "logger.h"

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "wbemuuid.lib")

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

EnvironmentUtil::EnvironmentUtil()
{
}

EnvironmentUtil::~EnvironmentUtil()
{
}

bool EnvironmentUtil::SetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName, const std::string &value)
{
  return SystemUtil::SetMsiProperty (hInstall, propertyName, value);
}

std::string EnvironmentUtil::GetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName)
{
  return SystemUtil::GetMsiProperty (hInstall, propertyName);
}

bool EnvironmentUtil::CheckWindowsFeature (const std::string &featureName)
{
  std::string command = "powershell -Command \"Get-WindowsOptionalFeature -Online -FeatureName " + featureName +
			" | Select-Object -ExpandProperty State\"";
  std::string result = SystemUtil::ExecutePowerShellCommand (command);

  return result.find ("Enabled") != std::string::npos;
}

bool EnvironmentUtil::CheckWSLInstalled()
{
  LPCSTR registryKeyPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Lxss\\MSI";

  return SystemUtil::CheckRegistryValueExists (HKEY_LOCAL_MACHINE, registryKeyPath, "ProductCode");
}

bool EnvironmentUtil::CheckWSLRebootRequired()
{
  LPCSTR registryKeyPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending";
  return SystemUtil::CheckRegistryKeyExists (HKEY_LOCAL_MACHINE, registryKeyPath);
}

bool EnvironmentUtil::CheckSystemVirtualization (IWbemServices *pSvc)
{
  bool systemVirtEnabled = false;
  bool biosVirtualizationEnabled = false;
  bool hypervisorPresent = false;
  IEnumWbemClassObject *pEnumerator = NULL;
  HRESULT hres = S_OK;

  hres = pSvc->ExecQuery (bstr_t ("WQL"),
			  bstr_t ("SELECT VirtualizationFirmwareEnabled FROM Win32_Processor"),
			  WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

  if (SUCCEEDED (hres))
    {
      IWbemClassObject *pclsObj = NULL;
      ULONG uReturn = 0;

      while (pEnumerator)
	{
	  HRESULT hr = pEnumerator->Next (WBEM_INFINITE, 1, &pclsObj, &uReturn);
	  if (0 == uReturn)
	    {
	      break;
	    }

	  VARIANT vtVirtEnabled;
	  VariantInit (&vtVirtEnabled);

	  hr = pclsObj->Get (L"VirtualizationFirmwareEnabled", 0, &vtVirtEnabled, 0, 0);
	  if (SUCCEEDED (hr) && vtVirtEnabled.vt == VT_BOOL)
	    {
	      if (vtVirtEnabled.boolVal == VARIANT_TRUE)
		{
		  biosVirtualizationEnabled = true;
		}
	    }

	  VariantClear (&vtVirtEnabled);
	  pclsObj->Release();
	}
      pEnumerator->Release();
    }

  pEnumerator = NULL;
  hres = pSvc->ExecQuery (bstr_t ("WQL"),
			  bstr_t ("SELECT HypervisorPresent FROM Win32_ComputerSystem"),
			  WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

  if (SUCCEEDED (hres))
    {
      IWbemClassObject *pclsObj = NULL;
      ULONG uReturn = 0;

      while (pEnumerator)
	{
	  HRESULT hr = pEnumerator->Next (WBEM_INFINITE, 1, &pclsObj, &uReturn);
	  if (0 == uReturn)
	    {
	      break;
	    }

	  VARIANT vtHypervisorPresent;
	  VariantInit (&vtHypervisorPresent);

	  hr = pclsObj->Get (L"HypervisorPresent", 0, &vtHypervisorPresent, 0, 0);
	  if (SUCCEEDED (hr) && vtHypervisorPresent.vt == VT_BOOL)
	    {
	      if (vtHypervisorPresent.boolVal == VARIANT_TRUE)
		{
		  hypervisorPresent = true;
		}
	    }

	  VariantClear (&vtHypervisorPresent);
	  pclsObj->Release();
	  break;
	}
      pEnumerator->Release();
    }

  systemVirtEnabled = biosVirtualizationEnabled || hypervisorPresent;
  return systemVirtEnabled;
}

bool EnvironmentUtil::CheckWindowsBuildNumber (IWbemServices *pSvc, std::string &buildNumber)
{
  HRESULT hres = S_OK;
  bool windowsVersionOK = false;
  IEnumWbemClassObject *pEnumerator = NULL;
  hres = pSvc->ExecQuery (bstr_t ("WQL"),
			  bstr_t ("SELECT BuildNumber FROM Win32_OperatingSystem"),
			  WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

  if (SUCCEEDED (hres))
    {
      IWbemClassObject *pclsObj = NULL;
      ULONG uReturn = 0;

      while (pEnumerator)
	{
	  HRESULT hr = pEnumerator->Next (WBEM_INFINITE, 1, &pclsObj, &uReturn);
	  if (0 == uReturn)
	    {
	      break;
	    }

	  VARIANT vtBuildNumber;
	  VariantInit (&vtBuildNumber);

	  hr = pclsObj->Get (L"BuildNumber", 0, &vtBuildNumber, 0, 0);
	  if (SUCCEEDED (hr) && vtBuildNumber.vt == VT_BSTR)
	    {
	      buildNumber = _com_util::ConvertBSTRToString (vtBuildNumber.bstrVal);
	      int buildNum = std::stoi (buildNumber);
	      windowsVersionOK = (buildNum >= 19041);
	    }

	  VariantClear (&vtBuildNumber);
	  pclsObj->Release();
	  break;
	}
      pEnumerator->Release();
    }

  return windowsVersionOK;
}

bool EnvironmentUtil::CheckAdministratorRights()
{
  BOOL isAdmin = FALSE;
  PSID administratorsGroup = NULL;
  SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;

  if (AllocateAndInitializeSid (
	      &NtAuthority,
	      2,
	      SECURITY_BUILTIN_DOMAIN_RID,
	      DOMAIN_ALIAS_RID_ADMINS,
	      0, 0, 0, 0, 0, 0,
	      &administratorsGroup))
    {

      if (!CheckTokenMembership (NULL, administratorsGroup, &isAdmin))
	{
	  isAdmin = FALSE;
	}
      FreeSid (administratorsGroup);
    }

  return isAdmin == TRUE;
}

bool EnvironmentUtil::CheckWindowsOptionalFeature ( IWbemServices *pSvc, const std::string &featureName)
{
  HRESULT hres = S_OK;
  IEnumWbemClassObject *pEnumerator = NULL;
  IWbemClassObject *pclsObj = NULL;
  ULONG uReturn = 0;
  bool featureEnabled = false;

  hres = pSvc->ExecQuery (
		 bstr_t ("WQL"),
		 bstr_t ("SELECT Name, InstallState FROM Win32_OptionalFeature"),
		 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		 NULL,
		 &pEnumerator);

  if (FAILED (hres))
    {
      return false;
    }

  std::string targetName = featureName;
  std::transform (targetName.begin(), targetName.end(), targetName.begin(), ::tolower);

  while (pEnumerator)
    {
      HRESULT hr = pEnumerator->Next (WBEM_INFINITE, 1, &pclsObj, &uReturn);

      if (0 == uReturn)
	{
	  break;
	}

      VARIANT vtName;
      VARIANT vtState;
      VariantInit (&vtName);
      VariantInit (&vtState);

      hr = pclsObj->Get (L"Name", 0, &vtName, 0, 0);
      if (SUCCEEDED (hr) && vtName.vt == VT_BSTR)
	{
	  std::string currentName = _com_util::ConvertBSTRToString (vtName.bstrVal);
	  std::transform (currentName.begin(), currentName.end(), currentName.begin(), ::tolower);

	  if (currentName == targetName)
	    {
	      hr = pclsObj->Get (L"InstallState", 0, &vtState, 0, 0);
	      if (SUCCEEDED (hr))
		{
		  if (vtState.vt == VT_I4 && vtState.lVal == 1)
		    {
		      featureEnabled = true;
		    }
		}
	      if (vtState.vt != VT_EMPTY)
		{
		  VariantClear (&vtState);
		}
	      VariantClear (&vtName);
	      pclsObj->Release();
	      break;
	    }
	}

      VariantClear (&vtName);
      if (vtState.vt != VT_EMPTY)
	{
	  VariantClear (&vtState);
	}
      pclsObj->Release();
    }

  pEnumerator->Release();
  return featureEnabled;
}

UINT EnvironmentUtil::RunAllEnvironmentChecks (MSIHANDLE hInstall)
{
  std::string result = "============== Environment Check ==============\r\n";

  bool comInitialized = false;

  IWbemLocator  *pLoc = NULL;
  IWbemServices *pSvc = NULL;

  try
    {
      HRESULT hres = CoInitializeEx (0, COINIT_MULTITHREADED);
      if (FAILED (hres))
	{
	  SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", "ERROR: Failed to initialize COM");
	  return ERROR_INSTALL_FAILURE;
	}
      comInitialized = true;

      hres = CoInitializeSecurity (
		     NULL,                        // Security descriptor
		     -1,                          // COM authentication
		     NULL,                        // Authentication services
		     NULL,                        // Reserved
		     RPC_C_AUTHN_LEVEL_CONNECT,   // Default authentication level for proxies
		     RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation level for proxies
		     NULL,                        // Authentication info
		     EOAC_NONE,                   // Additional capabilities
		     NULL                         // Reserved
	     );

      if (FAILED (hres) && hres != RPC_E_TOO_LATE)
	{
	  CoUninitialize();
	  SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", "ERROR: Failed to initialize COM security");
	  return ERROR_INSTALL_FAILURE;
	}

      hres = CoCreateInstance (CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc);
      if (FAILED (hres))
	{
	  CoUninitialize();
	  SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", "ERROR: Failed to create WMI locator");
	  return ERROR_INSTALL_FAILURE;
	}

      hres = pLoc->ConnectServer (_bstr_t (L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
      if (FAILED (hres))
	{
	  pLoc->Release();
	  CoUninitialize();
	  SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", "ERROR: Failed to connect to WMI");
	  return ERROR_INSTALL_FAILURE;
	}

      hres = CoSetProxyBlanket (pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
				RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
      if (FAILED (hres))
	{
	  pSvc->Release();
	  pLoc->Release();
	  CoUninitialize();
	  SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", "ERROR: Failed to set WMI security");
	  return ERROR_INSTALL_FAILURE;
	}

      bool windowsVersionOK = false;
      std::string buildNumber = "";
      windowsVersionOK = CheckWindowsBuildNumber (pSvc, buildNumber);

      if (windowsVersionOK)
	{
	  result += "Windows Build Number: OK (Build 19041+ / Win10 2004+ or Win11)\r\n";
	}
      else
	{
	  result += "Windows Build Number: FAIL (Requires Build 19041+)\r\n";
	}

      SetMsiProperty (hInstall, "CUB_WINDOWS_VERSION_OK", windowsVersionOK ? "1" : "0");
      SetMsiProperty (hInstall, "CUB_WINDOWS_BUILD", buildNumber);

      std::string privileged = GetMsiProperty (hInstall, "Privileged");
      if (privileged == "1")
	{
	  bool adminRights = CheckAdministratorRights();
	  SetMsiProperty (hInstall, "CUB_ADMIN_RIGHTS_GRANTED", adminRights ? "1" : "0");
	  if (adminRights)
	    {
	      result += "Administrator Rights: OK (Yes)\r\n";
	    }
	  else
	    {
	      result += "Administrator Rights: FAIL (No)\r\n";
	    }
	}
      else
	{
	  SetMsiProperty (hInstall, "CUB_ADMIN_RIGHTS_GRANTED", "0");
	  result += "Administrator Rights: FAIL (No)\r\n";
	}

      bool systemVirtEnabled = CheckSystemVirtualization (pSvc);

      if (systemVirtEnabled)
	{
	  result += "BIOS CPU Virtualization: OK\r\n";
	}
      else
	{
	  result += "BIOS CPU Virtualization: FAIL (Disabled)\r\n";
	}

      SetMsiProperty (hInstall, "CUB_BIOS_VIRT_ENABLED", systemVirtEnabled ? "1" : "0");

      bool systemReady = windowsVersionOK && systemVirtEnabled;
      SetMsiProperty (hInstall, "CUB_SYSTEM_READY", systemReady ? "1" : "0");

      bool wslInstalled = CheckWSLInstalled();
      SetMsiProperty (hInstall, "CUB_WSL_INSTALLED", wslInstalled ? "1" : "0");
      if (wslInstalled)
	{
	  result += "WSL Installed: OK\r\n";
	}
      else
	{
	  result += "WSL Installed: FAIL (No)\r\n";
	}

      bool wslRebootRequired = CheckWSLRebootRequired();
      SetMsiProperty (hInstall, "CUB_WSL_REBOOT_REQUIRED", wslRebootRequired ? "1" : "0");
      if (wslRebootRequired)
	{
	  result += "System Reboot Required : WARN (Yes)\r\n";
	}
      else
	{
	  result += "System Reboot Required : OK\r\n";
	}

      bool vmPlatformEnabled = CheckWindowsOptionalFeature (pSvc, "VirtualMachinePlatform");
      SetMsiProperty (hInstall, "CUB_VIRT_MACHINE_PLATFORM_ENABLED", vmPlatformEnabled ? "1" : "0");
      if (vmPlatformEnabled)
	{
	  result += "VirtualMachinePlatform : OK (Enabled)\r\n";
	}
      else
	{
	  result += "VirtualMachinePlatform: WARN (Disabled)\r\n";
	  result += "*** 'Virtual Machine Platform' will be enabled during installation.\r\n";
	}

      bool windowsSubsystemLinuxEnabled = CheckWindowsOptionalFeature (pSvc, "Microsoft-Windows-Subsystem-Linux");
      SetMsiProperty (hInstall, "CUB_WINDOWS_SUBSYSTEM_LINUX", windowsSubsystemLinuxEnabled ? "1" : "0");
      if (windowsSubsystemLinuxEnabled)
	{
	  result += "Microsoft-Windows-Subsystem-Linux: OK (Enabled)\r\n";
	}
      else
	{
	  result += "Microsoft-Windows-Subsystem-Linux: WARN (Disabled)\r\n";
	  result += "***'Microsoft Windows Subsystem for Linux' will be enabled during installation.\r\n";
	}

      if (!vmPlatformEnabled || !windowsSubsystemLinuxEnabled)
	{
	  SetMsiProperty (hInstall, "NEEDS_REBOOT", "1");
	}

      SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", result);

      pSvc->Release();
      pLoc->Release();
      CoUninitialize();

      return ERROR_SUCCESS;

    }
  catch (...)
    {
      if (pSvc)
	{
	  pSvc->Release();
	}
      if (pLoc)
	{
	  pLoc->Release();
	}

      if (comInitialized)
	{
	  CoUninitialize();
	}
      SetMsiProperty (hInstall, "CUB_ENVIRONMENT_CHECK_OUTPUT", "ERROR: Environment check failed due to unexpected error");
      return ERROR_INSTALL_FAILURE;
    }
}

UINT EnvironmentUtil::OpenFileDialog (MSIHANDLE hInstall)
{
  HRESULT hr = S_OK;
  std::string selectedPath = "";
  IFileOpenDialog *pFileOpenDialog = NULL;

  hr = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED);
  if (FAILED (hr))
    {
      return ERROR_INSTALL_FAILURE;
    }

  hr = CoCreateInstance (CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
			 IID_IFileOpenDialog, reinterpret_cast<void **> (&pFileOpenDialog));

  if (SUCCEEDED (hr))
    {
      const COMDLG_FILTERSPEC rgSpec[] =
      {
	{L"CUBRID Image Files (*.tar.gz)", L"*.tar.gz"},
	{L"All Files (*.*)", L"*.*"}
      };

      pFileOpenDialog->SetFileTypes (ARRAYSIZE (rgSpec), rgSpec);
      pFileOpenDialog->SetFileTypeIndex (1);
      pFileOpenDialog->SetOptions (FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
      pFileOpenDialog->SetTitle (L"Select a CUBRID Image File");

      hr = pFileOpenDialog->Show (NULL);
      if (SUCCEEDED (hr))
	{
	  IShellItem *pItem = NULL;
	  hr = pFileOpenDialog->GetResult (&pItem);
	  if (SUCCEEDED (hr))
	    {
	      PWSTR pszPath = NULL;
	      hr = pItem->GetDisplayName (SIGDN_FILESYSPATH, &pszPath);
	      if (SUCCEEDED (hr))
		{
		  int size_needed = WideCharToMultiByte (CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
		  std::string selectedPath (size_needed, 0);
		  WideCharToMultiByte (CP_UTF8, 0, pszPath, -1, &selectedPath[0], size_needed, NULL, NULL);
		  CoTaskMemFree (pszPath);
		  SetMsiProperty (hInstall, "CUB_WSL_IMAGE_FILE", selectedPath);
		}
	      pItem->Release();
	    }
	}
      else
	{
	  hr = HRESULT_FROM_WIN32 (GetLastError());
	}
      pFileOpenDialog->Release();
    }
  CoUninitialize();
  return ERROR_SUCCESS;
}

