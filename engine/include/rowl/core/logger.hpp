#pragma once

#include <string>
#include <string_view>
#include <mutex>
#include <memory>
#include <iostream>
#include <fstream>
#include <deque>
#include <atomic>
#include <vector>

namespace Rowl::Core {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5
};

typedef void (*RowlLogCallbackFn)(int level, const char* levelStr, const char* message, const char* timestamp);

struct LogEntryRecord {
    LogLevel level;
    std::string levelStr;
    std::string message;
    std::string timestamp;
};

class Logger {
public:
    static void init(const std::string& logFile = "");
    static void setLogLevel(LogLevel level);
    static LogLevel getLogLevel();

    static void setLogCallback(RowlLogCallbackFn cb);
    static std::string getRecentLogsJson(int minLevel, size_t maxCount);

    static void log(LogLevel level, std::string_view msg);

    static void trace(std::string_view msg) { log(LogLevel::Trace, msg); }
    static void debug(std::string_view msg) { log(LogLevel::Debug, msg); }
    static void info(std::string_view msg)  { log(LogLevel::Info, msg); }
    static void warn(std::string_view msg)  { log(LogLevel::Warn, msg); }
    static void error(std::string_view msg) { log(LogLevel::Error, msg); }
    static void critical(std::string_view msg) { log(LogLevel::Critical, msg); }

private:
    static LogLevel s_logLevel;
    static std::mutex s_logMutex;
    static bool s_initialized;
    static std::unique_ptr<std::ofstream> s_logFile;
    static size_t s_logFileSize;
    static const size_t MAX_LOG_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
    /// A1 (#35): the exact path passed to init(). Rotation renames THIS file,
    /// never a hardcoded name, so a custom log path rotates beside itself.
    static std::string s_logPath;

    static std::atomic<RowlLogCallbackFn> s_callback;
    static std::mutex s_bufferMutex;
    static std::deque<LogEntryRecord> s_recentLogs;
    static const size_t MAX_RECENT_LOGS = 256;

    static std::string_view logLevelToString(LogLevel level);
    static std::string formatTimestamp();
    static void rotateLogFile();
};

} // namespace Rowl::Core

#define ROWL_LOG_TRACE(...) ::Rowl::Core::Logger::trace(__VA_ARGS__)
#define ROWL_LOG_DEBUG(...) ::Rowl::Core::Logger::debug(__VA_ARGS__)
#define ROWL_LOG_INFO(...)  ::Rowl::Core::Logger::info(__VA_ARGS__)
#define ROWL_LOG_WARN(...)  ::Rowl::Core::Logger::warn(__VA_ARGS__)
#define ROWL_LOG_ERROR(...) ::Rowl::Core::Logger::error(__VA_ARGS__)
#define ROWL_LOG_CRITICAL(...) ::Rowl::Core::Logger::critical(__VA_ARGS__)