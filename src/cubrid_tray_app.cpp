#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cctype>

#include "cubrid_tray_app.h"
#include "config.h"
#include "logger.h"
#include "resource.h"
#include "system_util.h"

const UINT TrayApp::MENU_ABOUT = 1001;
const UINT TrayApp::MENU_START = 1002;
const UINT TrayApp::MENU_STOP  = 1003;
const UINT TrayApp::MENU_README  = 1004;
const UINT TrayApp::MENU_EXIT  = 1005;

const char *TrayApp::WINDOW_CLASS_NAME = "CUBRIDTrayApp";

static const char *TRAY_APP_TITLE    = "CUBRID Service Tray";
static const char *TRAY_TIP_RUNNING  = "CUBRID Service - Running";
static const char *TRAY_TIP_STOPPED  = "CUBRID Service - Stopped";
static const char *TRAY_TIP_ERROR    = "CUBRID Service - Error";
static const char *TRAY_MUTEX_NAME   = "Global\\CUBRID_WSL_Tray_App_Mutex";

const int monitorInterval = 10;
const std::string guideFileName = CUB_GUIDE_FILE;

TrayApp::TrayApp() : hWnd (NULL), hIcon (NULL), isRunning (false)
{

}

TrayApp::~TrayApp()
{
  if (hIcon)
    {
      DestroyIcon (hIcon);
    }
}

CUBRIDErrorCode TrayApp::Initialize()
{
  Logger::GetInstance().LogInfo ("Initializing Tray application...");
  bool result = GetWSLNameFromRegistry (wslName);

  if (!result)
    {
      Logger::GetInstance().LogError ("Failed to get WSL name from registry.");
      return CUBRIDErrorCode::CUB_ERROR_CODE_INVALID_WSL_NAME;
    }
  else
    {
      Logger::GetInstance().LogInfo ("WSL name: " + wslName);
    }

  result = GetInstallDirFromRegistry (installDir);
  if (!result)
    {
      Logger::GetInstance().LogError ("Failed to get install directory from registry.");
    }
  else
    {
      Logger::GetInstance().LogInfo ("Install directory: " + installDir);
    }

  WNDCLASSEXA wc = {};
  wc.cbSize = sizeof (WNDCLASSEXA);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandle (NULL);
  wc.lpszClassName = WINDOW_CLASS_NAME;
  wc.hIcon = LoadIcon (NULL, IDI_APPLICATION);
  wc.hCursor = LoadCursor (NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);

  if (!RegisterClassExA (&wc))
    {
      Logger::GetInstance().LogError ("Failed to register window class.");
      return CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_REGISTER_WINDOW_CLASS;
    }

  hWnd = CreateWindowExA (
		 0,
		 WINDOW_CLASS_NAME,
		 TRAY_APP_TITLE,
		 WS_OVERLAPPEDWINDOW,
		 CW_USEDEFAULT, CW_USEDEFAULT,
		 CW_USEDEFAULT, CW_USEDEFAULT,
		 NULL, NULL,
		 GetModuleHandle (NULL),
		 this
	 );

  if (!hWnd)
    {
      Logger::GetInstance().LogError ("Failed to create window.");
      return CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_CREATE_WINDOW;
    }

  ShowWindow (hWnd, SW_HIDE);

  iconRunning_ = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_TRAY_ICON));

  if (!iconRunning_)
    {
      Logger::GetInstance().LogWarning ("Failed to load running icon, using default.");
      iconRunning_ = LoadIcon (NULL, IDI_APPLICATION);
    }

  iconStopped_ = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_ICON_STOPPED));

  if (!iconStopped_)
    {
      Logger::GetInstance().LogWarning ("Failed to load stopped icon, using running icon.");
      iconStopped_ = iconRunning_;
    }

  iconError_ = LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE (IDI_ICON_ERROR));

  if (!iconError_)
    {
      Logger::GetInstance().LogWarning ("Failed to load error icon, using running icon.");
      iconError_ = iconRunning_;
    }

  hIcon = iconRunning_;

  if (!AddTrayIcon())
    {
      Logger::GetInstance().LogError ("Failed to add tray icon.");
      return CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_CREATE_TRAY_ICON;
    }

  Logger::GetInstance().LogInfo ("Tray application initialized successfully.");
  return CUBRIDErrorCode::CUB_ERROR_CODE_SUCCESS;
}

void TrayApp::Run()
{
  Logger::GetInstance().LogInfo ("Starting Tray application...");
  isRunning = true;

  std::thread monitorThread (&TrayApp::MonitorCUBRIDStatus, this);

  MSG msg;
  while (isRunning && GetMessage (&msg, NULL, 0, 0))
    {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }

  if (monitorThread.joinable())
    {
      monitorThread.join();
    }

  Logger::GetInstance().LogInfo ("Tray application stopped.");
}

void TrayApp::Shutdown()
{
  Logger::GetInstance().LogInfo ("Shutting down Tray application...");

  RemoveTrayIcon();

  if (hWnd)
    {
      DestroyWindow (hWnd);
      hWnd = NULL;
    }

  isRunning = false;
}

bool TrayApp::AddTrayIcon()
{
  NOTIFYICONDATAA nid = {};
  nid.cbSize = sizeof (NOTIFYICONDATAA);
  nid.hWnd = hWnd;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TRAYICON;
  nid.hIcon = hIcon;
  strcpy_s (nid.szTip, TRAY_APP_TITLE);

  return Shell_NotifyIconA (NIM_ADD, &nid);
}

bool TrayApp::RemoveTrayIcon()
{
  NOTIFYICONDATAA nid = {};
  nid.cbSize = sizeof (NOTIFYICONDATAA);
  nid.hWnd = hWnd;
  nid.uID = 1;

  return Shell_NotifyIconA (NIM_DELETE, &nid);
}

void TrayApp::UpdateTrayStatus()
{
  CUBRIDStatus status = GetCUBRIDStatus();

  NOTIFYICONDATAA nid = {};
  nid.cbSize = sizeof (NOTIFYICONDATAA);
  nid.hWnd = hWnd;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_TIP;

  switch (status)
    {
    case CUBRIDStatus::CUB_STATUS_RUNNING:
      nid.hIcon = iconRunning_;
      strcpy_s (nid.szTip, TRAY_TIP_RUNNING);
      break;
    case CUBRIDStatus::CUB_STATUS_STOPPED:
      nid.hIcon = iconStopped_;
      strcpy_s (nid.szTip, TRAY_TIP_STOPPED);
      break;
    case CUBRIDStatus::CUB_STATUS_ERROR:
      nid.hIcon = iconError_;
      strcpy_s (nid.szTip, TRAY_TIP_ERROR);
      break;
    }

  Shell_NotifyIconA (NIM_MODIFY, &nid);
}

CUBRIDStatus TrayApp::GetCUBRIDStatus()
{
  Logger::GetInstance().LogInfo ("Checking CUBRID service status...");

  std::string command = BuildWslCommand ("cubrid service status");
  std::string result = SystemUtil::ExecuteCommandWithTimeout (command, 5000);

  Logger::GetInstance().LogInfo ("Checking CUBRID service status command : " + command);

  if (result.empty())
    {
      Logger::GetInstance().LogWarning ("Failed to get CUBRID service status (timeout or error).");
      return CUBRIDStatus::CUB_STATUS_ERROR;
    }

  Logger::GetInstance().LogInfo ("CUBRID service status: " + result);
  std::istringstream iss (result);
  std::string line;
  while (std::getline (iss, line))
    {
      if (line.find ("cubrid master is running.") != std::string::npos)
	{
	  return CUBRIDStatus::CUB_STATUS_RUNNING;
	}

      if (line.find ("cubrid master is not running") != std::string::npos)
	{
	  return CUBRIDStatus::CUB_STATUS_STOPPED;
	}
    }
  Logger::GetInstance().LogError ("Failed to parse CUBRID service status.");
  return CUBRIDStatus::CUB_STATUS_ERROR;
}

std::string TrayApp::GetCUBRIDVersion()
{
  Logger::GetInstance().LogInfo ("Getting CUBRID version...");

  std::string command = BuildWslCommand ("cubrid_rel");
  std::string result = SystemUtil::ExecuteCommandWithTimeout (command, 5000);

  if (result.empty())
    {
      Logger::GetInstance().LogWarning ("Failed to get CUBRID version (timeout or error).");
      return "Unknown (WSL not responding)";
    }

  result.erase (0, result.find_first_not_of (" \t\n\r"));
  result.erase (result.find_last_not_of (" \t\n\r") + 1);

  Logger::GetInstance().LogInfo ("CUBRID version retrieved: " + result);
  return result;
}

bool TrayApp::StartCUBRIDService()
{
  Logger::GetInstance().LogInfo ("Starting CUBRID service...");

  std::string command = BuildWslCommand ("cubrid service start", true);
  Logger::GetInstance().LogInfo ("StartCUBRIDService command: " + command);
  HANDLE hProcess = SystemUtil::ExecuteCommandWithOutResultAsync (command);

  if (hProcess != NULL)
    {
      CloseHandle (hProcess);
      Logger::GetInstance().LogInfo ("CUBRID service start request dispatched.");
      UpdateTrayStatus();
      return true;
    }
  Logger::GetInstance().LogError ("Failed to start CUBRID service.");
  return false;
}

bool TrayApp::StopCUBRIDService()
{
  Logger::GetInstance().LogInfo ("Stopping CUBRID service...");

  std::string command = BuildWslCommand ("cubrid service stop");
  bool result = SystemUtil::ExecuteCommandWithoutResultWithTimeout (command, 10000);

  if (result)
    {
      Logger::GetInstance().LogInfo ("CUBRID service stopped successfully.");
      UpdateTrayStatus();
      return true;
    }
  Logger::GetInstance().LogWarning ("Failed to stop CUBRID service (timeout or error).");
  return false;
}

void TrayApp::ShowAboutDialog()
{
  Logger::GetInstance().LogInfo ("Showing about dialog...");

  std::string version = GetCUBRIDVersion();
  std::string status = "";

  switch (GetCUBRIDStatus())
    {
    case CUBRIDStatus::CUB_STATUS_RUNNING:
      status = "Running";
      break;
    case CUBRIDStatus::CUB_STATUS_STOPPED:
      status = "Stopped";
      break;
    case CUBRIDStatus::CUB_STATUS_ERROR:
      status = "Error";
      break;
    }

  std::string message = std::string (TRAY_APP_TITLE) + "\n\n\n";
  message += "Server Version: " + version + "\n";
  message += "\n";
  message += "Server Status: " + status + "\n";
  message += "\n";
  message += "WSL Name: " + wslName + "\n";
  message += "\n";
  message += "Install Directory: " + installDir + "\n\n\n";
  message += "Version: " CUBRID_WSL_VERSION;


  std::string caption = std::string ("About ") + TRAY_APP_TITLE;

  MSGBOXPARAMSA mbp = {};
  mbp.cbSize      = sizeof (MSGBOXPARAMSA);
  mbp.hwndOwner   = hWnd;
  mbp.hInstance   = GetModuleHandle (NULL);
  mbp.lpszText    = message.c_str();
  mbp.lpszCaption = caption.c_str();
  mbp.dwStyle     = MB_OK | MB_USERICON;
  mbp.lpszIcon    = MAKEINTRESOURCEA (IDI_TRAY_ICON);
  MessageBoxIndirectA (&mbp);
}

void TrayApp::ShowContextMenu()
{
  Logger::GetInstance().LogInfo ("Showing context menu...");

  POINT pt;
  GetCursorPos (&pt);

  HMENU hMenu = CreatePopupMenu();
  if (hMenu)
    {
      AppendMenuA (hMenu, MF_STRING, TrayApp::MENU_ABOUT, "About");
      AppendMenuA (hMenu, MF_SEPARATOR, 0, NULL);

      CUBRIDStatus status = GetCUBRIDStatus();
      if (status == CUBRIDStatus::CUB_STATUS_RUNNING)
	{
	  AppendMenuA (hMenu, MF_STRING | MF_GRAYED, TrayApp::MENU_START, "CUBRID Start");
	  AppendMenuA (hMenu, MF_STRING, TrayApp::MENU_STOP, "CUBRID Stop");
	}
      else
	{
	  AppendMenuA (hMenu, MF_STRING, TrayApp::MENU_START, "CUBRID Start");
	  AppendMenuA (hMenu, MF_STRING | MF_GRAYED, TrayApp::MENU_STOP, "CUBRID Stop");
	}

      AppendMenuA (hMenu, MF_SEPARATOR, 0, NULL);
      AppendMenuA (hMenu, MF_STRING, TrayApp::MENU_README, "Guide");
      AppendMenuA (hMenu, MF_STRING, TrayApp::MENU_EXIT, "Exit");

      SetForegroundWindow (hWnd);
      TrackPopupMenu (hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
      DestroyMenu (hMenu);
    }
}

void TrayApp::MonitorCUBRIDStatus()
{
  Logger::GetInstance().LogInfo ("Starting CUBRID status monitoring...");

  while (isRunning)
    {
      UpdateTrayStatus();
      std::this_thread::sleep_for (std::chrono::seconds (monitorInterval));
    }

  Logger::GetInstance().LogInfo ("CUBRID status monitoring stopped.");
}

CUBRIDErrorCode TrayApp::ShowGuideFile()
{
  Logger::GetInstance().LogInfo ("Showing readme file...");

  if (installDir.empty())
    {
      Logger::GetInstance().LogError ("Readme file path is empty.");
      return CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_GET_INSTALL_DIR;
    }

  HINSTANCE result = ShellExecuteA (NULL, "open", (installDir + "/" + guideFileName).c_str(), NULL, NULL, SW_SHOWNORMAL);
  if (result <= (HINSTANCE)32)
    {
      Logger::GetInstance().LogError ("Failed to show readme file." );
      return CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_SHOW_README_FILE;
    }

  return CUBRIDErrorCode::CUB_ERROR_CODE_SUCCESS;
}

std::string TrayApp::BuildWslCommand (const std::string &bashScript, bool detached) const
{
  static const std::string kEnvSetup = ". ~/.cubrid.sh; ";

  if (detached)
    {
      return "wsl.exe -d " + wslName + " -u cubrid --exec bash -c \"" +
	     kEnvSetup + "nohup " + bashScript + " </dev/null >/dev/null 2>&1\"";
    }

  return "wsl.exe -d " + wslName + " -u cubrid --exec bash -c \"" + kEnvSetup + bashScript + " </dev/null\"";
}

bool TrayApp::GetWSLNameFromRegistry (std::string &wslName)
{
  return SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, CUB_REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_WSL_NAME,
	 wslName);
}

bool TrayApp::GetInstallDirFromRegistry (std::string &installDir)
{
  return SystemUtil::GetRegistryValueString (HKEY_CURRENT_USER, CUB_REGISTRY_KEY_PATH, REGISTRY_VALUE_NAME_INSTALL_DIR,
	 installDir);
}

std::string TrayApp::ErrorCodeToString (CUBRIDErrorCode errorCode)
{
  switch (errorCode)
    {
    case CUBRIDErrorCode::CUB_ERROR_CODE_SUCCESS:
      return "Success";
    case CUBRIDErrorCode::CUB_ERROR_CODE_INVALID_WSL_NAME:
      return "Invalid WSL name or WSL not installed";
    case CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_REGISTER_WINDOW_CLASS:
      return "Failed to register window class";
    case CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_CREATE_WINDOW:
      return "Failed to create window";
    case CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_CREATE_TRAY_ICON:
      return "Failed to create tray icon";
    case CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_GET_INSTALL_DIR:
      return "Failed to get install directory";
    case CUBRIDErrorCode::CUB_ERROR_CODE_FAILED_TO_SHOW_README_FILE:
      return "Failed to show readme file: " + installDir + guideFileName;
    default:
      return "Unknown error: " + std::to_string (errorCode);
    }
}

LRESULT CALLBACK TrayApp::WindowProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  TrayApp *app = nullptr;
  CUBRIDErrorCode errorCode = CUBRIDErrorCode::CUB_ERROR_CODE_SUCCESS;

  if (uMsg == WM_CREATE)
    {
      CREATESTRUCT *pCreate = (CREATESTRUCT *)lParam;
      app = (TrayApp *)pCreate->lpCreateParams;
      SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR)app);
    }
  else
    {
      app = (TrayApp *)GetWindowLongPtr (hWnd, GWLP_USERDATA);
    }

  switch (uMsg)
    {
    case WM_TRAYICON:
      if (lParam == WM_RBUTTONUP)
	{
	  if (app)
	    {
	      app->ShowContextMenu();
	    }
	}
      break;

    case WM_COMMAND:
      if (app)
	{
	  switch (LOWORD (wParam))
	    {
	    case TrayApp::MENU_ABOUT:
	      app->ShowAboutDialog();
	      Logger::GetInstance().LogInfo ("About dialog shown.");
	      break;
	    case TrayApp::MENU_START:
	      app->StartCUBRIDService();
	      Logger::GetInstance().LogInfo ("CUBRID service started successfully.");
	      break;
	    case TrayApp::MENU_STOP:
	      app->StopCUBRIDService();
	      Logger::GetInstance().LogInfo ("CUBRID service stopped successfully.");
	      break;
	    case TrayApp::MENU_README:
	      errorCode = app->ShowGuideFile();
	      if (errorCode == CUBRIDErrorCode::CUB_ERROR_CODE_SUCCESS)
		{
		  Logger::GetInstance().LogInfo ("Readme dialog shown.");
		}
	      else
		{
		  MessageBoxA (NULL, app->ErrorCodeToString (errorCode).c_str(), TRAY_APP_TITLE, MB_OK | MB_ICONERROR);
		}
	      break;
	    case TrayApp::MENU_EXIT:
	      app->Shutdown();
	      Logger::GetInstance().LogInfo ("Tray application shut down.");
	      PostQuitMessage (0);
	      break;
	    }
	}
      break;

    case WM_DESTROY:
      PostQuitMessage (0);
      break;

    default:
      return DefWindowProc (hWnd, uMsg, wParam, lParam);
    }

  return 0;
}

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  SetCurrentDirectoryA (SystemUtil::GetSystemDir().c_str());

  Logger::GetInstance().Initialize();

  HANDLE hMutex = CreateMutexA (NULL, TRUE, TRAY_MUTEX_NAME);
  if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
      Logger::GetInstance().LogInfo ("Another instance of CUBRID Tray App is already running. Exiting.");
      return 0;
    }

  Logger::GetInstance().LogStart ("CUBRID WSL Tray Application");

  TrayApp trayApp;
  CUBRIDErrorCode errorCode = trayApp.Initialize();
  if (errorCode == CUBRIDErrorCode::CUB_ERROR_CODE_SUCCESS)
    {
      trayApp.Run();
    }
  else
    {
      Logger::GetInstance().LogError ("Failed to initialize tray application");
      std::string errorMessage = "Failed to initialize tray application.\n";
      errorMessage += trayApp.ErrorCodeToString (errorCode);
      MessageBoxA (NULL, errorMessage.c_str(), TRAY_APP_TITLE, MB_OK | MB_ICONERROR);

      CloseHandle (hMutex);
      Logger::GetInstance().LogEnd ("CUBRID WSL Tray Application", false);
      return 1;
    }

  CloseHandle (hMutex);
  Logger::GetInstance().LogEnd ("CUBRID WSL Tray Application", true);
  return 0;
}