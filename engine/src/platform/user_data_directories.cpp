#include "rowl/platform/user_data_directories.hpp"

#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#elif defined(__linux__)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Rowl::Platform {
namespace {

constexpr auto kApplicationDirectory = "rowl-engine";

std::filesystem::path absoluteEnvironmentPath(const char* name) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') return {};
    std::filesystem::path path(value);
    return path.is_absolute() ? path.lexically_normal() : std::filesystem::path{};
}

#if defined(_WIN32)
std::filesystem::path windowsLocalAppData() {
    PWSTR widePath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                       nullptr, &widePath)) && widePath) {
        std::filesystem::path result(widePath);
        CoTaskMemFree(widePath);
        return result.lexically_normal();
    }
    if (widePath) CoTaskMemFree(widePath);

    const wchar_t* fallback = _wgetenv(L"LOCALAPPDATA");
    if (fallback && fallback[0] != L'\0') {
        std::filesystem::path result(fallback);
        if (result.is_absolute()) return result.lexically_normal();
    }
    return {};
}
#elif defined(__linux__)
std::filesystem::path linuxHomeDirectory() {
    if (auto home = absoluteEnvironmentPath("HOME"); !home.empty()) return home;

    long requestedSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (requestedSize < 1024 || requestedSize > 1024 * 1024) {
        requestedSize = 16 * 1024;
    }
    std::vector<char> buffer(static_cast<std::size_t>(requestedSize));
    passwd entry{};
    passwd* result = nullptr;
    if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &result) == 0 &&
        result && entry.pw_dir && entry.pw_dir[0] != '\0') {
        std::filesystem::path home(entry.pw_dir);
        if (home.is_absolute()) return home.lexically_normal();
    }
    return {};
}
#endif

std::filesystem::path platformUserDataRoot() {
#if defined(_WIN32)
    if (auto root = windowsLocalAppData(); !root.empty()) {
        return root / kApplicationDirectory;
    }
#elif defined(__linux__)
    if (auto xdg = absoluteEnvironmentPath("XDG_DATA_HOME"); !xdg.empty()) {
        return xdg / kApplicationDirectory;
    }
    if (auto home = linuxHomeDirectory(); !home.empty()) {
        return home / ".local" / "share" / kApplicationDirectory;
    }
#elif defined(__APPLE__)
    // B7 (#36): macOS fell into the #else below (XDG-style ~/.local/share) —
    // the platform convention is ~/Library/Application Support. Sandboxed
    // (Mac App Store) builds relocate this automatically; the non-sandboxed
    // path here matches Apple's File System Programming Guide.
    if (auto home = absoluteEnvironmentPath("HOME"); !home.empty()) {
        return home / "Library" / "Application Support" / kApplicationDirectory;
    }
#else
    if (auto home = absoluteEnvironmentPath("HOME"); !home.empty()) {
        return home / ".local" / "share" / kApplicationDirectory;
    }
#endif
    std::error_code error;
    auto fallback = std::filesystem::temp_directory_path(error);
    if (error || fallback.empty()) fallback = std::filesystem::current_path(error);
    return fallback / kApplicationDirectory;
}

} // namespace

std::filesystem::path pathFromUtf8(std::string_view value) {
    if (value.empty()) return {};
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

std::string pathToUtf8(const std::filesystem::path& value) {
    const auto utf8 = value.u8string();
    return std::string(utf8.begin(), utf8.end());
}

UserDataDirectories makeUserDataDirectories(
    const std::filesystem::path& userDataRoot) {
    const auto normalized = userDataRoot.lexically_normal();
    return {normalized / "saves", normalized / "profiles"};
}

UserDataDirectories resolveUserDataDirectories() {
    return makeUserDataDirectories(platformUserDataRoot());
}

} // namespace Rowl::Platform
