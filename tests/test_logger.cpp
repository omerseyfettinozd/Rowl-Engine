/**
 * test_logger.cpp — Logger timestamp formatting through the public log() path.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_logger_timestamp() {
    TEST_SECTION("Logger Timestamp Subsystem");

    // Exercises Logger::formatTimestamp() through the public log() path.
    // Regression guard for the Windows build where POSIX localtime_r does
    // not exist and localtime_s must be used instead.
    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() / "rowl_logger_timestamp_test.log";
    std::error_code ec;
    std::filesystem::remove(logPath, ec);

    Rowl::Core::Logger::init(logPath.string());
    Rowl::Core::Logger::setLogLevel(Rowl::Core::LogLevel::Trace);
    Rowl::Core::Logger::info("logger timestamp smoke");

    std::ifstream logFile(logPath);
    if (!logFile.is_open()) {
        std::cerr << "Logger test log file was not created" << std::endl;
        exit(1);
    }
    std::string line;
    std::getline(logFile, line);
    if (line.find("logger timestamp smoke") == std::string::npos) {
        std::cerr << "Logger test message missing from log file" << std::endl;
        exit(1);
    }
    // Expected prefix: [YYYY-MM-DD HH:MM:SS.mmm]
    if (line.size() < 25 || line[0] != '[' || line[5] != '-' || line[8] != '-' ||
        line[11] != ' ' || line[14] != ':' || line[17] != ':' || line[20] != '.' ||
        line[24] != ']') {
        std::cerr << "Logger timestamp format invalid: " << line << std::endl;
        exit(1);
    }
    TEST_PASS("Logger timestamp formats and writes through portable localtime");
}
