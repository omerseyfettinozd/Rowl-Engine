/**
 * rowl/text/markup_parser.hpp
 *
 * Faz 3 Dilim 2 — Zengin metin markup parser'i: dialogue metnindeki inline
 * etiketleri (<b>, <i>, <color>, <size>, <br/>, <speed>, <pause>, <shake>,
 * <wave>) hata toleransli bicimde cozer ve daktilo / olcum / render
 * tuketicileri icin duz metin + karakter basina zengin stil token dizisi
 * uretir.
 *
 * Ownership: bagimsiz moduldur; engine.cpp / window.cpp kazanim elde etmez.
 * Saf fonksiyondur (paylasilan durum yok, dosya I/O yok, throw yok):
 * tum girdiler icin sonlanir ve asla cökme uretmez.
 *
 * Hata toleransi (fail-closed / graceful fallback):
 *  - Bilinen acilis etiketleri kapatilmamis olsa bile gecerlidir; etkisi
 *    girdinin sonuna kadar surer ve bir uyari (diagnostic) uretilir.
 *  - Bilinmeyen, bozuk parametreli, baston (stray) kapatma ve sonlandirilmamis
 *    etiketler literal metin olarak korunur + bir uyari uretilir.
 *  - Gecersiz UTF-8 dizileri U+FFFD ile degistirilir + uyari uretilir.
 *
 * Cikti modeli dogrusal (flat) bir mantiksal token akisidir, agac (tree)
 * degildir. Dilim 3 bu skalerleri shaping/reveal cluster'larina esler; stiller
 * bagimsiz bayrak/yiginlardir, ic ice gecme sirasi onemli degildir (her
 * kapatma kendi turunun yiginini patlatir).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Rowl::Text {

/// C API ve C# sinirinda uygulanan tasiyici boyutu (byte).
/// Parser'in kendisi sinirsiz dogru calisir; bu sinir ABI/managed giris
/// kapilarinda asiri buyuk girdiyi erken reddetmek icindir.
inline constexpr std::size_t kMaxMarkupBytes = 256u * 1024u;

/// Kaydedilen uyari sayisi ust siniri; asimi sayacla raporlanir.
/// Uzun/dusmani girdilerde JSON ciktisinin sinirsiz buyumemesi icindir.
inline constexpr std::size_t kMaxStoredDiagnostics = 128u;

/// Boyut araligi (soyut birim; renderer varsayilani etiket yokken gecerli).
inline constexpr float kMinFontSize = 1.0f;
inline constexpr float kMaxFontSize = 512.0f;

/// Daktilo hiz carpani araligi: (0, 100]. 1.0 = normal hiz.
inline constexpr float kMinSpeedMultiplier = 0.0001f;
inline constexpr float kMaxSpeedMultiplier = 100.0f;

/// Bekleme suresi araligi (saniye): [0, 60].
inline constexpr float kMinPauseSeconds = 0.0f;
inline constexpr float kMaxPauseSeconds = 60.0f;

/// Efekt parametre araligi: [0, 100].
inline constexpr float kMinEffectParam = 0.0f;
inline constexpr float kMaxEffectParam = 100.0f;

struct Rgba {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    bool operator==(const Rgba&) const = default;
};

/// Duz metindeki (plain text) tek bir Unicode skalerinin zengin gorunumu.
/// Bu mantiksal parser birimi nihai daktilo gosterim birimi degildir. Dilim 3
/// UAX #29 grapheme sinirlari ile HarfBuzz shaping cluster'larini birlestirip
/// ayri reveal_index degerleri uretecek; combining mark, ZWJ emoji ve ligaturler
/// parca parca gosterilmeyecektir. Sira plainText'in skaler sirasidir;
/// line-break karakterleri ('\n') de birer girdidir ve o andaki stilleri tasir.
struct MarkupChar {
    std::string text;  // Bu Unicode skalerinin UTF-8 baytlari ("\n" dahil).
    bool lineBreak = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool hasColor = false;
    Rgba color{};
    bool hasSize = false;
    float size = 0.0f;
    float speed = 1.0f;       // Daktilo hiz carpani (o karakter icin).
    float pauseBefore = 0.0f;  // Bu karakterden once beklenen sure (sn).
    bool shake = false;
    float shakeIntensity = 0.0f;
    bool wave = false;
    float waveSpeed = 0.0f;
    float waveAmplitude = 0.0f;
};

/// Her zaman siddeti "warning" olan tani kaydi.
struct MarkupDiagnostic {
    std::string message;
    uint32_t sourceOffset = 0;  // Kaynak girdideki byte ofseti.
    uint32_t sourceLength = 0;  // Ilgili kaynak araligin byte uzunlugu.
};

struct MarkupDocument {
    std::string plainText;  // Tum gecerli etiketlerden arindirilmis UTF-8.
    std::vector<MarkupChar> chars;  // plainText kod noktalariyla birebir.
    std::vector<MarkupDiagnostic> diagnostics;  // Uyari listesi (bos olabilir).
    double trailingPause = 0.0;  // Sondan sonra gelen <pause> toplami (sn).
    /// Saklama sinirina takilip atilan uyari sayisi.
    std::size_t omittedDiagnostics = 0;
};

/// Girdiyi cozer; asla throw etmez, asla cökmez.
/// Bkz. docs/RICH_TEXT_MARKUP_CONTRACT.md (tek davranis kaynagi bu sozlesme
/// + bu basliktir; celiskide kod kazanir, belge yamanir).
MarkupDocument parseMarkup(std::string_view markup) noexcept;

/// parseMarkup(markup).plainText ile esdeger kisa yol.
std::string stripMarkup(std::string_view markup) noexcept;

/// Belgeyi C ABI JSON bicimine serilestirir (anahtar sirasi deterministik).
/// Bkz. docs/RICH_TEXT_MARKUP_CONTRACT.md bolum 5.
std::string markupDocumentToJson(const MarkupDocument& document) noexcept;

/// "#RRGGBB" biciminde kisa yol (sadece test/sozlesme gorunurlugu icin).
std::string colorToHex(const Rgba& color) noexcept;

/// Adlandirilmis renk cozumlemesi (kucuk harfe duyarsiz). Bilinmeyende false.
bool tryParseNamedColor(std::string_view name, Rgba& out) noexcept;

/// "#RGB" / "#RRGGBB" cozumlemesi (basinda '#', tirnaklar ayiklanmis olmali).
bool tryParseHexColor(std::string_view value, Rgba& out) noexcept;

}  // namespace Rowl::Text
