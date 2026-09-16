#include "rowl/platform/crash_handler.hpp"

#ifdef _WIN32

// Fail-closed stub: install is a no-op; see docs/CRASH_LOG_CONTRACT.md.
namespace Rowl::Platform {
bool RowlCrash_Install(const char*, int, char*[]) { return false; }
void RowlCrash_Uninstall() {}
bool RowlCrash_IsInstalled() { return false; }
void RowlCrash_RefreshSnapshot(int32_t, const char*, const char*) {}
}  // namespace Rowl::Platform

#else

#include <cerrno>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace Rowl::Platform {
namespace {

// Fixed bounds keep the crash path clear of heap use.
constexpr unsigned kMaxDir = 1024;
constexpr unsigned kMaxArgv = 1024;
constexpr unsigned kMaxOperation = 128;
constexpr unsigned kMaxTarget = 256;
constexpr unsigned kMaxPath = kMaxDir + 64;
constexpr unsigned kMaxFileAttempts = 100;
constexpr int kCrashSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL};

char g_logDir[kMaxDir] = {};
char g_argvEcho[kMaxArgv] = {};
char g_operation[kMaxOperation] = {};
char g_target[kMaxTarget] = {};
int32_t g_resultCode = 0;
volatile sig_atomic_t g_installed = 0;
volatile sig_atomic_t g_sequence = 0;
std::terminate_handler g_previousTerminate = nullptr;

// Copy at most cap-1 bytes plus NUL. Runs in normal control flow only.
unsigned copyBounded(char* dst, const char* src, unsigned cap) {
    if (cap == 0 || dst == nullptr) {
        return 0;
    }
    unsigned count = 0;
    if (src != nullptr) {
        while (count + 1 < cap && src[count] != '\0') {
            dst[count] = src[count];
            ++count;
        }
    }
    dst[count] = '\0';
    return count;
}

unsigned strLength(const char* text) {
    unsigned count = 0;
    if (text != nullptr) {
        while (text[count] != '\0') {
            ++count;
        }
    }
    return count;
}

// write(2) retried on EINTR; partial writes loop until done.
bool writeAll(int fd, const char* data, unsigned length) {
    unsigned done = 0;
    while (done < length) {
        const ssize_t written = write(fd, data + done, length - done);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        done += static_cast<unsigned>(written);
    }
    return true;
}

void writeText(int fd, const char* text) {
    writeAll(fd, text, strLength(text));
}

unsigned appendInteger(char* buf, unsigned pos, unsigned cap, long value) {
    const bool negative = value < 0;
    unsigned long rest = negative ? static_cast<unsigned long>(-(value + 1)) + 1u
                                  : static_cast<unsigned long>(value);
    char rev[32];
    unsigned digits = 0;
    do {
        rev[digits++] = static_cast<char>('0' + (rest % 10u));
        rest /= 10u;
    } while (rest > 0 && digits < sizeof(rev));
    if (negative && pos < cap) {
        buf[pos++] = '-';
    }
    while (digits > 0 && pos < cap) {
        buf[pos++] = rev[--digits];
    }
    return pos;
}

// Decimal rendering without stdio or heap use.
void writeInteger(int fd, long value) {
    char buf[32];
    writeAll(fd, buf, appendInteger(buf, 0, sizeof(buf), value));
}

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGABRT:
            return "SIGABRT";
        case SIGFPE:
            return "SIGFPE";
        case SIGILL:
            return "SIGILL";
        default:
            return "UNKNOWN";
    }
}

// Crash-context writer: open/write/close plus plain buffer reads only.
void emitCrashLog(const char* reason) {
    const long pid = static_cast<long>(getpid());
    int fd = -1;
    for (unsigned attempt = 0; attempt < kMaxFileAttempts; ++attempt) {
        char path[kMaxPath];
        unsigned pos = 0;
        const unsigned cap = sizeof(path) - 1;  // reserve NUL
        for (unsigned i = 0; g_logDir[i] != '\0' && pos < cap; ++i) {
            path[pos++] = g_logDir[i];
        }
        if (pos > 0 && path[pos - 1] != '/' && pos < cap) {
            path[pos++] = '/';
        }
        const char* stem = "crash-";
        for (unsigned i = 0; stem[i] != '\0' && pos < cap; ++i) {
            path[pos++] = stem[i];
        }
        pos = appendInteger(path, pos, cap, pid);
        if (pos < cap) {
            path[pos++] = '-';
        }
        pos = appendInteger(path, pos, cap, static_cast<long>(g_sequence));
        const char* ext = ".log";
        for (unsigned i = 0; ext[i] != '\0' && pos < cap; ++i) {
            path[pos++] = ext[i];
        }
        path[pos] = '\0';
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            break;
        }
        // Name taken or directory unusable: advance and retry so a repeat
        // crash landing in one directory still gets a distinct file, while
        // a missing directory simply exhausts attempts and stays silent.
        // (Assignment form: ++ on volatile sig_atomic_t is deprecated.)
        g_sequence = static_cast<sig_atomic_t>(g_sequence + 1);
    }
    if (fd < 0) {
        return;
    }
    writeText(fd, "rowl-crash-log v1\n");
    writeText(fd, "reason: ");
    writeText(fd, reason);
    writeText(fd, "\n");
    writeText(fd, "pid: ");
    writeInteger(fd, pid);
    writeText(fd, "\n");
    writeText(fd, "argv: ");
    writeText(fd, g_argvEcho);
    writeText(fd, "\n");
    writeText(fd, "result-code: ");
    writeInteger(fd, static_cast<long>(g_resultCode));
    writeText(fd, "\n");
    writeText(fd, "result-operation: ");
    writeText(fd, g_operation);
    writeText(fd, "\n");
    writeText(fd, "result-target: ");
    writeText(fd, g_target);
    writeText(fd, "\n");
    writeText(fd, "note: snapshot reflects the last safe-point refresh\n");
    close(fd);
}

void crashSignalHandler(int sig) {
    emitCrashLog(signalName(sig));
    // The signal is blocked while this handler runs (sa_mask); unblock it
    // before re-raising, otherwise raise() just returns and the process
    // would exit normally instead of dying by the original signal.
    sigset_t unblock;
    sigemptyset(&unblock);
    sigaddset(&unblock, sig);
    sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(128 + sig);
}

void crashTerminateHook() noexcept {
    emitCrashLog("terminate");
    _exit(134);
}

}  // namespace

bool RowlCrash_Install(const char* logDir, int argc, char* argv[]) {
    if (g_installed != 0) {
        return true;
    }
    if (logDir == nullptr || logDir[0] == '\0') {
        return false;
    }
    copyBounded(g_logDir, logDir, sizeof(g_logDir));
    unsigned pos = 0;
    const unsigned cap = sizeof(g_argvEcho) - 1;
    for (int i = 0; i < argc && pos < cap; ++i) {
        const char* item = (argv != nullptr && argv[i] != nullptr) ? argv[i] : "";
        if (i > 0 && pos < cap) {
            g_argvEcho[pos++] = ' ';
        }
        for (unsigned k = 0; item[k] != '\0' && pos < cap; ++k) {
            char c = item[k];
            if (c == '\n' || c == '\r') {
                c = ' ';
            }
            g_argvEcho[pos++] = c;
        }
    }
    g_argvEcho[pos] = '\0';
    g_resultCode = 0;
    g_operation[0] = '\0';
    g_target[0] = '\0';

    struct sigaction act {};
    act.sa_handler = crashSignalHandler;
    sigemptyset(&act.sa_mask);
    constexpr unsigned kSignalCount = sizeof(kCrashSignals) / sizeof(kCrashSignals[0]);
    for (unsigned i = 0; i < kSignalCount; ++i) {
        sigaddset(&act.sa_mask, kCrashSignals[i]);
    }
    act.sa_flags = SA_RESETHAND;
    for (unsigned i = 0; i < kSignalCount; ++i) {
        if (sigaction(kCrashSignals[i], &act, nullptr) != 0) {
            RowlCrash_Uninstall();
            return false;
        }
    }
    g_previousTerminate = std::set_terminate(crashTerminateHook);
    g_installed = 1;
    return true;
}

void RowlCrash_Uninstall() {
    constexpr unsigned kSignalCount = sizeof(kCrashSignals) / sizeof(kCrashSignals[0]);
    for (unsigned i = 0; i < kSignalCount; ++i) {
        signal(kCrashSignals[i], SIG_DFL);
    }
    if (g_installed != 0) {
        std::set_terminate(g_previousTerminate);
        g_installed = 0;
    }
}

bool RowlCrash_IsInstalled() {
    return g_installed != 0;
}

void RowlCrash_RefreshSnapshot(int32_t resultCode, const char* operation, const char* target) {
    g_resultCode = resultCode;
    copyBounded(g_operation, operation, sizeof(g_operation));
    copyBounded(g_target, target, sizeof(g_target));
}

}  // namespace Rowl::Platform

#endif  // _WIN32
