#include "rowl/core/logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace Rowl::Core {

LogLevel Logger::s_logLevel = LogLevel::Trace;
std::mutex Logger::s_logMutex;
bool Logger::s_initialized = false;
std::unique_ptr<std::ofstream> Logger::s_logFile;
std::atomic<RowlLogCallbackFn> Logger::s_callback{nullptr};
std::mutex Logger::s_bufferMutex;
std::deque<LogEntryRecord> Logger::s_recentLogs;
size_t Logger::s_logFileSize = 0;
std::string Logger::s_logPath;

void Logger::init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (!s_initialized) {
        s_initialized = true;
        s_logLevel = LogLevel::Trace;

        if (!logFile.empty()) {
            s_logPath = logFile;
            s_logFile = std::make_unique<std::ofstream>(s_logPath, std::ios::app);
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

void Logger::setLogCallback(RowlLogCallbackFn cb) {
    s_callback.store(cb, std::memory_order_release);
}

std::string Logger::getRecentLogsJson(int minLevel, size_t maxCount) {
    std::lock_guard<std::mutex> lock(s_bufferMutex);
    std::stringstream ss;
    ss << "[";
    bool first = true;
    size_t count = 0;
    for (auto it = s_recentLogs.rbegin(); it != s_recentLogs.rend() && count < maxCount; ++it) {
        if (static_cast<int>(it->level) < minLevel) continue;
        if (!first) ss << ",";
        first = false;
        ss << "{\"level\":" << static_cast<int>(it->level)
           << ",\"levelStr\":\"" << it->levelStr << "\""
           << ",\"timestamp\":\"" << it->timestamp << "\""
           << ",\"message\":\"";
        for (char c : it->message) {
            if (c == '"') ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else if (static_cast<unsigned char>(c) < 32) ss << " ";
            else ss << c;
        }
        ss << "\"}";
        count++;
    }
    ss << "]";
    return ss.str();
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
#ifdef _WIN32
    localtime_s(&tmBuf, &in_time_t);
#else
    localtime_r(&in_time_t, &tmBuf);
#endif
    ss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::rotateLogFile() {
    if (!s_logFile || !s_logFile->is_open() || s_logPath.empty()) return;

    if (s_logFileSize >= MAX_LOG_FILE_SIZE) {
        s_logFile->close();

        // Rename current log to .1, .2, .3 (keep 3 backups), beside the
        // configured path — never a hardcoded filename (A1, #35). Rename
        // failures are checked and reported; the worst case is a skipped
        // generation, never silent loss or a logging stall.
        std::error_code fsError;
        for (int i = 2; i >= 0; --i) {
            const std::string src = (i == 0) ? s_logPath : s_logPath + "." + std::to_string(i);
            const std::string dst = s_logPath + "." + std::to_string(i + 1);
            if (std::filesystem::exists(src, fsError) && !fsError) {
                std::filesystem::rename(src, dst, fsError);
                if (fsError) {
                    std::cout << "[Logger] rotation rename failed: '" << src << "' -> '"
                              << dst << "': " << fsError.message() << std::endl;
                    fsError.clear();
                }
            } else if (fsError) {
                fsError.clear();
            }
        }

        s_logFile = std::make_unique<std::ofstream>(s_logPath, std::ios::app);
        if (!s_logFile->is_open()) {
            std::cout << "[Logger] rotation reopen failed: '" << s_logPath << "'" << std::endl;
            s_logFile.reset();
        }
        s_logFileSize = 0;
    }
}

void Logger::log(LogLevel level, std::string_view msg) {
    std::string timestamp;
    std::string levelStrStr;
    std::string msgStr(msg);

    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (level < s_logLevel) {
            return;
        }

        timestamp = formatTimestamp();
        std::string_view levelStr = logLevelToString(level);
        levelStrStr = std::string(levelStr);

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

        std::string logLine = "[" + timestamp + "] [" + std::string(colorCode) + std::string(levelStr) + colorReset + "] " + msgStr;

        // Console output
        std::cout << logLine << std::endl;

        // File output with rotation
        if (s_logFile && s_logFile->is_open()) {
            rotateLogFile();
            if (s_logFile && s_logFile->is_open()) {
                *s_logFile << "[" << timestamp << "] [" << levelStr << "] " << msgStr << std::endl;
                s_logFile->flush();
                s_logFileSize += logLine.size() + 1;
            }
        }
    }

    // Save to ring buffer
    {
        std::lock_guard<std::mutex> bLock(s_bufferMutex);
        s_recentLogs.push_back({level, levelStrStr, msgStr, timestamp});
        while (s_recentLogs.size() > MAX_RECENT_LOGS) {
            s_recentLogs.pop_front();
        }
    }

    // Invoke callback outside lock
    auto cb = s_callback.load(std::memory_order_acquire);
    if (cb) {
        try {
            cb(static_cast<int>(level), levelStrStr.c_str(), msgStr.c_str(), timestamp.c_str());
        } catch (...) {
            // Guarantee no exception leaks across boundary
        }
    }
}

} // namespace Rowl::Core