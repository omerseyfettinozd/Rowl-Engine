/**
 * test_w8e_video_serial_pin.cpp — W8-e (3b) VideoSerialGuard reentrancy/olumcul-kullanim pin'i.
 *
 * Bagimsiz ikili: rowl_w8e_video_serial_pin (ctest -R w8e_video_serial).
 * rowl_engine_objects'a baglanir (VideoSerialGuard sembolu icin; D05
 * konvansiyonu). rowl_tests govdesine gomulmez.
 *
 * Kapsam (ProbU olmayan kilit): VideoSerialGuard reentrancy sozlesmesi —
 * process-wide NON-recursive std::mutex + RAII guard
 * (engine/src/platform/sdl_video_serial.cpp:17-29); tutuldugu govdeler
 * (engine/src/c_api_lifecycle.cpp:271,295,324,350,452); sozlesme
 * (engine/include/rowl/platform/sdl_video_serial.hpp:18-22: sira
 * VideoSerial>handle>lease). Mevcut tests/test_aux_gate.py:180-203 yalniz
 * STATIK sira kontroludur (serial-handle sirasi + Run/Step serial-free);
 * ayni-thread ic-ice Init/Destroy (non-recursive deadlock) runtime probu
 * YOK — KASTEN YOKTUR: non-recursive mutex'te ic-ice alimi deneyen bir test
 * deadlock uretir. Bu prob kural pin'idir, deadlock uretmez.
 *
 * Pin (davranis-degisikligi yok):
 *   statik — serial TU'da `std::mutex` var + `recursive_mutex` YOK;
 *     baslikta `VideoSerial > handle > lease` sozlesmesi mevcut;
 *     5 giriste (Destroy/ReclaimHandle/Init/InitStandalone/Shutdown)
 *     yorum-strip sonrasi EXACTLY-1 VideoSerialGuard (ayni-thread ic-ice
 *     alim yok) + serial ilk handle-kilit ediniminden ONCE;
 *     Run/Step govdesinde serial YOK (Run'da isLiveHandle, Step'te
 *     claimHandleOrClassify korunur);
 *   runtime — tek guard al-birak smoke (RAII kullanilabilir; ic-ice alim
 *     test EDILMEZ — by-design deadlock olurdu).
 *
 * Oz-denetim: bellek-ici mini-kaynak fixture'lari (cift-guard'li Destroy,
 * serial'li Run, recursive_mutex'li serial TU) statik denetimi dusurmelidir;
 * dusuremezse probun kendisi kiriktir (exit 1). RED-kaniti repo'ya
 * dokunmadan: ROWL_W8E_SERIAL_SRC / ROWL_W8E_SERIAL_HPP /
 * ROWL_W8E_LIFECYCLE_SRC tamper'li kopyalara isaret edince exit 1.
 *
 * KIRMIZI-YESIL SOZLESMESI: pin tam exit 0, degilse exit 1.
 * Kirmizida commit YOK.
 * Kapilar: N D (native/platform; ABI degisikligi yok).
 */
#include "rowl_test_harness.hpp"

#include "rowl/platform/sdl_video_serial.hpp"

#include <string>
#include <vector>

namespace {

void w8eSerialFail(const std::string& message) {
    rowlLockFail("w8e-video-serial-pin", message);
}

void w8eSerialRequire(bool condition, const std::string& message) {
    if (!condition) w8eSerialFail(message);
}

std::string readSourceFile(const char* envName, const char* relPath) {
    if (const char* override = std::getenv(envName);
        override != nullptr && override[0] != '\0') {
        std::ifstream in(override);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string text = ss.str();
            if (!text.empty()) return text;
        }
        w8eSerialFail(std::string(envName) + " okunamadi: " + override);
    }
    const std::string absCandidate =
#ifdef ROWL_W8E_SOURCE_DIR
        std::string(ROWL_W8E_SOURCE_DIR) + "/" + relPath;
#else
        std::string();
#endif
    const char* paths[3] = {relPath, absCandidate.empty() ? nullptr : absCandidate.c_str(),
                            nullptr};
    for (const char** p = paths; *p != nullptr; ++p) {
        std::ifstream in(*p);
        if (!in) continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        if (!text.empty()) return text;
    }
    w8eSerialFail(std::string(relPath) + " okunamadi (aday yollar tukendi)");
    return {};
}

// Yorum-strip: gate'ler kodu yargilar, nesri degil (aux-gate emsali).
std::string stripComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool line = false, block = false;
    for (std::size_t i = 0; i < src.size();) {
        if (line) {
            if (src[i] == '\n') {
                line = false;
                out += src[i++];
            } else {
                ++i;
            }
        } else if (block) {
            if (src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/') {
                block = false;
                i += 2;
            } else {
                if (src[i] == '\n') out += '\n';
                ++i;
            }
        } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            line = true;
            i += 2;
        } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            block = true;
            i += 2;
        } else {
            out += src[i++];
        }
    }
    return out;
}

std::size_t countOccurrences(const std::string& text, const std::string& token) {
    std::size_t count = 0, pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        ++count;
        pos += token.size();
    }
    return count;
}

// `Name(` tanimindan baslayip suslu-parantez esleyerek govdeyi cikarir.
// Tanmlar sutun-0'dadir; bulunamazsa bos doner.
std::string entryBody(const std::string& code, const std::string& name) {
    const auto sig = code.find(name + "(");
    if (sig == std::string::npos) return {};
    const auto open = code.find('{', sig);
    if (open == std::string::npos) return {};
    int depth = 0;
    for (std::size_t i = open; i < code.size(); ++i) {
        if (code[i] == '{') ++depth;
        if (code[i] == '}') {
            if (--depth == 0) return code.substr(open, i - open + 1);
        }
    }
    return {};
}

const char* kSerialEntries[] = {
    "RowlEngine_Destroy",
    "RowlEngine_ReclaimHandle",
    "RowlEngine_Init",
    "RowlEngine_InitStandalone",
    "RowlEngine_Shutdown",
};

const char* kHandleLockTokens[] = {
    "g_handleMutex",
    "classifyHandle",
    "claimHandleThread",
    "claimHandleOrClassify",
    "takeLiveHandle",
    "toEngineChecked",
    "copyEngineAnyThread",
    "isLiveHandle",
    "unclaimHandleThread",
};

// Statik pin denetimi; ihlal listesi doner (bos = temiz).
std::vector<std::string> checkSerialPin(const std::string& serialCpp,
                                        const std::string& serialHpp,
                                        const std::string& lifecycleCpp) {
    std::vector<std::string> failures;
    const std::string serialCode = stripComments(serialCpp);
    const std::string lifeCode = stripComments(lifecycleCpp);

    // (i) non-recursive mutex: std::mutex var, recursive_mutex YOK.
    if (serialCode.find("std::mutex") == std::string::npos) {
        failures.push_back("serial TU'da std::mutex yok");
    }
    if (serialCode.find("recursive_mutex") != std::string::npos) {
        failures.push_back("serial TU recursive (ic-ice alim deadlock'u sessizlesir)");
    }
    // (ii) kilit-sirasi sozlesmesi baslikta. NOT: sozlesme bir belge
    // yorumudur (///); yorum-strip'li kodda aranmaz, ham metinde aranir.
    if (serialHpp.find("VideoSerial > handle > lease") == std::string::npos) {
        failures.push_back("baslikta `VideoSerial > handle > lease` sozlesmesi yok");
    }
    // (iii) 5 giris: EXACTLY-1 guard + serial handle-kilitlerden once.
    for (const char* entry : kSerialEntries) {
        const std::string body = entryBody(lifeCode, entry);
        if (body.empty()) {
            failures.push_back(std::string(entry) + " govdesi cozulemedi");
            continue;
        }
        const std::size_t guards = countOccurrences(body, "VideoSerialGuard");
        if (guards != 1) {
            failures.push_back(std::string(entry) + ": VideoSerialGuard sayisi " +
                               std::to_string(guards) + " (ic-ice alim riski; exactly-1 beklenir)");
            continue;
        }
        const auto serialPos = body.find("VideoSerialGuard");
        std::size_t firstLock = std::string::npos;
        for (const char* token : kHandleLockTokens) {
            const auto p = body.find(token);
            if (p != std::string::npos && p < firstLock) firstLock = p;
        }
        if (firstLock != std::string::npos && !(serialPos < firstLock)) {
            failures.push_back(std::string(entry) + ": handle-kilidi serial'dan once (sira ters)");
        }
    }
    // (iv) Run/Step serial-free (+ bekcileri korunur).
    {
        const std::string run = entryBody(lifeCode, "RowlEngine_Run");
        if (run.empty()) {
            failures.push_back("RowlEngine_Run govdesi cozulemedi");
        } else {
            if (run.find("VideoSerialGuard") != std::string::npos) {
                failures.push_back("RowlEngine_Run serial tutuyor (blocking-loop deadlock)");
            }
            if (run.find("isLiveHandle") == std::string::npos) {
                failures.push_back("RowlEngine_Run liveness-bekcisini kaybetmis");
            }
        }
        const std::string step = entryBody(lifeCode, "RowlEngine_Step");
        if (step.empty()) {
            failures.push_back("RowlEngine_Step govdesi cozulemedi");
        } else {
            if (step.find("VideoSerialGuard") != std::string::npos) {
                failures.push_back("RowlEngine_Step serial tutuyor (serial-free olmali)");
            }
            if (step.find("claimHandleOrClassify") == std::string::npos) {
                failures.push_back("RowlEngine_Step claim-or-reject'i kaybetmis");
            }
        }
    }
    return failures;
}

}  // namespace

int main() {
    TEST_SECTION("W8-e (3b): VideoSerialGuard reentrancy pin'i");

    // Oz-denetim: mini-fixture'lar statik denetimi dusurmeli (RED-kilit).
    {
        const std::string goodSerial =
            "namespace {\nstd::mutex& serialMutex() {\n static std::mutex mutex;\n"
            " return mutex;\n}\n}\n";
        const std::string goodHeader = "/// VideoSerial > handle > lease.\n";
        const std::string serialGuardedBody =
            " {\n    Rowl::Platform::VideoSerialGuard serial;\n"
            "    std::lock_guard<std::mutex> lock(g_handleMutex);\n}\n";
        const std::string goodLife =
            "void RowlEngine_Destroy(RowlEngineHandle handle)" + serialGuardedBody +
            "RowlEngine_ResultCode RowlEngine_ReclaimHandle(RowlEngineHandle handle)" +
            serialGuardedBody +
            "int RowlEngine_Init(RowlEngineHandle handle,\n int w, int h, int f)" +
            serialGuardedBody +
            "int RowlEngine_InitStandalone(RowlEngineHandle handle,\n int w, int h, int f)" +
            serialGuardedBody +
            "void RowlEngine_Run(RowlEngineHandle handle) {\n"
            "    if (!isLiveHandle(handle)) return;\n}\n"
            "void RowlEngine_Step(RowlEngineHandle handle, float dt) {\n"
            "    if (claimHandleOrClassify(handle)) {}\n}\n"
            "void RowlEngine_Shutdown(RowlEngineHandle handle)" + serialGuardedBody;
        const std::string nestedLife =
            "void RowlEngine_Destroy(RowlEngineHandle handle) {\n"
            "    Rowl::Platform::VideoSerialGuard serial;\n"
            "    Rowl::Platform::VideoSerialGuard nested;\n"
            "    std::lock_guard<std::mutex> lock(g_handleMutex);\n}\n"
            "RowlEngine_ResultCode RowlEngine_ReclaimHandle(RowlEngineHandle handle)" +
            serialGuardedBody +
            "int RowlEngine_Init(RowlEngineHandle handle,\n int w, int h, int f)" +
            serialGuardedBody +
            "int RowlEngine_InitStandalone(RowlEngineHandle handle,\n int w, int h, int f)" +
            serialGuardedBody +
            "void RowlEngine_Run(RowlEngineHandle handle) {\n"
            "    if (!isLiveHandle(handle)) return;\n}\n"
            "void RowlEngine_Step(RowlEngineHandle handle, float dt) {\n"
            "    if (claimHandleOrClassify(handle)) {}\n}\n"
            "void RowlEngine_Shutdown(RowlEngineHandle handle)" + serialGuardedBody;
        const std::string runSerialLife =
            "void RowlEngine_Destroy(RowlEngineHandle handle)" + serialGuardedBody +
            "RowlEngine_ResultCode RowlEngine_ReclaimHandle(RowlEngineHandle handle)" +
            serialGuardedBody +
            "int RowlEngine_Init(RowlEngineHandle handle,\n int w, int h, int f)" +
            serialGuardedBody +
            "int RowlEngine_InitStandalone(RowlEngineHandle handle,\n int w, int h, int f)" +
            serialGuardedBody +
            "void RowlEngine_Run(RowlEngineHandle handle) {\n"
            "    Rowl::Platform::VideoSerialGuard serial;\n"
            "    if (!isLiveHandle(handle)) return;\n}\n"
            "void RowlEngine_Step(RowlEngineHandle handle, float dt) {\n"
            "    if (claimHandleOrClassify(handle)) {}\n}\n"
            "void RowlEngine_Shutdown(RowlEngineHandle handle)" + serialGuardedBody;
        const std::string recursiveSerial =
            "namespace {\nstd::recursive_mutex& serialMutex() {\n"
            " static std::recursive_mutex mutex;\n return mutex;\n}\n}\n";
        const bool goodClean =
            checkSerialPin(goodSerial, goodHeader, goodLife).empty();
        const bool nestedCaught =
            !checkSerialPin(goodSerial, goodHeader, nestedLife).empty();
        const bool runSerialCaught =
            !checkSerialPin(goodSerial, goodHeader, runSerialLife).empty();
        const bool recursiveCaught =
            !checkSerialPin(recursiveSerial, goodHeader, goodLife).empty();
        std::cout << "  [" << (goodClean ? "ok" : "KIRIK") << "] oz-denetim mini-temiz-GREEN"
                  << std::endl;
        std::cout << "  [" << (nestedCaught ? "ok" : "KIRIK")
                  << "] oz-denetim cift-guard-RED" << std::endl;
        std::cout << "  [" << (runSerialCaught ? "ok" : "KIRIK") << "] oz-denetim Run-serial-RED"
                  << std::endl;
        std::cout << "  [" << (recursiveCaught ? "ok" : "KIRIK")
                  << "] oz-denetim recursive-mutex-RED" << std::endl;
        w8eSerialRequire(goodClean && nestedCaught && runSerialCaught && recursiveCaught,
                         "oz-denetim dustu (statik pin RED-kilitli degil)");
    }
    TEST_PASS("Oz-denetim: cift-guard/Run-serial/recursive tamper'lari RED-kilitli");

    // Statik pin: gercek kaynaklar.
    {
        const std::string serialCpp =
            readSourceFile("ROWL_W8E_SERIAL_SRC", "engine/src/platform/sdl_video_serial.cpp");
        const std::string serialHpp = readSourceFile(
            "ROWL_W8E_SERIAL_HPP", "engine/include/rowl/platform/sdl_video_serial.hpp");
        const std::string lifecycleCpp = readSourceFile("ROWL_W8E_LIFECYCLE_SRC",
                                                        "engine/src/c_api_lifecycle.cpp");
        const auto failures = checkSerialPin(serialCpp, serialHpp, lifecycleCpp);
        for (const auto& f : failures) {
            std::cout << "  [KIRIK] serial-pin: " << f << std::endl;
        }
        w8eSerialRequire(failures.empty(), "serial pin dustu (statik)");
    }
    TEST_PASS("Statik: non-recursive mutex + sira-sozlesmesi + exactly-1 guard + Run/Step serial-free");

    // Runtime smoke: tek guard al-birak (ic-ice alim test EDILMEZ).
    {
        Rowl::Platform::VideoSerialGuard guard;
    }
    TEST_PASS("Runtime: tek VideoSerialGuard al-birak smoke (ic-ice alim denenmedi)");

    std::cout << "W8E-SERIAL GREEN: reentrancy kural pinli" << std::endl;
    TEST_PASS("W8-e (3b) yesil");
    return 0;
}
