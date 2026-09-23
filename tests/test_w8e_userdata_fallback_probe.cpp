/**
 * test_w8e_userdata_fallback_probe.cpp — W8-e (3a) user-data fallback-zincir pin'i.
 *
 * Bagimsiz ikili: rowl_w8e_userdata_fallback_probe (ctest -R w8e_userdata_fallback).
 * rowl_engine_objects'a baglanir (platform sembolleri icin; D05 konvansiyonu:
 * uye-semboller paylasilan RowlEngineCore'dan ihraç edilmez). rowl_tests
 * govdesine gomulmez.
 *
 * Kapsam (ProbU olmayan kilit): platformUserDataRoot fallback zinciri
 * (engine/src/platform/user_data_directories.cpp:68-97): XDG (`:74`),
 * HOME/getpwuid (`:77/:49-65`), fallback temp/current (`:93-96`).
 * tests/test_platform_host.cpp'da fallback-zincir probu YOK (tek
 * temp_directory_path kullanimi `:103`; UserData-resolve eslesmesi yok).
 *
 * Pin (davranis-degisikligi yok):
 *   statik — uc kol da kaynakta mevcut (XDG_DATA_HOME, getpwuid,
 *     temp_directory_path + current_path) ve son durak
 *     `return fallback / kApplicationDirectory` (temp-kolunda fail-open
 *     `return {}` yok);
 *   runtime — resolveUserDataDirectories() bos-donmez, mutlak-yoldur,
 *     saves/profiles ayrik + "saves"/"profiles" adli, kok "rowl-engine"
 *     adli; XDG_DATA_HOME canli-onceligi setenv ile pin'lenir (HOME/
 *     getpwuid dali ortamin gercek kullanicisina bagli oldugundan temp'e
 *     inis runtime'da zorlanamaz — o kol statik pinlidir).
 *
 * Oz-denetim: bellek-ici tamper fixture'lar (temp kolu kirilmis / fail-open
 * `return {}`'li kaynak) statik denetimi dusurmelidir; dusuremezse probun
 * kendisi kiriktir (exit 1). RED-kaniti repo'ya dokunmadan:
 * ROWL_W8E_USERDATA_SRC tamper'li kopyaya isaret edince exit 1.
 *
 * KIRMIZI-YESIL SOZLESMESI: zincir tam + resolve saglikli exit 0, degilse
 * exit 1. Kirmizida commit YOK.
 * Kapilar: N D (native/platform; ABI degisikligi yok).
 */
#include "rowl_test_harness.hpp"

#include <string>
#include <vector>

namespace {

void w8eUserdataFail(const std::string& message) {
    rowlLockFail("w8e-userdata-fallback-probe", message);
}

void w8eUserdataRequire(bool condition, const std::string& message) {
    if (!condition) w8eUserdataFail(message);
}

std::string readSourceFile() {
    if (const char* override = std::getenv("ROWL_W8E_USERDATA_SRC");
        override != nullptr && override[0] != '\0') {
        std::ifstream in(override);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string text = ss.str();
            if (!text.empty()) return text;
        }
        w8eUserdataFail(std::string("ROWL_W8E_USERDATA_SRC okunamadi: ") + override);
    }
    const char* candidates[] = {
        "engine/src/platform/user_data_directories.cpp",
#ifdef ROWL_W8E_SOURCE_DIR
        ROWL_W8E_SOURCE_DIR "/engine/src/platform/user_data_directories.cpp",
#endif
        nullptr,
    };
    for (const char** p = candidates; *p != nullptr; ++p) {
        std::ifstream in(*p);
        if (!in) continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        if (!text.empty()) return text;
    }
    w8eUserdataFail("user_data_directories.cpp okunamadi (aday yollar tukendi)");
    return {};
}

// Statik zincir denetimi; ihlal listesi doner (bos = temiz).
std::vector<std::string> checkFallbackChain(const std::string& text) {
    std::vector<std::string> failures;
    if (text.find("platformUserDataRoot") == std::string::npos) {
        failures.push_back("platformUserDataRoot cozumu kaynakta yok");
    }
    if (text.find("XDG_DATA_HOME") == std::string::npos) {
        failures.push_back("XDG kolu yok (XDG_DATA_HOME)");
    }
    if (text.find("getpwuid") == std::string::npos) {
        failures.push_back("HOME/getpwuid kolu yok");
    }
    const auto tempPos = text.find("temp_directory_path");
    if (tempPos == std::string::npos) {
        failures.push_back("temp fallback kolu yok (temp_directory_path)");
    }
    if (text.find("current_path") == std::string::npos) {
        failures.push_back("son-durak kolu yok (current_path)");
    }
    if (text.find("return fallback / kApplicationDirectory") == std::string::npos) {
        failures.push_back("fallback donusu yok (return fallback / kApplicationDirectory)");
    }
    // Fail-open yasagi: platformUserDataRoot govdesinde temp-kolundan sonra
    // ciplak `return {}` olmamalidir (pencere govde-kapanisina kadardir;
    // sonraki yardimcilarin bos-donusleri sayilmaz).
    if (tempPos != std::string::npos) {
        const auto closePos = text.find("\n}\n", tempPos);
        const std::string tail =
            (closePos == std::string::npos)
                ? text.substr(tempPos)
                : text.substr(tempPos, closePos - tempPos);
        if (tail.find("return {};") != std::string::npos) {
            failures.push_back("fail-open: temp-kolunda `return {}` var");
        }
    }
    return failures;
}

std::string tamperReplace(std::string text, const std::string& from,
                          const std::string& to) {
    const auto pos = text.find(from);
    if (pos != std::string::npos) text.replace(pos, from.size(), to);
    return text;
}

void checkLayout(const Rowl::Platform::UserDataDirectories& dirs, const char* label) {
    w8eUserdataRequire(!dirs.saves.empty() && !dirs.profiles.empty(),
                       std::string(label) + ": saves/profiles bos (fail-open)");
    w8eUserdataRequire(dirs.saves.is_absolute() && dirs.profiles.is_absolute(),
                       std::string(label) + ": yollar mutlak degil");
    w8eUserdataRequire(dirs.saves != dirs.profiles,
                       std::string(label) + ": saves == profiles");
    w8eUserdataRequire(dirs.saves.filename() == "saves",
                       std::string(label) + ": saves adi bozuk");
    w8eUserdataRequire(dirs.profiles.filename() == "profiles",
                       std::string(label) + ": profiles adi bozuk");
    w8eUserdataRequire(dirs.saves.parent_path().filename() == "rowl-engine",
                       std::string(label) + ": kok rowl-engine degil");
}

}  // namespace

int main() {
    TEST_SECTION("W8-e (3a): user-data fallback-zincir pin'i");

    const std::string source = readSourceFile();

    // Oz-denetim: tamper-fixture'lar statik denetimi dusurmeli (RED-kilit).
    {
        const auto brokenTemp =
            tamperReplace(source, "temp_directory_path", "temp_directory_patcX");
        const auto brokenFailOpen = tamperReplace(
            source, "return fallback / kApplicationDirectory", "return {};");
        const bool realClean = checkFallbackChain(source).empty();
        const bool tempCaught = !checkFallbackChain(brokenTemp).empty();
        const bool failOpenCaught = !checkFallbackChain(brokenFailOpen).empty();
        std::cout << "  [" << (realClean ? "ok" : "KIRIK") << "] oz-denetim gercek-kaynak-temiz"
                  << std::endl;
        std::cout << "  [" << (tempCaught ? "ok" : "KIRIK") << "] oz-denetim temp-kolu-tamper-RED"
                  << std::endl;
        std::cout << "  [" << (failOpenCaught ? "ok" : "KIRIK")
                  << "] oz-denetim fail-open-tamper-RED" << std::endl;
        w8eUserdataRequire(realClean && tempCaught && failOpenCaught,
                           "oz-denetim dustu (statik denetim RED-kilitli degil)");
    }
    TEST_PASS("Oz-denetim: temp/fail-open tamper'lari RED-kilitli");

    // Statik pin: gercek kaynak zinciri tam olmalidir.
    {
        const auto failures = checkFallbackChain(source);
        for (const auto& f : failures) {
            std::cout << "  [KIRIK] zincir: " << f << std::endl;
        }
        w8eUserdataRequire(failures.empty(), "fallback zinciri kirik (statik pin dustu)");
    }
    TEST_PASS("Statik: XDG -> HOME/getpwuid -> temp/current zinciri + fail-open yok");

    // Runtime pin: resolve saglikli (bos-donus yok).
    checkLayout(Rowl::Platform::resolveUserDataDirectories(), "resolve");
    TEST_PASS("Runtime: resolveUserDataDirectories ayrik + mutlak + rowl-engine koklu");

    // Runtime pin: XDG_DATA_HOME canli-onceligi.
    {
        const char* oldXdg = std::getenv("XDG_DATA_HOME");
        const std::string saved = (oldXdg != nullptr) ? oldXdg : "";
        const std::string xdgRoot =
            (std::filesystem::temp_directory_path() / "rowl_w8e_xdg_pin").string();
        std::error_code ec;
        std::filesystem::create_directories(xdgRoot, ec);
        w8eUserdataRequire(!ec, "xdg fixture dizini kurulamadi");
        w8eUserdataRequire(::setenv("XDG_DATA_HOME", xdgRoot.c_str(), 1) == 0,
                           "XDG_DATA_HOME setenv basarisiz");
        const auto xdgDirs = Rowl::Platform::resolveUserDataDirectories();
        const std::filesystem::path wantRoot =
            std::filesystem::path(xdgRoot) / "rowl-engine";
        w8eUserdataRequire(xdgDirs.saves == wantRoot / "saves",
                           "XDG onceligi calismadi (saves)");
        w8eUserdataRequire(xdgDirs.profiles == wantRoot / "profiles",
                           "XDG onceligi calismadi (profiles)");
        if (saved.empty()) {
            ::unsetenv("XDG_DATA_HOME");
        } else {
            ::setenv("XDG_DATA_HOME", saved.c_str(), 1);
        }
        std::filesystem::remove_all(xdgRoot, ec);
        // XDG'siz de fail-open yok: resolve yine dolu + mutlak doner.
        checkLayout(Rowl::Platform::resolveUserDataDirectories(), "resolve-no-xdg");
    }
    TEST_PASS("Runtime: XDG canli-oncelikli, XDG'siz fail-open yok");

    std::cout << "W8E-USERDATA GREEN: fallback zinciri pinli" << std::endl;
    TEST_PASS("W8-e (3a) yesil");
    return 0;
}
