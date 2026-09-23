/**
 * test_d12_red1_retention_comment.cpp — D12 yorum-celiski RED kilidi (RED-1).
 *
 * Bagimsiz ikili: rowl_d12_red1_retention_comment (ctest -R d12_red1).
 * D01 konvansiyonu: paylasilan RowlEngineCore'a baglanir; rowl_tests
 * govdesine gomulmez ki kirmizi-yesil dongusu tum suiti kosmadan
 * saniyeler icinde kanitlansin.
 *
 * Statik prob: engine/include/rowl/c_api.h metnini okur.
 *   KIRMIZI (pre-fix): "retained until process exit" cumlesi mevcutsa
 *   exit 1 — c_api.h:29-31, c_api_internal.hpp:36-41 ile celisir
 *   (gercek: free-list recycle + generation bump + ABA defense).
 *   YESIL (post-fix): recycle + generation wording mevcutsa exit 0.
 *
 * Sertlestirme (w8-e): salt-substring yerine davranis-capali pin —
 * kelime-ayrik false-green/red'e karsi:
 *   - negatif imza: "retained until process exit" dosyanin HERHANGI bir
 *     yerinde gorulurse RED (free-list recycle + generation gercigiyle
 *     celisir).
 *   - pozitif uclu: "recycl" + "generat…" + "ABA" UCU BIRLIKTE
 *     c_api.h:29-32 satir penceresinde olmalidir; biri eksikse ya da
 *     wording pencere disindaysa RED (W5 mikro-fix'i korunur: "32-bit
 *     generation" yorumu + ABA savunma wording'i aynen aranir).
 *   - oz-denetim: gomulu fixture'lar (bellek-ici tamper) siniflandiriciyi
 *     RED-kilitler — negatifli metin RED, ABA'siz metin RED, pencere-disi
 *     wording RED, hizali snippet GREEN; biri bile saparsa prob "kirk"
 *     sayilir (exit 1). RED degeri pre-fix celiskidedir; bu dilimde dosya
 *     hizali oldugundan GREEN beklenir.
 *
 * RED-kaniti (boz/revert, repo'ya dokunmadan): ROWL_D12_HEADER_FILE
 * ortam degiskeni okunacak baslik yolunu ezer; /tmp'de uretilen
 * tamper'li kopyaya isaret edilince exit 1, gercek baslikta exit 0.
 *
 * KIRMIZI-YESIL SOZLESMESI: celiski exit 1 / hizali exit 0.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void red1Fail(const std::string& message) {
    rowlLockFail("d12-red1-retention-comment", message);
}

// 1-based satir penceresi [kWindowFirst, kWindowLast] (c_api.h:29-32).
constexpr int kWindowFirst = 29;
constexpr int kWindowLast = 32;
constexpr const char* kNegativeSignature = "retained until process exit";

enum class Red1Verdict { Green, RedNegative, RedWindow };

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    lines.push_back(current);
    return lines;
}

std::string windowText(const std::string& text) {
    const auto lines = splitLines(text);
    std::string window;
    for (int i = kWindowFirst; i <= kWindowLast; ++i) {
        if (i >= 1 && static_cast<std::size_t>(i) <= lines.size()) {
            window += lines[static_cast<std::size_t>(i) - 1];
            window += '\n';
        }
    }
    return window;
}

Red1Verdict classifyHeader(const std::string& text) {
    // Negatif imza dosyanin herhangi bir yerinde RED'dir.
    if (text.find(kNegativeSignature) != std::string::npos) {
        return Red1Verdict::RedNegative;
    }
    // Pozitif uclu yalniz :29-32 penceresinde gecerlidir.
    const std::string window = windowText(text);
    const bool hasRecycle = window.find("recycl") != std::string::npos;
    const bool hasGeneration = window.find("generat") != std::string::npos;
    const bool hasAba = window.find("ABA") != std::string::npos;
    if (!hasRecycle || !hasGeneration || !hasAba) {
        return Red1Verdict::RedWindow;
    }
    return Red1Verdict::Green;
}

const char* verdictName(Red1Verdict verdict) {
    switch (verdict) {
        case Red1Verdict::Green: return "GREEN";
        case Red1Verdict::RedNegative: return "RED-negatif";
        case Red1Verdict::RedWindow: return "RED-pencere";
    }
    return "?";
}

std::string readHeader() {
    // RED-kaniti ezer: tamper'li kopya bu yoldan okutulur.
    if (const char* override = std::getenv("ROWL_D12_HEADER_FILE");
        override != nullptr && override[0] != '\0') {
        std::ifstream in(override);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string text = ss.str();
            if (!text.empty()) return text;
        }
        red1Fail(std::string("ROWL_D12_HEADER_FILE okunamadi: ") + override);
    }
    // ctest WORKING_DIRECTORY CMAKE_SOURCE_DIR'dir; yedek olarak derleme
    // anindaki kaynak kokune gore cozulen mutlak yol denenir.
    const char* candidates[] = {
        "engine/include/rowl/c_api.h",
#ifdef ROWL_D12_SOURCE_DIR
        ROWL_D12_SOURCE_DIR "/engine/include/rowl/c_api.h",
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
    red1Fail("c_api.h okunamadi (aday yollar tukendi)");
    return {};
}

// 40 satirlik yapay baslik: wording ya :29-32 penceresinde ya da disinda.
std::string fixtureHeader(bool negative, bool aba, bool inWindow) {
    std::ostringstream ss;
    for (int i = 1; i <= 40; ++i) {
        if (inWindow ? (i == kWindowFirst) : (i == 5)) {
            ss << " *  - Destroyed slots are recycled via a free-list (memory bounded by\n";
        } else if (inWindow ? (i == kWindowFirst + 1) : (i == 6)) {
            ss << " *    peak-live); a stale handle cannot become valid again within the\n";
        } else if (inWindow ? (i == kWindowFirst + 2) : (i == 7)) {
            ss << " *    32-bit generation space because the generation bump on slot reuse\n";
        } else if (inWindow ? (i == kWindowLast) : (i == 8)) {
            ss << " *    makes it name a different token (recycle + generations"
               << (aba ? ", ABA defense" : "") << ").\n";
        } else if (i == 12 && negative) {
            ss << " *  - Handles are retained until process exit.\n";
        } else {
            ss << " *  filler line " << i << "\n";
        }
    }
    return ss.str();
}

int runSelfChecks() {
    int failures = 0;
    const struct {
        const char* name;
        Red1Verdict expected;
        std::string text;
    } cases[] = {
        {"hizali-snippet-GREEN", Red1Verdict::Green,
         fixtureHeader(false, true, true)},
        {"negatif-imza-RED", Red1Verdict::RedNegative,
         fixtureHeader(true, true, true)},
        {"ABAsiz-RED", Red1Verdict::RedWindow,
         fixtureHeader(false, false, true)},
        {"pencere-disi-RED", Red1Verdict::RedWindow,
         fixtureHeader(false, true, false)},
    };
    for (const auto& c : cases) {
        const Red1Verdict got = classifyHeader(c.text);
        const bool ok = (got == c.expected);
        std::cout << "  [" << (ok ? "ok" : "KIRIK") << "] oz-denetim " << c.name
                  << " (beklenen=" << verdictName(c.expected)
                  << ", got=" << verdictName(got) << ")" << std::endl;
        if (!ok) ++failures;
    }
    return failures;
}

}  // namespace

int main() {
    TEST_SECTION("D12 RED-1: retention yorumu celiski probu (sert pin)");

    // Oz-denetim once: siniflandirici tamper-fixture'larda RED-kilitli
    // degilse probun kendisi kiriktir.
    if (runSelfChecks() != 0) {
        red1Fail("oz-denetim dustu (siniflandirici RED-kilitli degil)");
    }
    TEST_PASS("Oz-denetim: negatif/ABA/pencere fixture'lari kilitli");

    const std::string text = readHeader();
    const Red1Verdict verdict = classifyHeader(text);

    if (verdict == Red1Verdict::RedNegative) {
        std::cout << "D12-RED1 RED: c_api.h 'retained until process exit' "
                     "cumlesi internal.hpp (free-list recycle + generation) "
                     "ile celisiyor"
                  << std::endl;
        return 1;
    }
    if (verdict == Red1Verdict::RedWindow) {
        const std::string window = windowText(text);
        std::cout << "D12-RED1 RED: :29-32 penceresinde uclu eksik (recycl="
                  << (window.find("recycl") != std::string::npos ? "var" : "yok")
                  << ", generat="
                  << (window.find("generat") != std::string::npos ? "var" : "yok")
                  << ", ABA="
                  << (window.find("ABA") != std::string::npos ? "var" : "yok")
                  << ")" << std::endl;
        return 1;
    }

    std::cout << "D12-RED1 GREEN: c_api.h :29-32 recycle+generation+ABA hizali"
              << std::endl;
    TEST_PASS("D12 RED-1 yesil");
    return 0;
}
