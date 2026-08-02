#include "rowl/core/logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace Rowl::Core {

LogLevel Logger::s_logLevel = LogLevel::Trace;
std::mutex Logger::s_logMutex;
bool Logger::s_initialized = false;
std::unique_ptr<std::ofstream> Logger::s_logFile;
size_t Logger::s_logFileSize = 0;

void Logger::init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (!s_initialized) {
        s_initialized = true;
        s_logLevel = LogLevel::Trace;

        if (!logFile.empty()) {
            s_logFile = std::make_unique<std::ofstream>(logFile, std::ios::app);
            if (s_logFile && s_logFile->is_open()) {
                // Get current file size
                s_logFile->seekp(0, std::ios::end);
                s_logFileSize = static_cast<size_t>(s_logFile->tellp());
            }
        }
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

void Logger::rotateLogFile() {
    if (!s_logFile || !s_logFile->is_open()) return;

    if (s_logFileSize >= MAX_LOG_FILE_SIZE) {
        s_logFile->close();

        // Rename current log to .1, .2, .3 (keep 3 backups)
        for (int i = 2; i >= 0; --i) {
            std::string src = (i == 0) ? "rowl_engine.log" : "rowl_engine.log." + std::to_string(i);
            std::string dst = "rowl_engine.log." + std::to_string(i + 1);
            if (std::filesystem::exists(src)) {
                std::filesystem::rename(src, dst);
            }
        }

        s_logFile = std::make_unique<std::ofstream>("rowl_engine.log", std::ios::app);
        s_logFileSize = 0;
    }
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

    std::string logLine = "[" + timestamp + "] [" + std::string(colorCode) + std::string(levelStr) + colorReset + "] " + std::string(msg);

    // Console output
    std::cout << logLine << std::endl;

    // File output with rotation
    if (s_logFile && s_logFile->is_open()) {
        rotateLogFile();
        *s_logFile << "[" << timestamp << "] [" << levelStr << "] " << msg << std::endl;
        s_logFile->flush();
        s_logFileSize += logLine.size() + 1;
    }
}

} // namespace Rowl::Core