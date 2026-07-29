#include "rowl/core/logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace Rowl::Core {

LogLevel Logger::s_logLevel = LogLevel::Trace;
std::mutex Logger::s_logMutex;
bool Logger::s_initialized = false;

void Logger::init() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (!s_initialized) {
        s_initialized = true;
        s_logLevel = LogLevel::Trace;
    }
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_logLevel = level;
}

LogLevel Logger::getLogLevel() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    return s_logLevel;
}

std::string_view Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warn:     return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

std::string Logger::formatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    std::tm tmBuf{};
    localtime_r(&in_time_t, &tmBuf);
    ss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::log(LogLevel level, std::string_view msg) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (level < s_logLevel) {
        return;
    }

    std::string timestamp = formatTimestamp();
    std::string_view levelStr = logLevelToString(level);

    // Terminal ANSI colors
    const char* colorReset = "\033[0m";
    const char* colorCode = "";

    switch (level) {
        case LogLevel::Trace:    colorCode = "\033[37m"; break; // White
        case LogLevel::Debug:    colorCode = "\033[36m"; break; // Cyan
        case LogLevel::Info:     colorCode = "\033[32m"; break; // Green
        case LogLevel::Warn:     colorCode = "\033[33m"; break; // Yellow
        case LogLevel::Error:    colorCode = "\033[31m"; break; // Red
        case LogLevel::Critical: colorCode = "\033[35m"; break; // Magenta
    }

    std::cout << "[" << timestamp << "] [" << colorCode << levelStr << colorReset << "] " << msg << std::endl;
}

} // namespace Rowl::Core
