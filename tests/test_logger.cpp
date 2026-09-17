/**
 * test_logger.cpp — Logger timestamp formatting through the public log() path
 * plus the first concurrency test (T0b test hijyeni).
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

namespace {

// Hermetic per-run log path: Logger::init() is once-only process-wide, so
// both logger tests share this path (a second init() is a harmless no-op).
// The run tag also scopes the concurrency markers for exact counting under
// ios::app across reruns.
std::string loggerRunTag() {
    static const std::string tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return tag;
}

std::string loggerHermeticPath() {
    return (std::filesystem::temp_directory_path() /
            ("rowl_logger_hermetic_" + loggerRunTag() + ".log"))
        .string();
}

} // namespace

void test_logger_timestamp() {
    TEST_SECTION("Logger Timestamp Subsystem");

    // Exercises Logger::formatTimestamp() through the public log() path.
    // Regression guard for the Windows build where POSIX localtime_r does
    // not exist and localtime_s must be used instead.
    // T0b: the fixed temp name is now a hermetic per-run path so parallel
    // runs and reruns can never observe each other's file.
    const std::string logPath = loggerHermeticPath();

    Rowl::Core::Logger::init(logPath);
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

void test_logger_concurrency() {
    TEST_SECTION("Logger Concurrency Subsystem");

    // First concurrency test (T0b): 8 threads log through the public static
    // path at once. Logger serializes on s_logMutex and flushes every line,
    // so the run-tagged markers must arrive exactly threads*msgs times —
    // a crash, an interleaved/corrupt line, or a lost write fails loudly.
    const std::string logPath = loggerHermeticPath();
    Rowl::Core::Logger::init(logPath); // no-op if the timestamp test ran first
    Rowl::Core::Logger::setLogLevel(Rowl::Core::LogLevel::Trace);

    const std::string marker = "rowl-conc-" + loggerRunTag() + "-";
    constexpr int kThreads = 8;
    constexpr int kMessages = 50;
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t] {
            ready.fetch_add(1);
            while (!go.load()) std::this_thread::yield();
            for (int i = 0; i < kMessages; ++i) {
                Rowl::Core::Logger::info(marker + "t" + std::to_string(t) +
                                         "m" + std::to_string(i));
            }
        });
    }
    while (ready.load() < kThreads) std::this_thread::yield();
    go.store(true);
    for (auto& worker : workers) worker.join();

    std::ifstream logFile(logPath);
    if (!logFile.is_open()) {
        std::cerr << "Logger concurrency log file was not created" << std::endl;
        exit(1);
    }
    int hits = 0;
    std::string line;
    while (std::getline(logFile, line)) {
        if (line.find(marker) != std::string::npos) ++hits;
    }
    if (hits != kThreads * kMessages) {
        std::cerr << "Logger lost interleaved messages: expected "
                  << (kThreads * kMessages) << ", found " << hits << std::endl;
        exit(1);
    }
    TEST_PASS("Logger concurrent logging from 8 threads without loss");
}
