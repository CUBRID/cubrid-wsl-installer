#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>

#include "system_util.h"

namespace
{

  struct EnumConsoleCtx
  {
    DWORD targetPid;
    HWND  found;
  };

  static BOOL CALLBACK EnumConsoleProc (HWND hwnd, LPARAM lp)
  {
    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    auto *ctx = reinterpret_cast<EnumConsoleCtx *> (lp);
    if (pid != ctx->targetPid)
      {
	return TRUE;
      }
    WCHAR cls[64] = {};
    if (GetClassNameW (hwnd, cls, _countof (cls)) > 0)
      {
	if (wcscmp (cls, L"ConsoleWindowClass") == 0)
	  {
	    ctx->found = hwnd;
	    return FALSE;
	  }
      }
    return TRUE;
  }

  static HWND WaitForChildConsoleWindow (DWORD pid, int timeoutMs)
  {
    const auto deadline = std::chrono::steady_clock::now()
			  + std::chrono::milliseconds (timeoutMs);
    while (true)
      {
	EnumConsoleCtx ctx{ pid, nullptr };
	EnumWindows (EnumConsoleProc, reinterpret_cast<LPARAM> (&ctx));
	if (ctx.found)
	  {
	    return ctx.found;
	  }
	if (std::chrono::steady_clock::now() >= deadline)
	  {
	    return nullptr;
	  }
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
      }
  }

  static void DisableChildConsoleInputMode (DWORD pid)
  {
    FreeConsole();
    if (!AttachConsole (pid))
      {
	return;
      }

    HANDLE hIn = GetStdHandle (STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE && hIn != NULL)
      {
	DWORD mode = 0;
	if (GetConsoleMode (hIn, &mode))
	  {
	    DWORD newMode = mode;
	    newMode &= ~ (ENABLE_PROCESSED_INPUT
			  | ENABLE_LINE_INPUT
			  | ENABLE_ECHO_INPUT
			  | ENABLE_MOUSE_INPUT
			  | ENABLE_QUICK_EDIT_MODE
			  | ENABLE_WINDOW_INPUT);
	    newMode |= ENABLE_EXTENDED_FLAGS;
	    SetConsoleMode (hIn, newMode);
	  }
      }
    FreeConsole();
  }

}

void SystemUtil::BlockChildConsoleInput (DWORD pid)
{
  if (pid == 0)
    {
      return;
    }
  std::thread ([pid]
  {
    HWND hwnd = WaitForChildConsoleWindow (pid, 5000);
    if (hwnd)
      {
	HMENU hSys = GetSystemMenu (hwnd, FALSE);
	if (hSys)
	  {
	    DeleteMenu (hSys, SC_CLOSE,    MF_BYCOMMAND);
	    DrawMenuBar (hwnd);
	  }
	EnableWindow (hwnd, FALSE);
      }

    DisableChildConsoleInputMode (pid);
  }).detach();
}

std::string SystemUtil::ExecuteCommandWithPopen (const std::string &command)
{
  FILE *pipe = _popen (command.c_str(), "r");
  if (!pipe)
    {
      return "";
    }

  std::string result;
  char buffer[128];
  while (fgets (buffer, sizeof (buffer), pipe) != NULL)
    {
      result += buffer;
    }

  _pclose (pipe);
  return result;
}

const std::string &SystemUtil::GetSystemDir()
{
  static const std::string dir = []() {
    char buf[MAX_PATH];
    UINT n = GetSystemDirectoryA (buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::string (buf) : std::string ("C:\\Windows\\System32");
  }();
  return dir;
}

std::string SystemUtil::ExecutePowerShellCommand (const std::string &command)
{
  std::string fullCommand = "powershell -ExecutionPolicy Bypass -Command \"" + command + "\"";
  return ExecuteCommand (fullCommand);
}

bool SystemUtil::ExecuteCommandWithoutResult (const std::string &command)
{
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  ZeroMemory (&pi, sizeof (pi));

  if (CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE,
		      CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {

      WaitForSingleObject (pi.hProcess, INFINITE);
      DWORD exitCode;
      GetExitCodeProcess (pi.hProcess, &exitCode);
      CloseHandle (pi.hProcess);
      CloseHandle (pi.hThread);

      if (exitCode == 0)
	{
	  return true;
	}
      else
	{
	  return false;
	}
    }
  return false;
}

bool SystemUtil::ExecuteCommandWithoutResultWithTimeout (const std::string &command, DWORD timeoutMs)
{
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  ZeroMemory (&pi, sizeof (pi));

  if (CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE,
		      CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {

      DWORD waitResult = WaitForSingleObject (pi.hProcess, timeoutMs);

      if (waitResult == WAIT_TIMEOUT)
	{
	  TerminateProcess (pi.hProcess, 1);
	  CloseHandle (pi.hProcess);
	  CloseHandle (pi.hThread);
	  return false;
	}

      DWORD exitCode;
      GetExitCodeProcess (pi.hProcess, &exitCode);
      CloseHandle (pi.hProcess);
      CloseHandle (pi.hThread);

      if (exitCode == 0)
	{
	  return true;
	}
      else
	{
	  return false;
	}
    }
  return false;
}

HANDLE SystemUtil::ExecuteCommandWithOutResultAsync (const std::string &command)
{
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  ZeroMemory (&pi, sizeof (pi));

  if (CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, FALSE,
		      CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
      CloseHandle (pi.hThread);
      return pi.hProcess;
    }
  return NULL;
}

std::string SystemUtil::ExecuteCommand (const std::string &command)
{
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof (SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  HANDLE hReadPipe, hWritePipe;
  if (!CreatePipe (&hReadPipe, &hWritePipe, &sa, 0))
    {
      return "";
    }

  SetHandleInformation (hReadPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;
  si.wShowWindow = SW_HIDE;

  ZeroMemory (&pi, sizeof (pi));

  std::string result;
  if (CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, TRUE,
		      CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
      CloseHandle (hWritePipe);

      char buffer[4096];
      DWORD bytesRead;
      while (ReadFile (hReadPipe, buffer, sizeof (buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
	{
	  buffer[bytesRead] = '\0';
	  result += buffer;
	}

      WaitForSingleObject (pi.hProcess, INFINITE);
      CloseHandle (pi.hProcess);
      CloseHandle (pi.hThread);
    }

  CloseHandle (hReadPipe);
  return result;
}

std::string SystemUtil::ExecuteCommandWithTimeout (const std::string &command, DWORD timeoutMs)
{
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof (SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  HANDLE hReadPipe, hWritePipe;
  if (!CreatePipe (&hReadPipe, &hWritePipe, &sa, 0))
    {
      return "";
    }

  SetHandleInformation (hReadPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;
  si.wShowWindow = SW_HIDE;

  ZeroMemory (&pi, sizeof (pi));

  std::string result;
  if (CreateProcessA (NULL, (LPSTR)command.c_str(), NULL, NULL, TRUE,
		      CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
      CloseHandle (hWritePipe);

      DWORD waitResult = WaitForSingleObject (pi.hProcess, timeoutMs);

      if (waitResult == WAIT_TIMEOUT)
	{
	  TerminateProcess (pi.hProcess, 1);
	  CloseHandle (pi.hProcess);
	  CloseHandle (pi.hThread);
	  CloseHandle (hReadPipe);
	  return "";
	}

      char buffer[4096];
      DWORD bytesRead;
      while (ReadFile (hReadPipe, buffer, sizeof (buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
	{
	  buffer[bytesRead] = '\0';
	  result += buffer;
	}

      CloseHandle (pi.hProcess);
      CloseHandle (pi.hThread);
    }

  CloseHandle (hReadPipe);
  return result;
}

bool SystemUtil::IsCommandAvailable (const std::string &command)
{
  std::string testCommand = "where " + command + " >nul 2>&1";
  int result = system (testCommand.c_str());
  return result == 0;
}

bool SystemUtil::GetRegistryValueString (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
    std::string &outValue)
{
  HKEY hKey;
  if (RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
      return false;
    }

  DWORD valueType = REG_SZ;
  DWORD bufferSize = 0;
  LONG result = RegQueryValueExA (hKey, valueName.c_str(), NULL, &valueType, NULL, &bufferSize);
  if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA)
    {
      RegCloseKey (hKey);
      return false;
    }

  std::vector<char> buffer (bufferSize);
  result = RegQueryValueExA (hKey, valueName.c_str(), NULL, &valueType, (LPBYTE)buffer.data(), &bufferSize);
  RegCloseKey (hKey);

  if (result != ERROR_SUCCESS)
    {
      return false;
    }

  if (valueType == REG_SZ || valueType == REG_EXPAND_SZ)
    {
      if (bufferSize > 0 && buffer[bufferSize - 1] == '\0')
	{
	  bufferSize--;
	}
      outValue = std::string (buffer.data(), bufferSize);
    }
  else
    {
      outValue = std::string (buffer.data(), bufferSize);
    }

  return true;
}

bool SystemUtil::GetRegistryValueDWORD (const HKEY rootKey, const std::string &keyPath, const std::string &valueName,
					DWORD &outValue)
{
  HKEY hKey;
  if (RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
      return false;
    }
  DWORD valueSize = sizeof (outValue);
  DWORD valueType = REG_DWORD;
  LONG result = RegQueryValueExA (hKey, valueName.c_str(), NULL, &valueType, (LPBYTE)&outValue, &valueSize);
  RegCloseKey (hKey);
  return result == ERROR_SUCCESS;
}

bool SystemUtil::CheckRegistryValueExists (const HKEY rootKey, const std::string &keyPath,
    const std::string &valueName)
{
  HKEY hKey;
  if (RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
      return false;
    }

  LONG result = RegQueryValueExA (hKey, valueName.c_str(), NULL, NULL, NULL, NULL);
  RegCloseKey (hKey);

  return result == ERROR_SUCCESS;
}

bool SystemUtil::CheckRegistryKeyExists (const HKEY rootKey, const std::string &keyPath)
{
  HKEY hKey;
  if (RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
      RegCloseKey (hKey);
      return true;
    }
  return false;
}

bool SystemUtil::SetRegistryValueString (const HKEY rootKey, const std::string &keyPath,
    const std::string &valueName, const std::string &value)
{
  HKEY hKey;
  LONG result = RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_WRITE, &hKey);

  if (result != ERROR_SUCCESS)
    {
      result = RegCreateKeyExA (
		       rootKey,
		       keyPath.c_str(),
		       0,
		       NULL,
		       REG_OPTION_NON_VOLATILE,
		       KEY_WRITE,
		       NULL,
		       &hKey,
		       NULL
	       );

      if (result != ERROR_SUCCESS)
	{
	  return false;
	}
    }

  result = RegSetValueExA (
		   hKey,
		   valueName.c_str(),
		   0,
		   REG_SZ,
		   (const BYTE *)value.c_str(),
		   (DWORD) (value.length() + 1)
	   );

  RegCloseKey (hKey);
  return result == ERROR_SUCCESS;
}

bool SystemUtil::DeleteRegistryValue (const HKEY rootKey, const std::string &keyPath,
                                      const std::string &valueName)
{
  HKEY hKey;
  LONG result = RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_WRITE, &hKey);
  if (result != ERROR_SUCCESS)
    {
      return false;
    }

  result = RegDeleteValueA (hKey, valueName.c_str());
  RegCloseKey (hKey);
  // Treat "value not present" as success (idempotent removal; needed for uninstall).
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool SystemUtil::SetRegistryValueDWORD (const HKEY rootKey, const std::string &keyPath,
					const std::string &valueName, DWORD value)
{
  HKEY hKey;
  LONG result = RegOpenKeyExA (rootKey, keyPath.c_str(), 0, KEY_WRITE, &hKey);

  if (result != ERROR_SUCCESS)
    {
      result = RegCreateKeyExA (
		       rootKey,
		       keyPath.c_str(),
		       0,
		       NULL,
		       REG_OPTION_NON_VOLATILE,
		       KEY_WRITE,
		       NULL,
		       &hKey,
		       NULL
	       );

      if (result != ERROR_SUCCESS)
	{
	  return false;
	}
    }

  result = RegSetValueExA (
		   hKey,
		   valueName.c_str(),
		   0,
		   REG_DWORD,
		   (const BYTE *)&value,
		   sizeof (DWORD)
	   );

  RegCloseKey (hKey);
  return result == ERROR_SUCCESS;
}

bool SystemUtil::SetRegistryValueBoolean (const HKEY rootKey, const std::string &keyPath,
    const std::string &valueName, bool value)
{
  DWORD dwValue = value ? 1 : 0;
  return SetRegistryValueDWORD (rootKey, keyPath, valueName, dwValue);
}

bool SystemUtil::SetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName, const std::string &value)
{
  int wLen = MultiByteToWideChar (CP_UTF8, 0, propertyName.c_str(), -1, NULL, 0);
  std::vector<wchar_t> wPropertyName (wLen);
  MultiByteToWideChar (CP_UTF8, 0, propertyName.c_str(), -1, wPropertyName.data(), wLen);

  wLen = MultiByteToWideChar (CP_UTF8, 0, value.c_str(), -1, NULL, 0);
  std::vector<wchar_t> wValue (wLen);
  MultiByteToWideChar (CP_UTF8, 0, value.c_str(), -1, wValue.data(), wLen);

  UINT result = MsiSetPropertyW (hInstall, wPropertyName.data(), wValue.data());
  return (result == ERROR_SUCCESS);
}

std::string SystemUtil::GetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName)
{
  int wLen = MultiByteToWideChar (CP_UTF8, 0, propertyName.c_str(), -1, NULL, 0);
  std::vector<wchar_t> wPropertyName (wLen);
  MultiByteToWideChar (CP_UTF8, 0, propertyName.c_str(), -1, wPropertyName.data(), wLen);

  DWORD bufSize = 0;
  UINT result = MsiGetPropertyW (hInstall, wPropertyName.data(), L"", &bufSize);

  if (result == ERROR_MORE_DATA || result == ERROR_SUCCESS)
    {
      bufSize++;
      std::vector<wchar_t> buffer (bufSize);
      result = MsiGetPropertyW (hInstall, wPropertyName.data(), buffer.data(), &bufSize);

      if (result == ERROR_SUCCESS)
	{
	  std::wstring wValue (buffer.data());
	  int nLen = WideCharToMultiByte (CP_UTF8, 0, wValue.c_str(), -1, NULL, 0, NULL, NULL);
	  std::vector<char> nBuffer (nLen);
	  WideCharToMultiByte (CP_UTF8, 0, wValue.c_str(), -1, nBuffer.data(), nLen, NULL, NULL);
	  return std::string (nBuffer.data());
	}
    }

  return "";
}

void SystemUtil::KillProcessByName (const std::string &processName)
{
  HANDLE hSnapShot = CreateToolhelp32Snapshot (TH32CS_SNAPALL, NULL);
  PROCESSENTRY32 pEntry;
  pEntry.dwSize = sizeof (pEntry);
  BOOL hRes = Process32First (hSnapShot, &pEntry);
  while (hRes)
    {
      if (strcmp (pEntry.szExeFile, processName.c_str()) == 0)
	{
	  HANDLE hProcess = OpenProcess (PROCESS_TERMINATE, 0, pEntry.th32ProcessID);
	  if (hProcess != NULL)
	    {
	      TerminateProcess (hProcess, 9);
	      CloseHandle (hProcess);
	    }
	}
      hRes = Process32Next (hSnapShot, &pEntry);
    }
  CloseHandle (hSnapShot);
}

bool SystemUtil::CalculateDirectorySize (const std::string &dirPath, ULONGLONG &outSize)
{
  outSize = 0;

  try
    {
      for (const auto &entry : std::filesystem::recursive_directory_iterator (dirPath,
	   std::filesystem::directory_options::skip_permission_denied))
	{
	  if (entry.is_regular_file())
	    {
	      try
		{
		  outSize += entry.file_size();
		}
	      catch (const std::exception &)
		{
		  return false;
		}
	    }
	}
    }
  catch (const std::exception &)
    {
      return false;
    }

  return true;
}
