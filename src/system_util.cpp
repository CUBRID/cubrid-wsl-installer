#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include <filesystem>

#include "system_util.h"
#include "logger.h"

static Logger &logger = Logger::GetInstance();

namespace
{

  struct EnumConsoleCtx
  {
    DWORD targetPid;
    HWND  found;
  };

  struct PipeReaderState
  {
    HANDLE      hReadPipe = INVALID_HANDLE_VALUE;
    std::string result;
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
			  | ENABLE_QUICK_EDIT_MODE);
	    newMode |= ENABLE_EXTENDED_FLAGS;
	    SetConsoleMode (hIn, newMode);
	  }
      }
    FreeConsole();
  }

  static bool OpenOrCreateKeyWrite (HKEY root, const std::string &path, HKEY &outKey)
  {
    if (RegOpenKeyExA (root, path.c_str(), 0, KEY_WRITE, &outKey) == ERROR_SUCCESS)
      {
	return true;
      }
    return RegCreateKeyExA (root, path.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE,
			    KEY_WRITE, NULL, &outKey, NULL) == ERROR_SUCCESS;
  }

  static std::wstring Utf8ToWide (const std::string &s)
  {
    if (s.empty())
      {
	return std::wstring();
      }
    int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w (n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
  }

  static std::string WideToUtf8 (const std::wstring &w)
  {
    if (w.empty())
      {
	return std::string();
      }
    int n = WideCharToMultiByte (CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s (n, '\0');
    WideCharToMultiByte (CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
  }

  static void KillProcessTreeByPid (DWORD pid)
  {
    std::string cmd = "taskkill /F /T /PID " + std::to_string (pid);

    STARTUPINFOA si;
    ZeroMemory (&si, sizeof (si));
    si.cb = sizeof (si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION tk;
    ZeroMemory (&tk, sizeof (tk));

    std::string mutableCmd = cmd;
    if (CreateProcessA (NULL, mutableCmd.data(), NULL, NULL, FALSE,
			CREATE_NO_WINDOW, NULL, NULL, &si, &tk))
      {
	WaitForSingleObject (tk.hProcess, 5000);
	CloseHandle (tk.hProcess);
	CloseHandle (tk.hThread);
      }
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
      }

    DisableChildConsoleInputMode (pid);
  }).detach();
}

const std::string &SystemUtil::GetSystemDir()
{
  static const std::string dir = []()
  {
    char buf[MAX_PATH];
    UINT n = GetSystemDirectoryA (buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::string (buf) : std::string ("C:\\Windows\\System32");
  }
  ();
  return dir;
}

SystemUtil::CommandResult SystemUtil::RunProcessWithTimeout (const std::string &command, DWORD timeoutMs,
    bool captureOutput)
{
  CommandResult res;

  HANDLE hReadPipe  = INVALID_HANDLE_VALUE;
  HANDLE hWritePipe = INVALID_HANDLE_VALUE;

  if (captureOutput)
    {
      SECURITY_ATTRIBUTES sa;
      sa.nLength = sizeof (SECURITY_ATTRIBUTES);
      sa.bInheritHandle = TRUE;
      sa.lpSecurityDescriptor = NULL;
      if (!CreatePipe (&hReadPipe, &hWritePipe, &sa, 0))
	{
	  return res;
	}
      SetHandleInformation (hReadPipe, HANDLE_FLAG_INHERIT, 0);
    }

  HANDLE hJob = CreateJobObjectA (NULL, NULL);
  if (hJob)
    {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
      ZeroMemory (&jeli, sizeof (jeli));
      jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject (hJob, JobObjectExtendedLimitInformation, &jeli, sizeof (jeli));
    }

  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory (&si, sizeof (si));
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  if (captureOutput)
    {
      si.dwFlags |= STARTF_USESTDHANDLES;
      si.hStdOutput = hWritePipe;
      si.hStdError  = hWritePipe;
      si.hStdInput  = NULL;
    }

  ZeroMemory (&pi, sizeof (pi));

  std::string mutableCmd = command;
  if (!CreateProcessA (NULL, mutableCmd.data(), NULL, NULL,
		       captureOutput ? TRUE : FALSE,
		       CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
      if (hJob)
	{
	  CloseHandle (hJob);
	}
      if (hReadPipe != INVALID_HANDLE_VALUE)
	{
	  CloseHandle (hReadPipe);
	}
      if (hWritePipe != INVALID_HANDLE_VALUE)
	{
	  CloseHandle (hWritePipe);
	}
      return res;
    }

  if (hJob)
    {
      if (!AssignProcessToJobObject (hJob, pi.hProcess))
	{
	  logger.LogWarning ("AssignProcessToJobObject failed: "+ std::to_string (GetLastError ()));
	  CloseHandle (hJob);
	  hJob = NULL;
	}
    }
  ResumeThread (pi.hThread);
  res.launched = true;

  if (captureOutput)
    {
      CloseHandle (hWritePipe);
      hWritePipe = INVALID_HANDLE_VALUE;
    }

  std::shared_ptr<PipeReaderState> state;
  std::thread reader;
  if (captureOutput)
    {
      state = std::make_shared<PipeReaderState>();
      state->hReadPipe = hReadPipe;
      reader = std::thread ([state]
      {
	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile (state->hReadPipe, buffer, sizeof (buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
	  {
	    state->result.append (buffer, bytesRead);
	  }
	CloseHandle (state->hReadPipe);
	state->hReadPipe = INVALID_HANDLE_VALUE;
      });
    }

  res.timedOut = (WaitForSingleObject (pi.hProcess, timeoutMs) == WAIT_TIMEOUT);
  if (res.timedOut)
    {
      if (hJob)
	{
	  TerminateJobObject (hJob, 1);
	}
      else
	{
	  KillProcessTreeByPid (pi.dwProcessId);
	}
      TerminateProcess (pi.hProcess, 1);
    }

  if (captureOutput)
    {
      const DWORD readerGraceMs = 3000;
      HANDLE readerHandle = (HANDLE) reader.native_handle();
      bool readerDone = (WaitForSingleObject (readerHandle, readerGraceMs) == WAIT_OBJECT_0);

      if (!readerDone)
	{
	  res.timedOut = true;
	  for (int i = 0; i < 5 && !readerDone; ++i)
	    {
	      CancelSynchronousIo (readerHandle);
	      readerDone = (WaitForSingleObject (readerHandle, 200) == WAIT_OBJECT_0);
	    }
	}

      if (readerDone)
	{
	  reader.join();
	  res.output = std::move (state->result);
	}
      else
	{
	  reader.detach();
	}
    }

  if (!res.timedOut)
    {
      DWORD code = 0;
      if (GetExitCodeProcess (pi.hProcess, &code))
	{
	  res.exitCode = code;
	}
    }

  CloseHandle (pi.hProcess);
  CloseHandle (pi.hThread);
  if (hJob)
    {
      CloseHandle (hJob);
    }
  return res;
}

bool SystemUtil::ExecuteCommandWithoutResult (const std::string &command)
{
  CommandResult r = RunProcessWithTimeout (command, INFINITE, false);
  return r.launched && r.exitCode == 0;
}

bool SystemUtil::ExecuteCommandWithoutResultWithTimeout (const std::string &command, DWORD timeoutMs)
{
  CommandResult r = RunProcessWithTimeout (command, timeoutMs, false);
  return r.launched && !r.timedOut && r.exitCode == 0;
}

std::string SystemUtil::ExecuteCommandWithTimeout (const std::string &command, DWORD timeoutMs)
{
  CommandResult r = RunProcessWithTimeout (command, timeoutMs, true);
  return r.timedOut ? std::string() : r.output;
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
  if (!OpenOrCreateKeyWrite (rootKey, keyPath, hKey))
    {
      return false;
    }

  LONG result = RegSetValueExA (hKey, valueName.c_str(), 0, REG_SZ,
				(const BYTE *)value.c_str(), (DWORD) (value.length() + 1));
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
  return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool SystemUtil::SetRegistryValueDWORD (const HKEY rootKey, const std::string &keyPath,
					const std::string &valueName, DWORD value)
{
  HKEY hKey;
  if (!OpenOrCreateKeyWrite (rootKey, keyPath, hKey))
    {
      return false;
    }

  LONG result = RegSetValueExA (hKey, valueName.c_str(), 0, REG_DWORD,
				(const BYTE *)&value, sizeof (DWORD));
  RegCloseKey (hKey);
  return result == ERROR_SUCCESS;
}

bool SystemUtil::SetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName, const std::string &value)
{
  std::wstring wPropertyName = Utf8ToWide (propertyName);
  std::wstring wValue = Utf8ToWide (value);
  UINT result = MsiSetPropertyW (hInstall, wPropertyName.c_str(), wValue.c_str());
  return (result == ERROR_SUCCESS);
}

std::string SystemUtil::GetMsiProperty (MSIHANDLE hInstall, const std::string &propertyName)
{
  std::wstring wPropertyName = Utf8ToWide (propertyName);

  DWORD bufSize = 0;
  UINT result = MsiGetPropertyW (hInstall, wPropertyName.c_str(), L"", &bufSize);

  if (result == ERROR_MORE_DATA || result == ERROR_SUCCESS)
    {
      bufSize++;
      std::vector<wchar_t> buffer (bufSize);
      result = MsiGetPropertyW (hInstall, wPropertyName.c_str(), buffer.data(), &bufSize);

      if (result == ERROR_SUCCESS)
	{
	  return WideToUtf8 (std::wstring (buffer.data()));
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
