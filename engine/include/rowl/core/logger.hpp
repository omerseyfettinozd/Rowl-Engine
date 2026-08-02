#pragma once

#include <string>
#include <string_view>
#include <mutex>
#include <memory>
#include <iostream>
#include <fstream>

namespace Rowl::Core {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};

class Logger {
public:
    static void init(const std::string& logFile = "");
    static void setLogLevel(LogLevel level);
    static LogLevel getLogLevel();

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