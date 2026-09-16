/**
 * test_crash_log.cpp — Player crash-log (Faz 6 Dilim 1).
 *
 * Covers:
 *   1. grep-gate: crash_handler sources contain no async-signal-unsafe calls
 *      (malloc / printf / fopen / snprintf / new).
 *   2. POSIX: a forked child raising SIGSEGV emits crash-<pid>-0.log with the
 *      safe-point snapshot and dies by the same signal (re-raised, not masked).
 *   3. POSIX: a forked child hitting std::terminate emits a "terminate" log
 *      and exits 134.
 *   4. Parent-side install/uninstall round-trip (Windows: fail-closed stub).
 *
 * Every child case runs in an isolated temp directory with a waitpid timeout,
 * so a regression fails fast instead of hanging the suite.
 */
#include "rowl_test_harness.hpp"

#include "rowl/platform/crash_handler.hpp"

#include <cerrno>
#include <csignal>
#include <exception>
#include <sstream>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void checkCrashHandlerSourceGate() {
    const char* files[] = {
        "engine/src/platform/crash_handler.cpp",
        "engine/include/rowl/platform/crash_handler.hpp",
    };
    const char* tokens[] = {"malloc", "printf", "fopen", "snprintf", "new "};
    for (const char* path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "crash-log gate: cannot open " << path << " (run ctest from repo root)"
                      << std::endl;
            exit(1);
        }
        std::ostringstream body;
        body << in.rdbuf();
        const std::string text = body.str();
        for (const char* token : tokens) {
            if (text.find(token) != std::string::npos) {
                std::cerr << "crash-log gate: forbidden '" << token << "' found in " << path
                          << std::endl;
                exit(1);
            }
        }
    }
    TEST_PASS("crash_handler sources contain no forbidden calls (grep-gate)");
}

#ifndef _WIN32

int g_crashProbeCounter = 0;

std::filesystem::path makeCrashTempDir() {
    namespace fs = std::filesystem;
    const fs::path dir =
        fs::temp_directory_path() /
        ("rowl_crash_" + std::to_string(static_cast<long>(getpid())) + "_" +
         std::to_string(g_crashProbeCounter++));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "crash-log test: cannot create temp dir " << dir << std::endl;
        exit(1);
    }
    return dir;
}

// Waits up to timeoutSec for pid; kills and reports false on timeout.
bool waitChild(pid_t pid, int& status, int timeoutSec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    while (true) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            return true;
        }
        if (done < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "crash-log test: waitpid failed" << std::endl;
            exit(1);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Forks a child that installs the handler, refreshes a sentinel snapshot and
// then crashes. Only async-signal-safe calls run between fork and crash.
pid_t spawnCrashingChild(const std::string& dir, bool viaTerminate) {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "crash-log test: fork failed" << std::endl;
        exit(1);
    }
    if (pid == 0) {
        char prog[] = "rowl_probe";
        char first[] = "--alpha";
        char second[] = "beta gamma";
        char* args[] = {prog, first, second, nullptr};
        Rowl::Platform::RowlCrash_Install(dir.c_str(), 3, args);
        Rowl::Platform::RowlCrash_RefreshSnapshot(7, "load_story_graph_unit",
                                                  "/tmp/unit/story.json");
        if (viaTerminate) {
            std::terminate();
        } else {
            raise(SIGSEGV);
        }
        _exit(99);  // Unreached: the signal path re-raises, terminate exits.
    }
    return pid;
}

void expectCrashLog(const std::filesystem::path& dir, pid_t child, const std::string& reason) {
    const std::string name =
        "crash-" + std::to_string(static_cast<long>(child)) + "-0.log";
    const std::filesystem::path path = dir / name;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "crash-log test: missing expected log " << path << std::endl;
        exit(1);
    }
    std::ostringstream body;
    body << in.rdbuf();
    const std::string text = body.str();
    const char* required[] = {"rowl-crash-log v1",           reason.c_str(),
                              "result-code: 7",              "result-operation: load_story_graph_unit",
                              "result-target: /tmp/unit/story.json", "beta gamma"};
    for (const char* want : required) {
        if (text.find(want) == std::string::npos) {
            std::cerr << "crash-log test: log " << path << " misses '" << want << "'\n"
                      << text << std::endl;
            exit(1);
        }
    }
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        ++count;
    }
    if (count != 1) {
        std::cerr << "crash-log test: expected exactly 1 log in " << dir << ", saw " << count
                  << std::endl;
        exit(1);
    }
}

#endif  // _WIN32

}  // namespace

void test_crash_log() {
    TEST_SECTION("Player Crash Log (Faz 6 Dilim 1)");

    checkCrashHandlerSourceGate();

#ifdef _WIN32
    if (Rowl::Platform::RowlCrash_Install("crash-logs", 0, nullptr)) {
        std::cerr << "crash-log test: Windows stub install must report false" << std::endl;
        exit(1);
    }
    if (Rowl::Platform::RowlCrash_IsInstalled()) {
        std::cerr << "crash-log test: Windows stub must never report installed" << std::endl;
        exit(1);
    }
    Rowl::Platform::RowlCrash_RefreshSnapshot(1, "op", "target");  // Must stay a safe no-op.
    Rowl::Platform::RowlCrash_Uninstall();
    TEST_PASS("Windows crash stub is fail-closed");
#else
    // Parent-side install/uninstall round-trip (no crash involved).
    {
        const auto dir = makeCrashTempDir();
        if (!Rowl::Platform::RowlCrash_Install(dir.c_str(), 0, nullptr) ||
            !Rowl::Platform::RowlCrash_IsInstalled()) {
            std::cerr << "crash-log test: install round-trip failed" << std::endl;
            exit(1);
        }
        Rowl::Platform::RowlCrash_RefreshSnapshot(0, "roundtrip", "here");
        Rowl::Platform::RowlCrash_Uninstall();
        if (Rowl::Platform::RowlCrash_IsInstalled()) {
            std::cerr << "crash-log test: uninstall did not clear installed flag" << std::endl;
            exit(1);
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        TEST_PASS("crash handler install/uninstall round-trip");
    }

    // SIGSEGV child: log emitted, process still dies by SIGSEGV.
    {
        const auto dir = makeCrashTempDir();
        const pid_t child = spawnCrashingChild(dir.string(), false);
        int status = 0;
        if (!waitChild(child, status, 10)) {
            std::cerr << "crash-log test: SIGSEGV child timed out" << std::endl;
            exit(1);
        }
        if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV) {
            std::cerr << "crash-log test: SIGSEGV child did not die by SIGSEGV (status "
                      << status << ")" << std::endl;
            exit(1);
        }
        expectCrashLog(dir, child, "SIGSEGV");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        TEST_PASS("SIGSEGV child emits crash log and exits by signal");
    }

    // std::terminate child: "terminate" log, exit code 134.
    {
        const auto dir = makeCrashTempDir();
        const pid_t child = spawnCrashingChild(dir.string(), true);
        int status = 0;
        if (!waitChild(child, status, 10)) {
            std::cerr << "crash-log test: terminate child timed out" << std::endl;
            exit(1);
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 134) {
            std::cerr << "crash-log test: terminate child exit was " << status
                      << ", expected exit(134)" << std::endl;
            exit(1);
        }
        expectCrashLog(dir, child, "terminate");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        TEST_PASS("terminate child emits crash log and exits 134");
    }
#endif
}
