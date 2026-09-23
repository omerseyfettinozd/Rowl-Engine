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
 *     temp_directory_path + current_path), HOME kolu hedefi
 *     `home / ".local" / "share" / kApplicationDirectory` ile pinli
 *     (hedef degisirse sessiz-gecis yok) ve son durak
 *     `return fallback / kApplicationDirectory` (temp-kolunda fail-open
 *     `return {}` yok);
 *   tum statik aramalar satir-sonu normalize edilmis metinde yapilir
 *     (LF/CRLF dayanikli; CRLF varyant yalanci-RED vermez);
 *   runtime — resolveUserDataDirectories() bos-donmez, mutlak-yoldur,
 *     saves/profiles ayrik + "saves"/"profiles" adli, kok "rowl-engine"
 *     adli; XDG_DATA_HOME canli-onceligi setenv ile pin'lenir (HOME/
 *     getpwuid dali ortamin gercek kullanicisina bagli oldugundan temp'e
 *     inis runtime'da zorlanamaz — o kol statik pinlidir).
 *
 * Oz-denetim: bellek-ici tamper fixture'lar (temp kolu kirilmis / fail-open
 * `return {}`'li / HOME-hedefi bozulmus kaynak) statik denetimi dusurmelidir;
 * dusuremezse probun kendisi kiriktir (exit 1). CRLF'ye cevrilmis kaynak da
 * temiz kalmalidir (yalanci-RED yok). RED-kaniti repo'ya dokunmadan:
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

// Satir-sonu normalize: prob LF/CRLF'ye dayaniklidir (CRLF varyant
// yalanci-RED vermez). `\r\n` -> `\n`, yalniz `\n` korunur, yalniz `\r` -> `\n`.
std::string normalizeLineEndings(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

// Statik zincir denetimi; ihlal listesi doner (bos = temiz).
// Tum aramalar normalize edilmis metinde yapilir (LF/CRLF dayanikli).
std::vector<std::string> checkFallbackChain(const std::string& text) {
    const std::string norm = normalizeLineEndings(text);
    std::vector<std::string> failures;
    if (norm.find("platformUserDataRoot") == std::string::npos) {
        failures.push_back("platformUserDataRoot cozumu kaynakta yok");
    }
    if (norm.find("XDG_DATA_HOME") == std::string::npos) {
        failures.push_back("XDG kolu yok (XDG_DATA_HOME)");
    }
    if (norm.find("getpwuid") == std::string::npos) {
        failures.push_back("HOME/getpwuid kolu yok");
    }
    // HOME kolu hedef pini: HOME/getpwuid dali `.local/share` altina
    // yazmalidir (home / ".local" / "share" / kApplicationDirectory).
    // Hedef degisirse (orn. dogrudan home / kApplicationDirectory)
    // sessiz-gecis YOK — fail.
    if (norm.find("\".local\"") == std::string::npos ||
        norm.find("\"share\"") == std::string::npos) {
        failures.push_back(
            "HOME kolu hedefi yok (home / \".local\" / \"share\" / kApplicationDirectory)");
    }
    const auto tempPos = norm.find("temp_directory_path");
    if (tempPos == std::string::npos) {
        failures.push_back("temp fallback kolu yok (temp_directory_path)");
    }
    if (norm.find("current_path") == std::string::npos) {
        failures.push_back("son-durak kolu yok (current_path)");
    }
    if (norm.find("return fallback / kApplicationDirectory") == std::string::npos) {
        failures.push_back("fallback donusu yok (return fallback / kApplicationDirectory)");
    }
    // Fail-open yasagi: platformUserDataRoot govdesinde temp-kolundan sonra
    // ciplak `return {}` olmamalidir (pencere govde-kapanisina kadardir;
    // sonraki yardimcilarin bos-donusleri sayilmaz).
    if (tempPos != std::string::npos) {
        const auto closePos = norm.find("\n}\n", tempPos);
        const std::string tail =
            (closePos == std::string::npos)
                ? norm.substr(tempPos)
                : norm.substr(tempPos, closePos - tempPos);
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

// Tum gecisleri degistirir (HOME-hedefi gibi birden fazla dalda gecen
// desenlerin tamper'i icin; `to`, `from`'u icermemelidir).
std::string tamperReplaceAll(std::string text, const std::string& from,
                             const std::string& to) {
    std::string::size_type pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

// Oz-denetim CRLF fixture'i: normalize edilmis kaynagi CRLF'ye cevirir.
std::string toCrlf(const std::string& text) {
    const std::string norm = normalizeLineEndings(text);
    std::string out;
    out.reserve(norm.size() + norm.size() / 8);
    for (const char c : norm) {
        if (c == '\n') {
            out += "\r\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
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
        const auto brokenHomeTarget =
            tamperReplaceAll(source, "\".local\"", "\".locaX\"");
        const auto crlfSource = toCrlf(source);
        const bool realClean = checkFallbackChain(source).empty();
        const bool crlfClean = checkFallbackChain(crlfSource).empty();
        const bool tempCaught = !checkFallbackChain(brokenTemp).empty();
        const bool failOpenCaught = !checkFallbackChain(brokenFailOpen).empty();
        const bool homeTargetCaught = !checkFallbackChain(brokenHomeTarget).empty();
        std::cout << "  [" << (realClean ? "ok" : "KIRIK") << "] oz-denetim gercek-kaynak-temiz"
                  << std::endl;
        std::cout << "  [" << (crlfClean ? "ok" : "KIRIK") << "] oz-denetim crlf-kaynak-temiz"
                  << std::endl;
        std::cout << "  [" << (tempCaught ? "ok" : "KIRIK") << "] oz-denetim temp-kolu-tamper-RED"
                  << std::endl;
        std::cout << "  [" << (failOpenCaught ? "ok" : "KIRIK")
                  << "] oz-denetim fail-open-tamper-RED" << std::endl;
        std::cout << "  [" << (homeTargetCaught ? "ok" : "KIRIK")
                  << "] oz-denetim home-hedef-tamper-RED" << std::endl;
        w8eUserdataRequire(realClean && crlfClean && tempCaught && failOpenCaught &&
                               homeTargetCaught,
                           "oz-denetim dustu (statik denetim RED-kilitli degil)");
    }
    TEST_PASS("Oz-denetim: temp/fail-open/home-hedef tamper'lari RED-kilitli, CRLF temiz");

    // Statik pin: gercek kaynak zinciri tam olmalidir.
    {
        const auto failures = checkFallbackChain(source);
        for (const auto& f : failures) {
            std::cout << "  [KIRIK] zincir: " << f << std::endl;
        }
        w8eUserdataRequire(failures.empty(), "fallback zinciri kirik (statik pin dustu)");
    }
    TEST_PASS("Statik: XDG -> HOME/getpwuid(.local/share) -> temp/current zinciri + fail-open yok");

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
