#include <windows.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>

#include "logger.h"
#include "system_util.h"
#include "cubrid_installer.h"

namespace
{

  constexpr int   kMaxAttempts      = 10;
  constexpr int   kRetryDelaySec    = 3;
  constexpr DWORD kStartTimeoutMs   = 60 * 1000;
  constexpr DWORD kStatusTimeoutMs  = 20 * 1000;
  constexpr int   kPostStartWaitSec = 2;

  const std::string kEnvSetup = ". ~/.cubrid.sh; ";

  bool RunBashInWsl (const std::string &wslName,
		     const std::string &bashCmd,
		     DWORD timeoutMs,
		     DWORD *outExit)
  {
    if (outExit)
      {
	*outExit = static_cast<DWORD> (-1);
      }

    std::string cmd =
	    "wsl.exe -d " + wslName + " -u cubrid --exec bash -c \"" +
	    kEnvSetup + bashCmd + "\"";

    STARTUPINFOA si{};
    si.cb          = sizeof (si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::string mutableCmd = cmd;
    BOOL ok = CreateProcessA (
		      NULL,
		      mutableCmd.data(),
		      NULL, NULL,
		      FALSE,
		      CREATE_NO_WINDOW,
		      NULL, NULL,
		      &si, &pi);

    if (!ok)
      {
	Logger::GetInstance().LogError (
		"cubrid_starter: CreateProcess failed err=" +
		std::to_string (GetLastError()) + " cmd=" + cmd);
	return false;
      }

    DWORD waitRc = WaitForSingleObject (pi.hProcess, timeoutMs);
    if (waitRc == WAIT_TIMEOUT)
      {
	Logger::GetInstance().LogWarning (
		"cubrid_starter: child timed out after " +
		std::to_string (timeoutMs) + "ms, terminating: " + bashCmd);
	TerminateProcess (pi.hProcess, 1);
	WaitForSingleObject (pi.hProcess, 5000);
      }

    DWORD exitCode = 0;
    GetExitCodeProcess (pi.hProcess, &exitCode);
    CloseHandle (pi.hThread);
    CloseHandle (pi.hProcess);

    if (outExit)
      {
	*outExit = exitCode;
      }
    return true;
  }

  bool RunBashInWslCapture (const std::string &wslName,
			    const std::string &bashCmd,
			    DWORD timeoutMs,
			    DWORD *outExit,
			    std::string *outStdout)
  {
    if (outExit)
      {
	*outExit = static_cast<DWORD> (-1);
      }
    if (outStdout)
      {
	outStdout->clear();
      }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength              = sizeof (sa);
    sa.bInheritHandle       = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe  = NULL;
    HANDLE hWritePipe = NULL;
    if (!CreatePipe (&hReadPipe, &hWritePipe, &sa, 0))
      {
	Logger::GetInstance().LogError (
		"cubrid_starter: CreatePipe failed err=" +
		std::to_string (GetLastError()));
	return false;
      }
    SetHandleInformation (hReadPipe, HANDLE_FLAG_INHERIT, 0);

    std::string cmd =
	    "wsl.exe -d " + wslName + " -u cubrid --exec bash -c \"" +
	    kEnvSetup + bashCmd + "\"";

    STARTUPINFOA si{};
    si.cb          = sizeof (si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWritePipe;
    si.hStdError   = hWritePipe;
    si.hStdInput   = NULL;
    PROCESS_INFORMATION pi{};

    std::string mutableCmd = cmd;
    BOOL ok = CreateProcessA (
		      NULL,
		      mutableCmd.data(),
		      NULL, NULL,
		      TRUE,                  // bInheritHandles must be TRUE for std redir.
		      CREATE_NO_WINDOW,
		      NULL, NULL,
		      &si, &pi);

    CloseHandle (hWritePipe);

    if (!ok)
      {
	CloseHandle (hReadPipe);
	Logger::GetInstance().LogError (
		"cubrid_starter: CreateProcess failed err=" +
		std::to_string (GetLastError()) + " cmd=" + cmd);
	return false;
      }

    std::atomic<bool> finished{false};
    std::thread watchdog ([&]
    {
      const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds (timeoutMs);
      while (!finished.load())
	{
	  if (WaitForSingleObject (pi.hProcess, 100) == WAIT_OBJECT_0)
	    {
	      return;
	    }
	  if (std::chrono::steady_clock::now() >= deadline)
	    {
	      Logger::GetInstance().LogWarning (
		      "cubrid_starter: command timed out after " +
		      std::to_string (timeoutMs) + "ms, terminating: " + bashCmd);
	      TerminateProcess (pi.hProcess, 1);
	      return;
	    }
	}
    });

    std::string out;
    char  buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile (hReadPipe, buf, sizeof (buf), &bytesRead, NULL) &&
	   bytesRead > 0)
      {
	out.append (buf, bytesRead);
      }

    finished.store (true);
    watchdog.join();
    CloseHandle (hReadPipe);

    DWORD exitCode = 0;
    GetExitCodeProcess (pi.hProcess, &exitCode);
    CloseHandle (pi.hThread);
    CloseHandle (pi.hProcess);

    if (outExit)
      {
	*outExit   = exitCode;
      }
    if (outStdout)
      {
	*outStdout = std::move (out);
      }
    return true;
  }

  bool IsMasterRunning (const std::string &wslName)
  {
    std::string output;
    DWORD       exitCode = 0;
    if (!RunBashInWslCapture (
		wslName,
		"cubrid service status 2>&1",
		kStatusTimeoutMs,
		&exitCode,
		&output))
      {
	return false;
      }

    std::istringstream iss (output);
    std::string        line;
    while (std::getline (iss, line))
      {
	if (line.find ("cubrid master") == std::string::npos)
	  {
	    continue;
	  }
	if (line.find (" is ") == std::string::npos)
	  {
	    continue;
	  }

	const bool notRunning =
		line.find ("is not running") != std::string::npos;
	Logger::GetInstance().LogInfo (
		std::string ("cubrid_starter: master status -> ") +
		(notRunning ? "NOT running" : "running") +
		" (line: " + line + ")");
	return !notRunning;
      }

    Logger::GetInstance().LogWarning (
	    "cubrid_starter: no 'cubrid master ... is ...' line in status output; "
	    "exit=" + std::to_string (exitCode) +
	    " output[0..200]=" + output.substr (0, 200));
    return false;
  }

  void CubridServiceStart (const std::string &wslName)
  {
    DWORD exitCode = 0;
    RunBashInWsl (
	    wslName,
	    "nohup cubrid service start </dev/null >/dev/null 2>&1",
	    kStartTimeoutMs,
	    &exitCode);
    Logger::GetInstance().LogInfo (
	    "cubrid_starter: cubrid service start exit=" + std::to_string (exitCode));
  }

  int RunStarter()
  {
    Logger &logger = Logger::GetInstance();
    logger.Initialize();
    logger.LogInfo ("cubrid_starter: begin");

    std::string wslName;
    if (!SystemUtil::GetRegistryValueString (
		HKEY_CURRENT_USER, REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_WSL_NAME, wslName) ||
	wslName.empty())
      {
	logger.LogError (
		"cubrid_starter: WslName not found in registry; aborting");
	return 0;
      }
    logger.LogInfo ("cubrid_starter: WslName=" + wslName);

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
      {
	if (IsMasterRunning (wslName))
	  {
	    logger.LogInfo (
		    "cubrid_starter: cubrid_master is running (attempt " +
		    std::to_string (attempt) + "/" +
		    std::to_string (kMaxAttempts) + ")");
	    return 0;
	  }

	logger.LogInfo (
		"cubrid_starter: attempt " + std::to_string (attempt) +
		"/" + std::to_string (kMaxAttempts) +
		" - cubrid service start");
	CubridServiceStart (wslName);

	std::this_thread::sleep_for (std::chrono::seconds (kPostStartWaitSec));

	if (IsMasterRunning (wslName))
	  {
	    logger.LogInfo (
		    "cubrid_starter: cubrid_master came up on attempt " +
		    std::to_string (attempt));
	    return 0;
	  }

	logger.LogWarning (
		"cubrid_starter: cubrid_master still not running after attempt " +
		std::to_string (attempt));

	if (attempt < kMaxAttempts)
	  {
	    std::this_thread::sleep_for (std::chrono::seconds (kRetryDelaySec));
	  }
      }

    logger.LogError (
	    "cubrid_starter: cubrid_master failed to start after " +
	    std::to_string (kMaxAttempts) + " attempts; giving up");
    return 0;
  }

}

int WINAPI WinMain (HINSTANCE, HINSTANCE, LPSTR, int)
{
  char sysDir[MAX_PATH];
  if (GetSystemDirectoryA (sysDir, MAX_PATH))
    {
      SetCurrentDirectoryA (sysDir);
    }
  return RunStarter();
}
