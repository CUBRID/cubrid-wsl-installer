#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "logger.h"

static MSIHANDLE g_hInstall = 0;

void SetGlobalMsiHandle (MSIHANDLE hInstall)
{
  g_hInstall = hInstall;
}

Logger &Logger::GetInstance()
{
  static Logger instance;
  return instance;
}

Logger::Logger() : debugLogEnabled_ (false) {}

Logger::~Logger()
{
  CloseDebugLog();
}

bool Logger::Initialize()
{
  bool shouldLog = (DEBUG_LOG == 1);

  if (!shouldLog)
    {
      char exePath[MAX_PATH] = {};
      if (GetModuleFileNameA (NULL, exePath, MAX_PATH) != 0)
	{
	  std::filesystem::path marker =
		  std::filesystem::path (exePath).parent_path() / "_debug_cubrid";
	  shouldLog = std::filesystem::exists (marker);
	}
    }

  if (shouldLog)
    {
      InitializeDebugLog();
    }
  return true;
}

bool Logger::InitializeDebugLog()
{
  std::lock_guard<std::mutex> lock (mutex_);

  if (debugLogEnabled_ && logFile_.is_open())
    {
      return true;
    }

  try
    {
      char tempPath[MAX_PATH];
      if (GetTempPathA (MAX_PATH, tempPath) == 0)
	{
	  std::cout << "[WARNING] Failed to get TEMP directory for debug logging." << std::endl;
	  return false;
	}

      std::filesystem::path logDir = std::filesystem::path (tempPath) / "CUBRID" / "log";
      try
	{
	  std::filesystem::create_directories (logDir);
	}
      catch (const std::filesystem::filesystem_error &e)
	{
	  std::cout << "[WARNING] Failed to create log directory: " << e.what() << std::endl;
	  return false;
	}

      auto now = std::time (nullptr);
      std::tm timeInfo;
      localtime_s (&timeInfo, &now);

      std::ostringstream oss;
      oss << "cubrid_wsl_"
	  << std::setfill ('0') << std::setw (4) << (timeInfo.tm_year + 1900)
	  << std::setw (2) << (timeInfo.tm_mon + 1)
	  << std::setw (2) << timeInfo.tm_mday
	  << "_"
	  << std::setw (2) << timeInfo.tm_hour
	  << std::setw (2) << timeInfo.tm_min
	  << std::setw (2) << timeInfo.tm_sec
	  << ".log";

      logFilePath_ = (logDir / oss.str()).string();

      logFile_.open (logFilePath_, std::ios::app);
      if (!logFile_.is_open())
	{
	  std::cout << "[WARNING] Failed to open log file: " << logFilePath_ << std::endl;
	  return false;
	}

      debugLogEnabled_ = true;

      char timeStr[100];
      std::strftime (timeStr, sizeof (timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);
      logFile_ << "[" << timeStr << "] === Debug Log Started ===" << std::endl;
      logFile_.flush();

      return true;
    }
  catch (const std::exception &e)
    {
      std::cout << "[WARNING] Exception in InitializeDebugLog: " << e.what() << std::endl;
      return false;
    }
  catch (...)
    {
      std::cout << "[WARNING] Unknown exception in InitializeDebugLog." << std::endl;
      return false;
    }
}

void Logger::CloseDebugLog()
{
  std::lock_guard<std::mutex> lock (mutex_);
  if (logFile_.is_open())
    {
      auto now = std::time (nullptr);
      std::tm timeInfo;
      localtime_s (&timeInfo, &now);
      char timeStr[100];
      std::strftime (timeStr, sizeof (timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);
      logFile_ << "[" << timeStr << "] === Debug Log Ended ===" << std::endl;
      logFile_.close();
    }
  debugLogEnabled_ = false;
}

void Logger::Log (const std::string &message)
{
  std::lock_guard<std::mutex> lock (mutex_);
  auto now = std::time (nullptr);
  std::tm timeInfo;
  localtime_s (&timeInfo, &now);
  char timeStr[100];
  std::strftime (timeStr, sizeof (timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);

  std::string logMessage = "[" + std::string (timeStr) + "] " + message;

  std::cout << logMessage << std::endl;

  if (debugLogEnabled_ && logFile_.is_open())
    {
      logFile_ << logMessage << std::endl;
      logFile_.flush();
    }

  if (g_hInstall != 0)
    {
      PMSIHANDLE hRecord = MsiCreateRecord (0);
      if (hRecord)
	{
	  MsiRecordSetStringA (hRecord, 0, logMessage.c_str());
	  MsiProcessMessage (g_hInstall, INSTALLMESSAGE_INFO, hRecord);
	}
    }
}

void Logger::LogError (const std::string &message)
{
  Log ("[ERROR] " + message);
}

void Logger::LogInfo (const std::string &message)
{
  Log ("[INFO] " + message);
}

void Logger::LogWarning (const std::string &message)
{
  Log ("[WARNING] " + message);
}

void Logger::LogStart (const std::string &operation)
{
  Log ("=== " + operation + " Started ===");
}

void Logger::LogEnd (const std::string &operation, bool success)
{
  Log ("=== " + operation + " Completed: " + (success ? "Success" : "Failed") + " ===");
}