#pragma once
#include <string>
#include <mutex>
#include <iostream>
#include <windows.h>
#include <msi.h>
#include <fstream>

#ifndef DEBUG_LOG
#define DEBUG_LOG 0
#endif

class Logger
{
  public:
    static Logger &GetInstance();

    bool Initialize();

    void Log (const std::string &message);
    void LogError (const std::string &message);
    void LogInfo (const std::string &message);
    void LogWarning (const std::string &message);
    void LogStart (const std::string &operation);
    void LogEnd (const std::string &operation, bool success);

    ~Logger();

  private:
    Logger();
    Logger (const Logger &) = delete;
    Logger &operator= (const Logger &) = delete;

    bool InitializeDebugLog();
    void CloseDebugLog();

    std::mutex mutex_;
    std::ofstream logFile_;
    std::string logFilePath_;
    bool debugLogEnabled_;
};

void SetGlobalMsiHandle (MSIHANDLE hInstall);