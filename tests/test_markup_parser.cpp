/**
 * test_markup_parser.cpp — Faz 3 Dilim 2 contract:
 * zengin metin markup cozumleyici (etiket sozlesmesi, hata toleransi,
 * duz-metin + karakter basina token akisi) ve eklemeli C ABI
 * (RowlEngine_ParseMarkup / RowlEngine_StripMarkup +
 * CAPABILITY_RICH_TEXT_MARKUP). engine.cpp / window.cpp buyumez; kurallar
 * rowl/text/markup_parser'dadir.
 */
#include "rowl_test_harness.hpp"
#include "rowl/text/markup_parser.hpp"

#include <nlohmann/json.hpp>

#include <clocale>

namespace {

using Rowl::Text::MarkupDocument;
using nlohmann::json;

void fail(const std::string& message) {
    std::cerr << message << std::endl;
    exit(1);
}

bool near(float value, float expected, float epsilon = 1e-4f) {
    return std::fabs(value - expected) <= epsilon;
}

/// plain_text'in kod noktasi sayisi chars.size() ile eslesmeli ve char
/// metinlerinin birlesimi plain_text vermelidir (mantiksal skaler akisi).
void checkCharPlainConsistency(const MarkupDocument& document,
                               const char* caseName) {
    if (document.chars.size() > document.plainText.size() * 4u + 1u) {
        fail(std::string("Char/plain size diverged: ") + caseName);
    }
    std::string joined;
    for (const auto& ch : document.chars) joined += ch.text;
    if (joined != document.plainText) {
        fail(std::string("Char texts do not join to plain_text: ") +
             caseName + " got '" + joined + "'");
    }
}

std::string readCallerString(
    RowlEngine_ResultCode (*getter)(const char*, char*, uint32_t, uint32_t*),
    const char* markup, const char* caseName) {
    uint32_t required = 0;
    if (getter(markup, nullptr, 0, &required) != ROWL_RESULT_OK ||
        required < 1) {
        fail(std::string("Markup size query failed: ") + caseName);
    }
    if (required > 1) {
        std::vector<char> undersized(required - 1, 'x');
        uint32_t repeated = 0;
        if (getter(markup, undersized.data(),
                   static_cast<uint32_t>(undersized.size()),
                   &repeated) != ROWL_RESULT_BUFFER_TOO_SMALL ||
            repeated != required || undersized.front() != '\0') {
            fail(std::string("Markup undersized contract failed: ") +
                 caseName);
        }
    }
    std::vector<char> buffer(required, '\0');
    uint32_t repeated = 0;
    if (getter(markup, buffer.data(), static_cast<uint32_t>(buffer.size()),
               &repeated) != ROWL_RESULT_OK ||
        repeated != required || buffer.back() != '\0') {
        fail(std::string("Markup exact-size copy failed: ") + caseName);
    }
    return std::string(buffer.data());
}

void testFormattingAndColors() {
    using Rowl::Text::parseMarkup;
    {
        const MarkupDocument doc = parseMarkup("<b>bold</b> plain");
        if (doc.plainText != "bold plain" || doc.chars.size() != 10 ||
            !doc.chars[0].bold || !doc.chars[3].bold ||
            doc.chars[5].bold || !doc.diagnostics.empty()) {
            fail("Basic <b> range was misparsed");
        }
        checkCharPlainConsistency(doc, "bold");
    }
    {
        // Ic ice gecen stiller bagimsiz yiginlardir: sira disi kapatma da
        // kendi turunu kapatir (sozlesme: dogrusal token akisi, agac yok).
        const MarkupDocument doc = parseMarkup("<b><i></b></i>");
        if (!doc.plainText.empty() || doc.diagnostics.size() != 0) {
            fail("Interleaved <b><i></b></i> should close per-kind silently");
        }
    }
    {
        const MarkupDocument doc =
            parseMarkup("<b><i><u>all</u></i></b>");
        if (doc.plainText != "all" || doc.chars.size() != 3 ||
            !doc.chars[0].bold || !doc.chars[0].italic ||
            !doc.chars[0].underline || !doc.diagnostics.empty()) {
            fail("Nested <b><i><u> was misparsed");
        }
        checkCharPlainConsistency(doc, "nested");
    }
    {
        const MarkupDocument doc = parseMarkup("<B>upper</B>");
        if (doc.plainText != "upper" || !doc.chars[0].bold) {
            fail("Tag names must be case-insensitive");
        }
    }
    {
        // Renk bicimleri: #RRGGBB, #RGB, adlandirilmis (buyuk/kucuk harf).
        const MarkupDocument doc = parseMarkup(
            "<color=#FF0000>a</color><color=#F00>b</color>"
            "<color=Red>c</color>");
        if (doc.plainText != "abc" || doc.chars.size() != 3 ||
            !doc.diagnostics.empty()) {
            fail("Color forms plain text mismatch");
        }
        for (const auto& ch : doc.chars) {
            if (!ch.hasColor || ch.color.r != 255 || ch.color.g != 0 ||
                ch.color.b != 0) {
                fail("Color forms did not all resolve to pure red");
            }
        }
    }
    {
        // Ic ice renk: en icteki kazanir; kapatma bir oncekine doner.
        const MarkupDocument doc = parseMarkup(
            "<color=blue>x<color=#00FF00>y</color>z</color>");
        if (doc.plainText != "xyz" || doc.chars.size() != 3 ||
            !doc.diagnostics.empty()) {
            fail("Nested color plain text mismatch");
        }
        if (doc.chars[0].color.b != 255 || doc.chars[1].color.g != 255 ||
            doc.chars[2].color.b != 255) {
            fail("Nested color stack did not restore the outer color");
        }
    }
    {
        const MarkupDocument doc = parseMarkup("<size=24>big</size>");
        if (doc.plainText != "big" || !doc.chars[0].hasSize ||
            !near(doc.chars[0].size, 24.0f) || !doc.diagnostics.empty()) {
            fail("<size=24> was misparsed");
        }
    }
    TEST_PASS("Formatting (<b>/<i>/<u>), colors and sizes");
}

void testStructureAndTiming() {
    using Rowl::Text::parseMarkup;
    {
        const MarkupDocument doc =
            parseMarkup("a<br/>b<br>c<br />d");
        if (doc.plainText != "a\nb\nc\nd" || doc.chars.size() != 7 ||
            !doc.chars[1].lineBreak || !doc.diagnostics.empty()) {
            fail("<br> variants were misparsed");
        }
        checkCharPlainConsistency(doc, "br");
    }
    {
        const MarkupDocument doc = parseMarkup("one\r\ntwo\rthree\nfour");
        if (doc.plainText != "one\ntwo\nthree\nfour" ||
            !doc.diagnostics.empty()) {
            fail("CRLF/CR newlines must normalize to a single LF each");
        }
        checkCharPlainConsistency(doc, "newlines");
    }
    {
        const MarkupDocument doc =
            parseMarkup("<speed=2.0>fast</speed> normal");
        if (doc.plainText != "fast normal" || doc.chars.size() != 11 ||
            !near(doc.chars[0].speed, 2.0f) ||
            !near(doc.chars[3].speed, 2.0f) ||
            !near(doc.chars[5].speed, 1.0f) || !doc.diagnostics.empty()) {
            fail("<speed> range did not apply/restore");
        }
    }
    {
        // Native sayi cozumleme surec locale'indan bagimsiz kalmalidir.
        const char* active = std::setlocale(LC_NUMERIC, nullptr);
        const std::string previous = active != nullptr ? active : "C";
        constexpr const char* kCommaLocales[] = {
            "tr_TR.UTF-8", "tr_TR.utf8", "de_DE.UTF-8", "de_DE.utf8",
            "Turkish_Turkey.1254", "German_Germany.1252"};
        bool commaLocaleAvailable = false;
        for (const char* candidate : kCommaLocales) {
            if (std::setlocale(LC_NUMERIC, candidate) != nullptr &&
                std::localeconv()->decimal_point != nullptr &&
                std::localeconv()->decimal_point[0] == ',') {
                commaLocaleAvailable = true;
                break;
            }
        }
        if (commaLocaleAvailable) {
            const MarkupDocument doc =
                parseMarkup("<speed=2.5>x</speed><pause=1.25>y");
            if (doc.plainText != "xy" || doc.chars.size() != 2 ||
                !near(doc.chars[0].speed, 2.5f) ||
                !near(doc.chars[1].pauseBefore, 1.25f) ||
                !doc.diagnostics.empty()) {
                fail("Markup numbers must ignore a comma-decimal process locale");
            }
        }
        std::setlocale(LC_NUMERIC, previous.c_str());
    }
    {
        const MarkupDocument doc = parseMarkup("a<pause=1.5>b<pause=0.5>c");
        if (doc.plainText != "abc" || doc.chars.size() != 3 ||
            !near(doc.chars[1].pauseBefore, 1.5f) ||
            !near(doc.chars[2].pauseBefore, 0.5f) ||
            !near(doc.chars[0].pauseBefore, 0.0f) ||
            doc.trailingPause != 0.0 || !doc.diagnostics.empty()) {
            fail("<pause> did not attach to the following character");
        }
    }
    {
        // Sonda bekleyen pause: duz metne iz birakmaz, trailing'e toplanir.
        const MarkupDocument doc = parseMarkup("a<pause=2.0><pause=1.0>");
        if (doc.plainText != "a" || doc.trailingPause != 3.0 ||
            !doc.diagnostics.empty()) {
            fail("Trailing pauses must accumulate into trailing_pause");
        }
    }
    TEST_PASS("Structure (<br/>, newlines) and timing (<speed>, <pause>)");
}

void testEffects() {
    using Rowl::Text::parseMarkup;
    {
        const MarkupDocument doc = parseMarkup(
            "<shake intensity=2.0>sh</shake> "
            "<wave speed=3.0 amplitude=5.0>wv</wave>");
        if (doc.plainText != "sh wv" || doc.chars.size() != 5 ||
            !doc.diagnostics.empty()) {
            fail("Effect plain text mismatch");
        }
        if (!doc.chars[0].shake || !doc.chars[1].shake ||
            !near(doc.chars[0].shakeIntensity, 2.0f) || doc.chars[2].shake ||
            doc.chars[2].wave) {
            fail("<shake> range was misparsed");
        }
        if (!doc.chars[3].wave || !doc.chars[4].wave ||
            !near(doc.chars[3].waveSpeed, 3.0f) ||
            !near(doc.chars[4].waveAmplitude, 5.0f) ||
            doc.chars[0].wave) {
            fail("<wave> range was misparsed");
        }
    }
    {
        // Parametre sirasi serbest, tirnakli degerler gecerli.
        const MarkupDocument doc = parseMarkup(
            "<wave amplitude=\"5.0\" speed='3.0'>x</wave>");
        if (doc.plainText != "x" || !doc.chars[0].wave ||
            !near(doc.chars[0].waveSpeed, 3.0f) ||
            !near(doc.chars[0].waveAmplitude, 5.0f) ||
            !doc.diagnostics.empty()) {
            fail("<wave> must accept any order and quoted values");
        }
    }
    {
        // Eksik parametre sessiz varsayima degil literal + uyariya gider.
        const MarkupDocument missing = parseMarkup("<shake>x</shake>");
        if (missing.plainText.find("<shake>") == std::string::npos ||
            missing.diagnostics.empty() || missing.chars[0].shake) {
            fail("Bare <shake> must stay literal with a diagnostic");
        }
        const MarkupDocument waveMissing =
            parseMarkup("<wave speed=3.0>x</wave>");
        if (waveMissing.plainText.find("<wave") == std::string::npos ||
            waveMissing.diagnostics.empty()) {
            fail("Partial <wave> must stay literal with a diagnostic");
        }
        checkCharPlainConsistency(missing, "shake-missing");
    }
    TEST_PASS("Dynamic effects (<shake>, <wave>)");
}

void testErrorTolerance() {
    using Rowl::Text::parseMarkup;
    {
        // Kapanmamis bilinen etiket: stil sona kadar surer + 1 uyari.
        const MarkupDocument doc = parseMarkup("<b>hello");
        if (doc.plainText != "hello" || !doc.chars[0].bold ||
            doc.diagnostics.size() != 1) {
            fail("Unclosed <b> must style to end with one diagnostic");
        }
    }
    {
        // Bilinmeyen etiket + baston kapatma: literal + uyari, crash yok.
        const MarkupDocument doc =
            parseMarkup("Watch <dragon>out</color>!");
        if (doc.plainText.find("<dragon>") == std::string::npos ||
            doc.plainText.find("</color>") == std::string::npos ||
            doc.diagnostics.size() != 2) {
            fail("Unknown/stray tags must stay literal with diagnostics");
        }
        checkCharPlainConsistency(doc, "unknown-stray");
    }
    {
        // Bozuk degerler: literal + uyari; gecerli komsu etkilenmez.
        const MarkupDocument doc = parseMarkup(
            "<color=#GGG>x</color> <color=blurple>y</color> "
            "<size=0>z</size> <speed=0>w</speed> <pause=-1>v</pause> "
            "<b foo=1>u</b> <br oops>t</br>");
        for (const char* literal :
             {"<color=#GGG>", "<color=blurple>", "<size=0>", "<speed=0>",
              "<pause=-1>", "<b foo=1>", "<br oops>", "</br>"}) {
            if (doc.plainText.find(literal) == std::string::npos) {
                fail(std::string("Broken tag was not preserved: ") + literal);
            }
        }
        // 7 bozuk acilis + onlara eslik eden 7 kapanis hatasi
        // (2x </color> + </size> + </speed> + </b> stray, </pause> + </br>
        // malformed) = 14 uyari; her biri literal korunur.
        if (doc.diagnostics.size() != 14) {
            fail("Broken opens plus their stray closes must warn " +
                 std::to_string(doc.diagnostics.size()));
        }
        checkCharPlainConsistency(doc, "broken-values");
    }
    {
        // Sonlandirilmamis '<' denemesi literal + uyari; karsilastirma
        // operatoru ("a < b") ise uyarisiz literal kalir.
        const MarkupDocument unterminated = parseMarkup("a <b hello");
        if (unterminated.plainText != "a <b hello" ||
            unterminated.diagnostics.size() != 1) {
            fail("Unterminated tag attempt must stay literal + diagnostic");
        }
        const MarkupDocument math = parseMarkup("a < b and 3 < 5");
        if (math.plainText != "a < b and 3 < 5" ||
            !math.diagnostics.empty()) {
            fail("Comparison '<' must stay silent literal text");
        }
    }
    {
        // Bos etiket ve kapatilamazlar.
        const MarkupDocument doc = parseMarkup("<> </br> </pause>");
        if (doc.plainText != "<> </br> </pause>" ||
            doc.diagnostics.size() != 3) {
            fail("Empty/unclosable tags must stay literal with diagnostics");
        }
    }
    TEST_PASS("Error tolerance (unclosed/broken tags stay literal + warn)");
}

void testUnicodeAndRobustness() {
    using Rowl::Text::parseMarkup;
    {
        // TR + CJK + emoji: duz metin birebir korunur, tani yoktur.
        const std::string input =
            "R\xC3\xB6le <b>\xC4\xB1\xC5\x9F\xC3\xA7\xC4\x9F\xC3\xBC\xC3\xB6"
            "\xC4\xB0</b> \xE6\x97\xA5\xE6\x9C\xAC\xF0\x9F\x90\x89";
        const MarkupDocument doc = parseMarkup(input);
        if (!doc.diagnostics.empty()) {
            fail("Valid Unicode markup must not warn");
        }
        checkCharPlainConsistency(doc, "unicode");
        // "Röle " (5) + "ışçğüöİ" (7, bold) + " " + 2 CJK + 1 emoji = 16.
        if (doc.chars.size() != 16 || doc.plainText.size() < 16) {
            fail("Unicode char stream must count code points, not bytes");
        }
        if (!doc.chars[5].bold || !doc.chars[11].bold ||
            doc.chars[12].bold) {
            fail("Bold range over multibyte text was misaligned");
        }
    }
    {
        const MarkupDocument doc = parseMarkup(R"(show \<b> literally)");
        if (doc.plainText != "show <b> literally" ||
            doc.chars.size() != 18 || !doc.diagnostics.empty()) {
            fail("Escaped '<' must stay literal without opening a tag");
        }
        for (const auto& ch : doc.chars) {
            if (ch.bold) fail("Escaped <b> must not enable bold styling");
        }
    }
    {
        // Gecersiz UTF-8: U+FFFD + uyari, asla crash yok.
        const std::string bad("a\xFF"
                              "b\xC3("
                              "c",
                              7);
        const MarkupDocument doc = parseMarkup(bad);
        if (doc.diagnostics.empty()) {
            fail("Invalid UTF-8 must produce a diagnostic");
        }
        if (doc.plainText.find("a") == std::string::npos ||
            doc.plainText.find("b") == std::string::npos) {
            fail("Valid bytes around broken UTF-8 must survive");
        }
        checkCharPlainConsistency(doc, "bad-utf8");
    }
    {
        // Bos girdi ve yalnizca-etiket girdisi.
        const MarkupDocument empty = parseMarkup("");
        if (!empty.plainText.empty() || !empty.chars.empty() ||
            !empty.diagnostics.empty() || empty.trailingPause != 0.0) {
            fail("Empty input must yield an empty document");
        }
        const MarkupDocument tagsOnly =
            parseMarkup("<b></b><pause=1.0>");
        if (!tagsOnly.plainText.empty() || !tagsOnly.chars.empty() ||
            tagsOnly.trailingPause != 1.0 ||
            !tagsOnly.diagnostics.empty()) {
            fail("Tags-only input must yield empty text + trailing pause");
        }
    }
    {
        // Fuzz: kotu niyetli/kirik girdilerde sonlanma + tutarlilik.
        const std::string nasty[] = {
            "<",          "<<",         "<<<>>>",    "<//>",     "</>",
            "<b",         "<b ",        "<b=",       "<b=>",     "<b/>",
            "<color=>",   "<color>",    "<color=#>", "<color=#12345>",
            "<color=#1234567>",         "<size=>",   "<size=abc>",
            "<size=24",   "<speed=>",   "<pause>",   "<pause=>",
            "<shake=>",   "<shake intensity=>",     "<wave>",
            "<wave speed=1 speed=2>",   "<unknown foo='bar\"",
            "<b><b><b>",  "</b></b>",   "<Br/>",     "<SIZE = \"24\" >",
            "<color = '#00ff00' >x</color>", "a<b",
            std::string("emoji \xF0\x9F\x98\x80 <b>bold\xFF", 19),
        };
        for (const std::string& input : nasty) {
            const MarkupDocument doc = parseMarkup(input);
            checkCharPlainConsistency(doc, input.c_str());
            if (doc.omittedDiagnostics != 0) {
                fail("Small fuzz inputs must not hit the diagnostic cap");
            }
        }
    }
    TEST_PASS("Unicode, invalid UTF-8 and hostile-input robustness");
}

void testCabiSurface() {
    TEST_SECTION("Markup C ABI (additive, handle-free, caller buffers)");

    uint64_t capabilities = 0;
    if (RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
        (capabilities & ROWL_ENGINE_CAPABILITY_RICH_TEXT_MARKUP) == 0) {
        fail("CAPABILITY_RICH_TEXT_MARKUP (256) is not advertised");
    }
    if ((capabilities & (ROWL_ENGINE_CAPABILITY_RESULT_CODES |
                         ROWL_ENGINE_CAPABILITY_CALLER_BUFFERS |
                         ROWL_ENGINE_CAPABILITY_USER_DATA_DIRECTORIES |
                         ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT |
                         ROWL_ENGINE_CAPABILITY_PLAYER_LOOP |
                         ROWL_ENGINE_CAPABILITY_SAVE_METADATA |
                         ROWL_ENGINE_CAPABILITY_PLAYER_CHOICES |
                         ROWL_ENGINE_CAPABILITY_LOCALIZATION)) == 0) {
        fail("Capability advertisement dropped a pre-existing flag");
    }
    TEST_PASS("CAPABILITY_RICH_TEXT_MARKUP advertised, older flags intact");

    // Null / arg guards: handle yok (saf yardimci), ama null giris ve null
    // outRequiredSize her zaman reddedilir; host'a istisna sizamaz.
    uint32_t required = 0;
    char tiny[4] = {0};
    if (RowlEngine_ParseMarkup(nullptr, nullptr, 0, &required) !=
            ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_StripMarkup(nullptr, nullptr, 0, &required) !=
            ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_ParseMarkup("<b>x</b>", nullptr, 0, nullptr) !=
            ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_StripMarkup("<b>x</b>", nullptr, 0, nullptr) !=
            ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_ParseMarkup("<b>x</b>", nullptr, 7, &required) !=
            ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_ParseMarkup(nullptr, tiny, sizeof(tiny), &required) !=
            ROWL_RESULT_INVALID_ARGUMENT) {
        fail("Markup C API null/argument guards failed");
    }
    TEST_PASS("Markup C API null/argument guards");

    // Asiri buyuk girdi (256 KiB siniri): parse edilmeden reddedilir.
    {
        const std::string huge(Rowl::Text::kMaxMarkupBytes + 1, 'x');
        if (RowlEngine_ParseMarkup(huge.c_str(), nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_ARGUMENT ||
            RowlEngine_StripMarkup(huge.c_str(), nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_ARGUMENT) {
            fail("Oversized markup input was not rejected");
        }
        const std::string atLimit(Rowl::Text::kMaxMarkupBytes, 'y');
        if (RowlEngine_StripMarkup(atLimit.c_str(), nullptr, 0, &required) !=
            ROWL_RESULT_OK) {
            fail("At-limit markup input must be accepted");
        }

        // Savunmaci sinir: NUL bulunmayan tam tarama penceresi buffer disina
        // cikmadan reddedilmelidir.
        const std::vector<char> unterminated(
            Rowl::Text::kMaxMarkupBytes + 1u, 'z');
        if (RowlEngine_ParseMarkup(unterminated.data(), nullptr, 0,
                                   &required) !=
            ROWL_RESULT_INVALID_ARGUMENT) {
            fail("Markup input scan was not bounded to 256 KiB + 1");
        }
    }
    TEST_PASS("Markup C API oversized-input rejection at 256 KiB");

    // Caller-buffer sozlesmesi + JSON semasi (Strip ve Parse).
    {
        const std::string stripped = readCallerString(
            RowlEngine_StripMarkup, "A <b>bold <color=red>red</color></b>!",
            "strip");
        if (stripped != "A bold red!") {
            fail(std::string("StripMarkup mismatch: ") + stripped);
        }
        const std::string broken = readCallerString(
            RowlEngine_StripMarkup, "a < b <oops>", "strip-literal");
        if (broken != "a < b <oops>") {
            fail(std::string("StripMarkup literal mismatch: ") + broken);
        }
    }
    {
        const std::string parsed = readCallerString(
            RowlEngine_ParseMarkup,
            "Hi <b>bold</b><pause=0.5>!", "parse-json");
        json document;
        try {
            document = json::parse(parsed);
        } catch (...) {
            fail("ParseMarkup output is not valid JSON");
        }
        if (!document.contains("plain_text") ||
            !document.contains("char_count") || !document.contains("chars") ||
            !document.contains("diagnostics") ||
            !document.contains("trailing_pause") ||
            !document.contains("omitted_diagnostics")) {
            fail("ParseMarkup JSON is missing a contract key");
        }
        if (document.at("plain_text").get<std::string>() != "Hi bold!" ||
            document.at("char_count").get<std::size_t>() != 8 ||
            document.at("chars").size() != 8 ||
            !document.at("diagnostics").is_array() ||
            document.at("omitted_diagnostics").get<std::size_t>() != 0) {
            fail("ParseMarkup JSON payload mismatch");
        }
        const json& bold = document.at("chars").at(3);
        if (bold.at("text").get<std::string>() != "b" ||
            !bold.at("bold").get<bool>() || bold.at("italic").get<bool>() ||
            !bold.at("color").is_null() || !bold.at("size").is_null() ||
            bold.at("speed").get<float>() != 1.0f ||
            bold.at("pause_before").get<float>() != 0.0f ||
            bold.at("shake").get<bool>() || bold.at("wave").get<bool>()) {
            fail("ParseMarkup per-char token mismatch");
        }
        // "<pause>" kendinden sonraki "!" karakterine eklenir.
        if (document.at("chars").at(7).at("pause_before").get<float>() !=
            0.5f) {
            fail("ParseMarkup pause_before did not land on '!'");
        }
    }
    TEST_PASS("Markup caller buffers, Strip text and Parse JSON schema");
}

}  // namespace

void test_markup_parser() {
    TEST_SECTION("Rich Text Markup — Parser, Token Stream, C ABI");
    testFormattingAndColors();
    testStructureAndTiming();
    testEffects();
    testErrorTolerance();
    testUnicodeAndRobustness();
    testCabiSurface();
}
