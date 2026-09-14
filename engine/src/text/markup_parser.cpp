/**
 * markup_parser.cpp
 *
 * Faz 3 Dilim 2 — bagimsiz zengin metin parser uygulamasi.
 * Bkz. engine/include/rowl/text/markup_parser.hpp ve
 * docs/RICH_TEXT_MARKUP_CONTRACT.md.
 */

#include "rowl/text/markup_parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>

namespace Rowl::Text {

namespace {

constexpr uint32_t kReplacementChar = 0xFFFDu;

// ── Kucuk yardimcilar ────────────────────────────────────────────────────

bool isAsciiAlpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isAsciiAlnum(unsigned char c) {
    return isAsciiAlpha(c) || (c >= '0' && c <= '9');
}

char toLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowerAscii(std::string_view value) {
    std::string out(value);
    for (char& c : out) c = toLowerAscii(c);
    return out;
}

std::string_view trimView(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' ||
            value[begin] == '\n' || value[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' ||
            value[end - 1] == '\n' || value[end - 1] == '\r')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string_view stripOneQuotePair(std::string_view value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

/// Yerel ayardan bagimsiz ASCII float cozucu: [+-]?digits[.digits]?([eE]...)?
/// Bosluk, "nan"/"inf" yazimlari ve kuyruk copu reddedilir.
std::optional<float> parseAsciiFloat(std::string_view text) {
    text = trimView(text);
    if (text.empty() || text.size() > 64) return std::nullopt;
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') ++i;
    bool digits = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
        digits = true;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
            digits = true;
        }
    }
    if (!digits) return std::nullopt;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        bool expDigits = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
            expDigits = true;
        }
        if (!expDigits) return std::nullopt;
    }
    if (i != text.size()) return std::nullopt;
    // from_chars surec locale'ini dikkate almaz. Standard grammar leading
    // '+' kabul etmedigi icin, yukarida dogrulanmis isareti burada ayikla.
    bool positiveSign = false;
    if (!text.empty() && text.front() == '+') {
        positiveSign = true;
        text.remove_prefix(1);
    }
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        value, std::chars_format::general);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() ||
        !std::isfinite(value)) {
        return std::nullopt;
    }
    if (positiveSign) value = std::abs(value);
    if (value > 3.402823466e+38 || value < -3.402823466e+38) {
        return std::nullopt;
    }
    return static_cast<float>(value);
}

bool inRange(float value, float lo, float hi) {
    return std::isfinite(value) && value >= lo && value <= hi;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendCodePointUtf8(std::string& out, uint32_t codePoint) {
    if (codePoint < 0x80u) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (codePoint >> 6u)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (codePoint >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (codePoint >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

// ── Etiket ici mini-sozdizimi ─────────────────────────────────────────────

/// "ad" + "=" + "deger" bicimindeki tekil key=value ciftini cozer.
/// Deger tirnakli olabilir ("..." / '...'). Basarisizda nullopt.
struct Attribute {
    std::string key;    // kucuk harfe cevrilmis
    std::string value;  // kirpilmis, tirnagi ayiklanmis
};

std::optional<Attribute> parseAttribute(std::string_view token) {
    const std::size_t eq = token.find('=');
    if (eq == std::string_view::npos || eq == 0 || eq + 1 >= token.size()) {
        return std::nullopt;
    }
    std::string_view key = trimView(token.substr(0, eq));
    std::string_view value = trimView(stripOneQuotePair(trimView(token.substr(eq + 1))));
    if (key.empty() || value.empty()) return std::nullopt;
    for (char c : key) {
        if (!isAsciiAlnum(static_cast<unsigned char>(c)) && c != '_' &&
            c != '-') {
            return std::nullopt;
        }
    }
    Attribute attr;
    attr.key = lowerAscii(key);
    attr.value = std::string(value);
    return attr;
}

/// Etiket adindan sonraki ham kuyrugu tirnak duyarli bicimde jetonlara boler:
/// bosluklar ayirac, tirnak ici bosluk korunur (esitsiz tirnak kuyrugu
/// reddedilir -> nullopt).
std::optional<std::vector<std::string>> splitAttributeTokens(
    std::string_view tail) {
    std::vector<std::string> tokens;
    std::string current;
    char quote = '\0';
    bool inToken = false;
    for (char c : tail) {
        if (quote != '\0') {
            current.push_back(c);
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            current.push_back(c);
            inToken = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inToken) {
                tokens.push_back(current);
                current.clear();
                inToken = false;
            }
            continue;
        }
        current.push_back(c);
        inToken = true;
    }
    if (quote != '\0') return std::nullopt;
    if (inToken) tokens.push_back(current);
    return tokens;
}

void appendJsonEscaped(std::string& out, std::string_view text) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20u) {
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4u) & 0xFu]);
                    out.push_back(kHex[c & 0xFu]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
}

void appendJsonNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "0";
        return;
    }
    char buffer[32];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec == std::errc()) {
        out.append(buffer, result.ptr);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        out += buffer;
    }
}

// ── Cozumleyici durumu ────────────────────────────────────────────────────

struct OpenFrame {
    std::string kind;  // "b", "i", "u", "color", "size", "speed", "shake", "wave"
    std::size_t offset = 0;
    std::size_t length = 0;
};

class MarkupParser {
public:
    explicit MarkupParser(std::string_view source) : m_source(source) {}

    MarkupDocument run() {
        std::size_t i = 0;
        const std::size_t n = m_source.size();
        while (i < n) {
            const unsigned char c =
                static_cast<unsigned char>(m_source[i]);
            if (c != '<') {
                consumeTextRun(i);
                continue;
            }
            if (!consumeTag(i)) {
                // consumeTag her yolda i'yi ilerletir.
            }
        }
        flushPendingPauseToTrailing();
        reportUnclosed();
        return std::move(m_document);
    }

private:
    // ── Tani kaydi ──
    void warn(std::string message, std::size_t offset, std::size_t length) {
        if (m_document.diagnostics.size() >= kMaxStoredDiagnostics) {
            ++m_document.omittedDiagnostics;
            return;
        }
        MarkupDiagnostic diag;
        diag.message = std::move(message);
        diag.sourceOffset = static_cast<uint32_t>(offset > 0xFFFFFFFFu
                                                      ? 0xFFFFFFFFu
                                                      : offset);
        diag.sourceLength = static_cast<uint32_t>(length > 0xFFFFFFFFu
                                                      ? 0xFFFFFFFFu
                                                      : length);
        m_document.diagnostics.push_back(std::move(diag));
    }

    // ── Stil gorunumu ──
    void currentStyle(MarkupChar& ch) const {
        ch.bold = m_boldDepth > 0;
        ch.italic = m_italicDepth > 0;
        ch.underline = m_underlineDepth > 0;
        if (!m_colorStack.empty()) {
            ch.hasColor = true;
            ch.color = m_colorStack.back().color;
        }
        if (!m_sizeStack.empty()) {
            ch.hasSize = true;
            ch.size = m_sizeStack.back().size;
        }
        ch.speed = m_speedStack.empty() ? 1.0f : m_speedStack.back().value;
        if (!m_shakeStack.empty()) {
            ch.shake = true;
            ch.shakeIntensity = m_shakeStack.back().intensity;
        }
        if (!m_waveStack.empty()) {
            ch.wave = true;
            ch.waveSpeed = m_waveStack.back().speed;
            ch.waveAmplitude = m_waveStack.back().amplitude;
        }
    }

    void emitChar(std::string bytes, bool lineBreak) {
        MarkupChar ch;
        ch.text = std::move(bytes);
        ch.lineBreak = lineBreak;
        currentStyle(ch);
        ch.pauseBefore = static_cast<float>(m_pendingPause);
        m_pendingPause = 0.0;
        m_document.plainText.append(ch.text);
        m_document.chars.push_back(std::move(ch));
    }

    void flushPendingPauseToTrailing() {
        if (m_pendingPause > 0.0) {
            m_document.trailingPause += m_pendingPause;
            m_pendingPause = 0.0;
        }
    }

    // ── UTF-8 metin tuketimi ──

    /// i konumundan bir sonraki '<' karakterine (veya sona) kadar duz metni
    /// tuketir; "\r\n"/"\r" tek '\n' olur, gecersiz UTF-8 U+FFFD olur.
    void consumeTextRun(std::size_t& i) {
        const std::size_t n = m_source.size();
        while (i < n && m_source[i] != '<') {
            const unsigned char c =
                static_cast<unsigned char>(m_source[i]);
            if (c == '\\' && i + 1 < n && m_source[i + 1] == '<') {
                emitChar("<", false);
                i += 2;
                continue;
            }
            if (c == '\r') {
                if (i + 1 < n && m_source[i + 1] == '\n') i += 2;
                else ++i;
                emitChar("\n", true);
                continue;
            }
            if (c == '\n') {
                ++i;
                emitChar("\n", true);
                continue;
            }
            if (c < 0x80u) {
                emitChar(std::string(1, static_cast<char>(c)), false);
                ++i;
                continue;
            }
            consumeUtf8Sequence(i);
        }
    }

    struct Utf8Decode {
        std::string bytes;
        std::size_t length = 1;  // Tukettigi kaynak bayt sayisi.
        bool valid = true;
        const char* warnMessage = nullptr;
        std::size_t warnLength = 1;
    };

    /// Kaynak gorunumun pos konumundaki UTF-8 dizisini cozer (saf: uyari
    /// uretmez, cagiran ofsetiyle warn() cagirir).
    static Utf8Decode decodeUtf8At(std::string_view src, std::size_t pos) {
        Utf8Decode out;
        const std::size_t n = src.size();
        const unsigned char lead =
            static_cast<unsigned char>(src[pos]);
        std::size_t want = 0;
        uint32_t min = 0;
        uint32_t value = 0;
        if ((lead & 0xE0u) == 0xC0u) {
            want = 2;
            min = 0x80u;
            value = lead & 0x1Fu;
        } else if ((lead & 0xF0u) == 0xE0u) {
            want = 3;
            min = 0x800u;
            value = lead & 0x0Fu;
        } else if ((lead & 0xF8u) == 0xF0u) {
            want = 4;
            min = 0x10000u;
            value = lead & 0x07u;
        } else {
            // Basibos sureklilik bayti (0x80..0xBF) veya 0xF8+ oneki.
            out.valid = false;
            out.warnMessage = "invalid UTF-8 sequence replaced with U+FFFD";
            out.warnLength = 1;
            appendCodePointUtf8(out.bytes, kReplacementChar);
            return out;
        }
        if (pos + want > n) {
            out.valid = false;
            out.length = n - pos;
            out.warnMessage = "truncated UTF-8 sequence replaced with U+FFFD";
            out.warnLength = n - pos;
            appendCodePointUtf8(out.bytes, kReplacementChar);
            return out;
        }
        for (std::size_t k = 1; k < want; ++k) {
            const unsigned char cont =
                static_cast<unsigned char>(src[pos + k]);
            if ((cont & 0xC0u) != 0x80u) {
                out.valid = false;
                out.length = k;
                out.warnMessage =
                    "invalid UTF-8 sequence replaced with U+FFFD";
                out.warnLength = k;
                appendCodePointUtf8(out.bytes, kReplacementChar);
                return out;
            }
            value = (value << 6u) | (cont & 0x3Fu);
        }
        if (value < min || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu)) {
            out.valid = false;
            out.length = want;
            out.warnMessage = "invalid UTF-8 sequence replaced with U+FFFD";
            out.warnLength = want;
            appendCodePointUtf8(out.bytes, kReplacementChar);
            return out;
        }
        out.bytes = std::string(src.substr(pos, want));
        out.length = want;
        return out;
    }

    void consumeUtf8Sequence(std::size_t& i) {
        Utf8Decode decoded = decodeUtf8At(m_source, i);
        if (!decoded.valid && decoded.warnMessage != nullptr) {
            warn(decoded.warnMessage, i, decoded.warnLength);
        }
        emitChar(std::move(decoded.bytes), false);
        i += decoded.length;
    }

    // ── Etiket tuketimi ──

    /// '<' konumunda etiket denemesi yapar; gecerli etiket tuketilir,
    /// hatali etiket literal metin + uyari olur. Her yolda true doner ve
    /// i'yi etiketin/sonraki konumun ilerisine tasir.
    bool consumeTag(std::size_t& i) {
        const std::size_t n = m_source.size();
        // '<' sonrasi ilk karakter etiket baslangici degilse (bosluk, rakam,
        // noktalama, UTF-8 bayti...) '<' duz metindir; tani uretilmez.
        // Ornek: "a < b", "a <3". Istisna: '>' ("<>") bos etiket denemesidir
        // ve malformed uyarisi uretir.
        if (i + 1 < n) {
            const unsigned char next =
                static_cast<unsigned char>(m_source[i + 1]);
            if (next != '/' && next != '>' && !isAsciiAlpha(next)) {
                emitChar("<", false);
                ++i;
                return true;
            }
        } else {
            emitChar("<", false);
            ++i;
            return true;
        }

        // Kapanis '>' aramasi: araya '<' girerse etiket sonlandirilmamistir.
        std::size_t close = std::string_view::npos;
        for (std::size_t k = i + 1; k < n; ++k) {
            if (m_source[k] == '>') {
                close = k;
                break;
            }
            if (m_source[k] == '<') break;
        }
        if (close == std::string_view::npos) {
            warn("unterminated tag; '<' kept as literal text", i, n - i);
            emitChar("<", false);
            ++i;
            return true;
        }

        const std::size_t tagOffset = i;
        const std::size_t tagLength = close - i + 1;
        const std::string_view inner = trimView(
            std::string_view(m_source.data() + i + 1, close - i - 1));
        i = close + 1;
        if (!applyTag(inner, tagOffset, tagLength)) {
            // Hatali etiket: ham bicimiyle literal metin olarak korunur.
            emitLiteralText(
                std::string_view(m_source.data() + tagOffset, tagLength));
        }
        return true;
    }

    /// Etiket icini yorumlar; gecerliyse durum guncellenir ve true doner.
    /// Hataliysa uyari uretilir ve false doner (cagiran literal korur).
    bool applyTag(std::string_view inner, std::size_t offset,
                  std::size_t length) {
        if (inner.empty()) {
            warn("malformed empty tag <>; kept as literal text", offset,
                 length);
            return false;
        }
        if (inner.front() == '/') {
            return applyClosingTag(trimView(inner.substr(1)), offset, length);
        }

        // Ad + kuyruk ayristirmasi.
        std::size_t nameEnd = 0;
        while (nameEnd < inner.size() &&
               isAsciiAlpha(
                   static_cast<unsigned char>(inner[nameEnd]))) {
            ++nameEnd;
        }
        if (nameEnd == 0) {
            warn("malformed tag; kept as literal text", offset, length);
            return false;
        }
        const std::string name =
            lowerAscii(inner.substr(0, nameEnd));
        std::string_view tail = trimView(inner.substr(nameEnd));

        // Sondaki '/' yalnizca <br> ve <pause> icin hos gorulur
        // ("<br/>", "<pause=1.0/>"); digerlerinde ham kuyruk gecerli
        // olmali, aksi halde literal + uyari.
        const bool selfClosed =
            !tail.empty() && tail.back() == '/';
        if (selfClosed &&
            (name == "br" || name == "pause")) {
            tail = trimView(tail.substr(0, tail.size() - 1));
        }

        if (name == "b" || name == "i" || name == "u") {
            if (!tail.empty()) {
                warn("tag <" + name + "> takes no attributes; kept literal",
                     offset, length);
                return false;
            }
            pushOpen(name, offset, length);
            if (name == "b") ++m_boldDepth;
            else if (name == "i") ++m_italicDepth;
            else ++m_underlineDepth;
            return true;
        }
        if (name == "br") {
            if (!tail.empty()) {
                warn("tag <br> takes no attributes; kept literal", offset,
                     length);
                return false;
            }
            emitChar("\n", true);
            return true;
        }
        if (name == "color") {
            Rgba color{};
            if (!parseEqualsValue(tail, color, offset, length)) return false;
            m_colorStack.push_back(ColorFrame{color});
            pushOpen(name, offset, length);
            return true;
        }
        if (name == "size") {
            float size = 0.0f;
            if (!parseEqualsFloat(tail, size, offset, length, "size")) {
                return false;
            }
            if (!inRange(size, kMinFontSize, kMaxFontSize)) {
                warn("tag <size> value out of range [1, 512]; kept literal",
                     offset, length);
                return false;
            }
            m_sizeStack.push_back(SizeFrame{size});
            pushOpen(name, offset, length);
            return true;
        }
        if (name == "speed") {
            float speed = 1.0f;
            if (!parseEqualsFloat(tail, speed, offset, length, "speed")) {
                return false;
            }
            if (!(std::isfinite(speed) && speed > 0.0f &&
                  speed <= kMaxSpeedMultiplier)) {
                warn("tag <speed> value out of range (0, 100]; kept literal",
                     offset, length);
                return false;
            }
            m_speedStack.push_back(SpeedFrame{speed});
            pushOpen(name, offset, length);
            return true;
        }
        if (name == "pause") {
            float seconds = 0.0f;
            if (!parseEqualsFloat(tail, seconds, offset, length, "pause")) {
                return false;
            }
            if (!inRange(seconds, kMinPauseSeconds, kMaxPauseSeconds)) {
                warn("tag <pause> value out of range [0, 60]; kept literal",
                     offset, length);
                return false;
            }
            // Sifir genislikli olay: bir sonraki karaktere (veya sonda
            // trailingPause'a) eklenir; duz metne iz birakmaz.
            m_pendingPause += static_cast<double>(seconds);
            return true;
        }
        if (name == "shake") {
            float intensity = 0.0f;
            if (!parseEffectParams(tail, {{"intensity", &intensity}}, offset,
                                   length, "shake")) {
                return false;
            }
            if (!inRange(intensity, kMinEffectParam, kMaxEffectParam)) {
                warn("tag <shake> intensity out of range [0, 100]; kept "
                     "literal",
                     offset, length);
                return false;
            }
            m_shakeStack.push_back(ShakeFrame{intensity});
            pushOpen(name, offset, length);
            return true;
        }
        if (name == "wave") {
            float speed = 0.0f;
            float amplitude = 0.0f;
            if (!parseEffectParams(tail,
                                   {{"speed", &speed},
                                    {"amplitude", &amplitude}},
                                   offset, length, "wave")) {
                return false;
            }
            if (!inRange(speed, kMinEffectParam, kMaxEffectParam) ||
                !inRange(amplitude, kMinEffectParam, kMaxEffectParam)) {
                warn("tag <wave> params out of range [0, 100]; kept literal",
                     offset, length);
                return false;
            }
            m_waveStack.push_back(WaveFrame{speed, amplitude});
            pushOpen(name, offset, length);
            return true;
        }
        warn("unknown tag; kept as literal text", offset, length);
        return false;
    }

    bool applyClosingTag(std::string_view body, std::size_t offset,
                         std::size_t length) {
        std::size_t nameEnd = 0;
        while (nameEnd < body.size() &&
               isAsciiAlpha(static_cast<unsigned char>(body[nameEnd]))) {
            ++nameEnd;
        }
        const std::string name = lowerAscii(body.substr(0, nameEnd));
        const std::string_view rest = trimView(body.substr(nameEnd));
        const bool closable = (name == "b" || name == "i" || name == "u" ||
                               name == "color" || name == "size" ||
                               name == "speed" || name == "shake" ||
                               name == "wave");
        if (nameEnd == 0 || !closable || !rest.empty()) {
            warn("malformed closing tag; kept as literal text", offset,
                 length);
            return false;
        }
        if (!popOpen(name)) {
            warn("stray closing tag </" + name + "> with no open <" + name +
                     ">; kept as literal text",
                 offset, length);
            return false;
        }
        if (name == "b") --m_boldDepth;
        else if (name == "i") --m_italicDepth;
        else if (name == "u") --m_underlineDepth;
        else if (name == "color") m_colorStack.pop_back();
        else if (name == "size") m_sizeStack.pop_back();
        else if (name == "speed") m_speedStack.pop_back();
        else if (name == "shake") m_shakeStack.pop_back();
        else if (name == "wave") m_waveStack.pop_back();
        return true;
    }

    // ── Deger cozumleyiciler ──

    /// "<color=DEGER>" kuyrugunu cozer (= zorunlu). Tirnakli deger kabul
    /// edilir. Basarisizda uyari + false.
    bool parseEqualsValue(std::string_view tail, Rgba& out,
                          std::size_t offset, std::size_t length) {
        if (tail.empty() || tail.front() != '=') {
            warn("tag <color> needs a value (<color=#RRGGBB|#RGB|named>); "
                 "kept literal",
                 offset, length);
            return false;
        }
        std::string value(
            trimView(stripOneQuotePair(trimView(tail.substr(1)))));
        if (value.empty()) {
            warn("tag <color> has an empty value; kept literal", offset,
                 length);
            return false;
        }
        if (value.front() == '#') {
            if (!tryParseHexColor(value, out)) {
                warn("tag <color> has an invalid hex value '" + value +
                         "'; kept literal",
                     offset, length);
                return false;
            }
            return true;
        }
        if (!tryParseNamedColor(value, out)) {
            warn("tag <color> has an unknown color name '" + value +
                     "'; kept literal",
                 offset, length);
            return false;
        }
        return true;
    }

    /// "<size=N>" / "<speed=F>" / "<pause=F>" kuyrugunu cozer (= zorunlu).
    bool parseEqualsFloat(std::string_view tail, float& out,
                          std::size_t offset, std::size_t length,
                          const char* tagName) {
        if (tail.empty() || tail.front() != '=') {
            warn(std::string("tag <") + tagName +
                     "> needs a value; kept literal",
                 offset, length);
            return false;
        }
        // Tirnakli sayi kabul edilir ("<size=\"24\">").
        const std::string value(
            trimView(stripOneQuotePair(trimView(tail.substr(1)))));
        const auto parsed = parseAsciiFloat(value);
        if (!parsed) {
            warn(std::string("tag <") + tagName +
                     "> has an invalid number '" + value + "'; kept literal",
                 offset, length);
            return false;
        }
        out = *parsed;
        return true;
    }

    struct EffectSlot {
        const char* key;
        float* out;
    };

    /// "<shake intensity=F>" / "<wave speed=F amplitude=F>" kuyrugunu cozer.
    /// Anahtarlar sira bagimsizdir ama eksik/fazla/tekrar anahtar
    /// reddedilir (sessiz varsayim yok).
    bool parseEffectParams(std::string_view tail,
                           std::initializer_list<EffectSlot> slots,
                           std::size_t offset, std::size_t length,
                           const char* tagName) {
        auto tokens = splitAttributeTokens(trimView(tail));
        if (!tokens) {
            warn(std::string("tag <") + tagName +
                     "> has unbalanced quotes; kept literal",
                 offset, length);
            return false;
        }
        if (tokens->size() != slots.size()) {
            warn(std::string("tag <") + tagName +
                     "> needs exactly " + std::to_string(slots.size()) +
                     " parameter(s); kept literal",
                 offset, length);
            return false;
        }
        for (const std::string& token : *tokens) {
            const auto attr = parseAttribute(token);
            if (!attr) {
                warn(std::string("tag <") + tagName +
                         "> has a malformed parameter '" + token +
                         "'; kept literal",
                     offset, length);
                return false;
            }
            bool known = false;
            for (const EffectSlot& slot : slots) {
                if (attr->key == slot.key) {
                    const auto parsed = parseAsciiFloat(attr->value);
                    if (!parsed) {
                        warn(std::string("tag <") + tagName + "> param '" +
                                 attr->key + "' is not a number; kept literal",
                             offset, length);
                        return false;
                    }
                    *slot.out = *parsed;
                    known = true;
                    break;
                }
            }
            if (!known) {
                warn(std::string("tag <") + tagName + "> has an unexpected "
                         "parameter '" + attr->key + "'; kept literal",
                     offset, length);
                return false;
            }
        }
        // Tekrarlanan anahtar yakalama: jeton sayisi beklenenle ayni ve
        // hepsi bilinen anahtarsa, ayni anahtar iki kez gecmis olabilir
        // ("<wave speed=1 speed=2>"). Bu sessizce ilk/son degeri secmek
        // yerine literal + uyari olur.
        {
            std::vector<std::string> keys;
            for (const std::string& token : *tokens) {
                const auto attr = parseAttribute(token);
                if (attr) keys.push_back(attr->key);
            }
            std::sort(keys.begin(), keys.end());
            for (std::size_t k = 1; k < keys.size(); ++k) {
                if (keys[k] == keys[k - 1]) {
                    warn(std::string("tag <") + tagName +
                             "> repeats parameter '" + keys[k] +
                             "'; kept literal",
                         offset, length);
                    return false;
                }
            }
        }
        return true;
    }

    // ── Acilis kayit defteri (kapanmamis uyari + tur-bazli yigin) ──

    void pushOpen(const std::string& kind, std::size_t offset,
                  std::size_t length) {
        m_opens.push_back(OpenFrame{kind, offset, length});
    }

    /// Turun en icteki acilisini kapatir; yoksa false (stray).
    bool popOpen(const std::string& kind) {
        for (std::size_t k = m_opens.size(); k-- > 0;) {
            if (m_opens[k].kind == kind) {
                m_opens.erase(m_opens.begin() +
                              static_cast<std::ptrdiff_t>(k));
                return true;
            }
        }
        return false;
    }

    void reportUnclosed() {
        for (const OpenFrame& open : m_opens) {
            warn("unclosed <" + open.kind +
                     "> applies to end of input; kept styling",
                 open.offset, open.length);
        }
        m_opens.clear();
    }

    /// Hatali etiketin ham baytlarini o andaki stillerle literal metin
    /// olarak korur (etiket ici '<' dagilimi: ic delta '<' tek tek ele
    /// alinir; burada ham aralikta '<' olamaz cunku close aramasi '<'
    /// gorunce durur — yine de genel guvenlik icin bayt bayt gecilir).
    void emitLiteralText(std::string_view raw) {
        for (std::size_t k = 0; k < raw.size();) {
            const unsigned char c =
                static_cast<unsigned char>(raw[k]);
            if (c == '\r') {
                if (k + 1 < raw.size() && raw[k + 1] == '\n') k += 2;
                else ++k;
                emitChar("\n", true);
                continue;
            }
            if (c == '\n') {
                ++k;
                emitChar("\n", true);
                continue;
            }
            if (c < 0x80u) {
                emitChar(std::string(1, static_cast<char>(c)), false);
                ++k;
                continue;
            }
            // Ham aralik kaynagin alt gorunumudur; mutlak ofset buradan
            // turetilir (uyari konumlari kaynak bazlidir).
            const std::size_t absolute =
                static_cast<std::size_t>(raw.data() - m_source.data()) + k;
            Utf8Decode decoded = decodeUtf8At(raw, k);
            if (!decoded.valid && decoded.warnMessage != nullptr) {
                warn(decoded.warnMessage, absolute, decoded.warnLength);
            }
            // Stilleri dogrudan uygula: emitChar bekleyen pause'u tuketir;
            // literal baytlar da normal metin gibi o andaki stili tasir.
            MarkupChar ch;
            ch.text = std::move(decoded.bytes);
            currentStyle(ch);
            ch.pauseBefore = static_cast<float>(m_pendingPause);
            m_pendingPause = 0.0;
            m_document.plainText.append(ch.text);
            m_document.chars.push_back(std::move(ch));
            k += decoded.length;
        }
    }

    struct ColorFrame {
        Rgba color{};
    };
    struct SizeFrame {
        float size = 0.0f;
    };
    struct SpeedFrame {
        float value = 1.0f;
    };
    struct ShakeFrame {
        float intensity = 0.0f;
    };
    struct WaveFrame {
        float speed = 0.0f;
        float amplitude = 0.0f;
    };

    std::string_view m_source;
    MarkupDocument m_document;
    double m_pendingPause = 0.0;
    int m_boldDepth = 0;
    int m_italicDepth = 0;
    int m_underlineDepth = 0;
    std::vector<ColorFrame> m_colorStack;
    std::vector<SizeFrame> m_sizeStack;
    std::vector<SpeedFrame> m_speedStack;
    std::vector<ShakeFrame> m_shakeStack;
    std::vector<WaveFrame> m_waveStack;
    std::vector<OpenFrame> m_opens;
};

struct NamedColor {
    const char* name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Sozlesme tablosu (docs/RICH_TEXT_MARKUP_CONTRACT.md bolum 2.3):
// C# MarkupParser.kNamedColors ile birebir ayni olmalidir.
constexpr NamedColor kNamedColors[] = {
    {"black", 0x00, 0x00, 0x00}, {"white", 0xFF, 0xFF, 0xFF},
    {"red", 0xFF, 0x00, 0x00},   {"green", 0x00, 0xFF, 0x00},
    {"blue", 0x00, 0x00, 0xFF},  {"yellow", 0xFF, 0xFF, 0x00},
    {"cyan", 0x00, 0xFF, 0xFF},  {"aqua", 0x00, 0xFF, 0xFF},
    {"magenta", 0xFF, 0x00, 0xFF}, {"fuchsia", 0xFF, 0x00, 0xFF},
    {"gray", 0x80, 0x80, 0x80},  {"grey", 0x80, 0x80, 0x80},
    {"orange", 0xFF, 0xA5, 0x00}, {"purple", 0x80, 0x00, 0x80},
    {"pink", 0xFF, 0xC0, 0xCB},  {"brown", 0xA5, 0x2A, 0x2A},
    {"lime", 0x00, 0xFF, 0x00},  {"navy", 0x00, 0x00, 0x80},
    {"teal", 0x00, 0x80, 0x80},  {"olive", 0x80, 0x80, 0x00},
};

}  // namespace

bool tryParseNamedColor(std::string_view name, Rgba& out) noexcept {
    const std::string lowered = lowerAscii(trimView(name));
    if (lowered.empty() || lowered.size() > 32) return false;
    for (const NamedColor& entry : kNamedColors) {
        if (lowered == entry.name) {
            out.r = entry.r;
            out.g = entry.g;
            out.b = entry.b;
            out.a = 255;
            return true;
        }
    }
    return false;
}

bool tryParseHexColor(std::string_view value, Rgba& out) noexcept {
    std::string hex(trimView(value));
    if (hex.empty() || hex.front() != '#') return false;
    hex.erase(hex.begin());
    auto nibbles = [&](std::size_t at, std::size_t count,
                       uint8_t& byte) -> bool {
        if (count == 1) {
            const int hi = hexNibble(hex[at]);
            if (hi < 0) return false;
            byte = static_cast<uint8_t>((hi << 4) | hi);
            return true;
        }
        const int hi = hexNibble(hex[at]);
        const int lo = hexNibble(hex[at + 1]);
        if (hi < 0 || lo < 0) return false;
        byte = static_cast<uint8_t>((hi << 4) | lo);
        return true;
    };
    Rgba color{};
    color.a = 255;
    if (hex.size() == 3) {
        if (!nibbles(0, 1, color.r) || !nibbles(1, 1, color.g) ||
            !nibbles(2, 1, color.b)) {
            return false;
        }
    } else if (hex.size() == 6) {
        if (!nibbles(0, 2, color.r) || !nibbles(2, 2, color.g) ||
            !nibbles(4, 2, color.b)) {
            return false;
        }
    } else {
        return false;
    }
    out = color;
    return true;
}

std::string colorToHex(const Rgba& color) noexcept {
    try {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r,
                      color.g, color.b);
        return std::string(buffer);
    } catch (...) {
        return "#FFFFFF";
    }
}

MarkupDocument parseMarkup(std::string_view markup) noexcept {
    try {
        MarkupParser parser(markup);
        return parser.run();
    } catch (...) {
        // Fail-closed: cozumleyici state'i tasiyamaz; bos belge dondur.
        MarkupDocument fallback;
        fallback.plainText = std::string(markup);
        MarkupDiagnostic diag;
        diag.message = "internal parser failure; input kept as plain text";
        diag.sourceOffset = 0;
        diag.sourceLength = 0;
        fallback.diagnostics.push_back(std::move(diag));
        fallback.chars.reserve(markup.size());
        for (char c : markup) {
            MarkupChar ch;
            ch.text = std::string(1, c);
            fallback.chars.push_back(std::move(ch));
        }
        return fallback;
    }
}

std::string stripMarkup(std::string_view markup) noexcept {
    try {
        return parseMarkup(markup).plainText;
    } catch (...) {
        return std::string(markup);
    }
}

std::string markupDocumentToJson(const MarkupDocument& document) noexcept {
    try {
        std::string out;
        out.reserve(document.plainText.size() + document.chars.size() * 96u +
                    256u);
        out += "{\"plain_text\":\"";
        appendJsonEscaped(out, document.plainText);
        out += "\",\"char_count\":";
        out += std::to_string(document.chars.size());
        out += ",\"chars\":[";
        for (std::size_t k = 0; k < document.chars.size(); ++k) {
            const MarkupChar& ch = document.chars[k];
            if (k > 0) out += ',';
            out += "{\"text\":\"";
            appendJsonEscaped(out, ch.text);
            out += "\",\"line_break\":";
            out += (ch.lineBreak ? "true" : "false");
            out += ",\"bold\":";
            out += (ch.bold ? "true" : "false");
            out += ",\"italic\":";
            out += (ch.italic ? "true" : "false");
            out += ",\"underline\":";
            out += (ch.underline ? "true" : "false");
            out += ",\"color\":";
            if (ch.hasColor) {
                out += '"';
                out += colorToHex(ch.color);
                out += '"';
            } else {
                out += "null";
            }
            out += ",\"size\":";
            if (ch.hasSize) {
                appendJsonNumber(out, ch.size);
            } else {
                out += "null";
            }
            out += ",\"speed\":";
            appendJsonNumber(out, ch.speed);
            out += ",\"pause_before\":";
            appendJsonNumber(out, ch.pauseBefore);
            out += ",\"shake\":";
            out += (ch.shake ? "true" : "false");
            out += ",\"shake_intensity\":";
            if (ch.shake) {
                appendJsonNumber(out, ch.shakeIntensity);
            } else {
                out += "null";
            }
            out += ",\"wave\":";
            out += (ch.wave ? "true" : "false");
            out += ",\"wave_speed\":";
            if (ch.wave) {
                appendJsonNumber(out, ch.waveSpeed);
            } else {
                out += "null";
            }
            out += ",\"wave_amplitude\":";
            if (ch.wave) {
                appendJsonNumber(out, ch.waveAmplitude);
            } else {
                out += "null";
            }
            out += '}';
        }
        out += "],\"diagnostics\":[";
        for (std::size_t k = 0; k < document.diagnostics.size(); ++k) {
            const MarkupDiagnostic& diag = document.diagnostics[k];
            if (k > 0) out += ',';
            out += "{\"message\":\"";
            appendJsonEscaped(out, diag.message);
            out += "\",\"offset\":";
            out += std::to_string(diag.sourceOffset);
            out += ",\"length\":";
            out += std::to_string(diag.sourceLength);
            out += '}';
        }
        out += "],\"trailing_pause\":";
        appendJsonNumber(out, document.trailingPause);
        out += ",\"omitted_diagnostics\":";
        out += std::to_string(document.omittedDiagnostics);
        out += '}';
        return out;
    } catch (...) {
        return "{\"plain_text\":\"\",\"char_count\":0,\"chars\":[],"
               "\"diagnostics\":[{\"message\":\"internal serialization "
               "failure\",\"offset\":0,\"length\":0}],\"trailing_pause\":0,"
               "\"omitted_diagnostics\":0}";
    }
}

}  // namespace Rowl::Text
