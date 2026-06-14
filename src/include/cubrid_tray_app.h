#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

enum TrayIconState : int
{
  TRAY_STATE_STOPPED = 0,
  TRAY_STATE_RUNNING = 1,
  TRAY_STATE_ERROR = 2
};

#define WM_TRAYICON (WM_USER + 1)

enum class CUBRIDStatus
{
  CUB_STATUS_RUNNING,
  CUB_STATUS_STOPPED,
  CUB_STATUS_ERROR
};

enum CUBRIDErrorCode : int
{
  CUB_ERROR_CODE_SUCCESS = 0,
  CUB_ERROR_CODE_INVALID_WSL_NAME = -1,
  CUB_ERROR_CODE_FAILED_TO_REGISTER_WINDOW_CLASS = -2,
  CUB_ERROR_CODE_FAILED_TO_CREATE_WINDOW = -3,
  CUB_ERROR_CODE_FAILED_TO_CREATE_TRAY_ICON = -4,
  CUB_ERROR_CODE_FAILED_TO_GET_INSTALL_DIR = -5,
  CUB_ERROR_CODE_FAILED_TO_SHOW_README_FILE = -6,
};

class TrayApp
{
  public:
    TrayApp();
    ~TrayApp();

    CUBRIDErrorCode Initialize();
    void Run();
    void Shutdown();

    CUBRIDStatus GetCUBRIDStatus();
    std::string GetCUBRIDVersion();
    bool StartCUBRIDService();
    bool StopCUBRIDService();

    void ShowAboutDialog();
    void ShowContextMenu();
    void UpdateTrayStatus();

    void MonitorCUBRIDStatus();
    std::string ErrorCodeToString (CUBRIDErrorCode errorCode);

    static const UINT MENU_ABOUT;
    static const UINT MENU_START;
    static const UINT MENU_STOP;
    static const UINT MENU_README;
    static const UINT MENU_EXIT;

    static const char *WINDOW_CLASS_NAME;

    HWND hWnd;
    HICON hIcon;
    bool isRunning;
    std::string logDir;

  private:
    std::string wslName;
    std::string installDir;
    HANDLE wslProcessHandle;
    HWND hwnd_;
    NOTIFYICONDATA nid_;
    HMENU trayMenu_;
    HICON iconStopped_;
    HICON iconRunning_;
    HICON iconError_;

    std::atomic<bool> running_;
    std::thread monitorThread_;
    std::atomic<TrayIconState> currentState_;

    static LRESULT CALLBACK WindowProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool AddTrayIcon();
    bool RemoveTrayIcon();

    bool StartCUBRID();
    bool StopCUBRID();

    CUBRIDErrorCode ShowGuideFile();

    std::string ExecuteWSLCommand (const std::string &command);

    void ShowAboutDialogPrivate();
    void ShowErrorDialog (const std::string &message);

    void StartStatusMonitor();
    void StopStatusMonitor();
    void MonitorThread();

    bool GetWSLNameFromRegistry (std::string &wslName);
    bool GetInstallDirFromRegistry (std::string &installDir);

    std::string BuildWslCommand (const std::string &bashScript, bool detached = false) const;

};