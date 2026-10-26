// ui_text_fit.cpp — D-K3 saf yerlesim kararlari (konusmaci etiketi, secenek dugmeleri).
#include "rowl/render/ui_text_fit.hpp"

#include "rowl/text/markup_parser.hpp"
#include "rowl/text/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Rowl::Render {

float speakerTagWidth(float textWidth, float dialogueWidth, float scale) {
    // B6 (#10): dar kutuda lo > hi olur (std::clamp UB) — ust siniri yukari cek.
    const float lo = 120.0f * scale;
    return std::clamp(textWidth + 32.0f * scale, lo, std::max(dialogueWidth * 0.8f, lo));
}

namespace {

// F2 (D-D7): does `cp` continue the previous grapheme cluster (combining mark, variation
// selector, joiner, emoji skin tone, tag)? A heuristic over the common ranges, not full UAX #29:
// enough that a name is never cut between a letter and its accent or inside an emoji sequence.
bool continuesCluster(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) ||
           (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF || cp == 0x05C1 || cp == 0x05C2 ||
           cp == 0x05C4 || cp == 0x05C5 || cp == 0x05C7 || (cp >= 0x0610 && cp <= 0x061A) ||
           (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06DC) ||
           (cp >= 0x06DF && cp <= 0x06E4) || cp == 0x06E7 || cp == 0x06E8 || (cp >= 0x06EA && cp <= 0x06ED) ||
           (cp >= 0x0900 && cp <= 0x0903) || (cp >= 0x093A && cp <= 0x094F) || (cp >= 0x0951 && cp <= 0x0957) ||
           cp == 0x0962 || cp == 0x0963 || cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E3A) ||
           (cp >= 0x0E47 && cp <= 0x0E4E) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           cp == 0x200C || cp == 0x200D || (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
           (cp >= 0xE0020 && cp <= 0xE007F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool isRegionalIndicator(char32_t cp) { return cp >= 0x1F1E6 && cp <= 0x1F1FF; }

}  // namespace

std::string ellipsizeToWidth(const std::string& text, float maxWidth,
                             const std::function<float(const std::string&)>& measure) {
    if (!(maxWidth > 0.0f) || !measure) return {};
    if (measure(text) <= maxWidth) return text;
    // Cluster sinirlari (bayt degil, kod noktasi da degil): cok baytli karakter, harf+aksan
    // ve ZWJ/emoji dizisi ortadan kesilmez. ends[k] = k. kumenin bitisi.
    std::vector<std::size_t> ends;
    char32_t previous = 0;
    bool previousWasJoiner = false;
    std::size_t regionalRun = 0;
    for (std::size_t i = 0; i < text.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        if (decoded.length == 0) break;
        const char32_t cp = static_cast<char32_t>(decoded.codepoint);
        const bool joinsPrevious = !ends.empty() &&
            (continuesCluster(cp) || previousWasJoiner ||
             (isRegionalIndicator(cp) && isRegionalIndicator(previous) && regionalRun % 2 == 1));
        regionalRun = isRegionalIndicator(cp) ? regionalRun + 1 : 0;
        i += decoded.length;
        if (joinsPrevious) ends.back() = i;  // same cluster: its end moves on
        else ends.push_back(i);
        previousWasJoiner = cp == 0x200D;
        previous = cp;
    }
    static const std::string kEllipsis = "\xE2\x80\xA6";  // …
    // En uzun on ek: sigan en buyuk cluster sayisi (sondaki bosluk "…"i kelimeden
    // ayirmasin diye kirpilir). F2 (D-O5): ikili arama - eskisi sondan baslayip her adayi
    // sekillendirirdi (120 karakterlik ad kare basina 25 ms); genislik on ek uzadikca
    // azalmaz, ikili arama ayni sonucu O(log n) olcumle verir.
    const auto candidateFor = [&](std::size_t keep) {
        std::string prefix = text.substr(0, ends[keep]);
        while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
        return prefix + kEllipsis;
    };
    long long lo = 0, hi = static_cast<long long>(ends.size()) - 1, best = -1;
    while (lo <= hi) {
        const long long mid = lo + (hi - lo) / 2;
        if (measure(candidateFor(static_cast<std::size_t>(mid))) <= maxWidth) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best >= 0 ? candidateFor(static_cast<std::size_t>(best)) : kEllipsis;
}

bool speakerHasVisibleText(const std::string& speaker) {
    const std::string visible = Rowl::Text::stripMarkup(speaker);
    for (std::size_t i = 0; i < visible.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            visible.data() + i, visible.data() + visible.size());
        if (decoded.length == 0) break;
        const char32_t cp = static_cast<char32_t>(decoded.codepoint);
        i += decoded.length;
        // ASCII / Latin-1 / Unicode spaces and format characters draw nothing.
        const bool blank = cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0 || cp == 0x1680 ||
                           (cp >= 0x2000 && cp <= 0x200F) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
                           cp == 0x205F || cp == 0x2060 || cp == 0x3000 || cp == 0xFEFF;
        if (!blank) return true;
    }
    return false;
}

namespace {

constexpr float kEps = 0.01f;
constexpr float kMaxPushGap = 24.0f;
constexpr float kMinPushGap = 4.0f;

// One placement attempt from the authored boxes: grow the boxes whose text does not fit
// (bottom edge fixed), then push the ones above upward off their lower neighbour, keeping
// `gapScale` of the authored gap (never less than kMinPushGap of it).
void placeChoiceBoxes(std::vector<ChoiceBox>& boxes, float gapScale) {
    struct Authored { float y; float height; };
    std::vector<Authored> authored;
    authored.reserve(boxes.size());
    for (const auto& box : boxes) authored.push_back({box.y, box.height});

    // 1) Metni sigmayan kutu alt kenari sabit, yukari dogru buyur.
    for (auto& box : boxes) {
        if (!box.visible || !(box.neededHeight > box.height)) continue;
        const float bottom = box.y + box.height;
        box.height = box.neededHeight;
        box.y = bottom - box.height;
    }

    // 2) Alttan uste: komsunun ustune binen kutuyu yukari it (yazarin araligini koru).
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i].visible) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return authored[a].y + authored[a].height > authored[b].y + authored[b].height;
    });
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::size_t i = order[k];
        float limit = std::numeric_limits<float>::infinity();
        for (std::size_t m = 0; m < k; ++m) {
            const std::size_t j = order[m];
            const float authoredBottom = authored[i].y + authored[i].height;
            if (authored[j].y < authoredBottom - kEps) continue;  // yazar zaten ust uste koymus
            const bool overlapsX = boxes[i].x < boxes[j].x + boxes[j].width &&
                                   boxes[j].x < boxes[i].x + boxes[i].width;
            if (!overlapsX) continue;
            // Yazarin araligi korunur ama en cok kMaxPushGap kadar: uzak bir kutu
            // (buyuk bosluk) komsusu kaydi diye gereksiz yere oynatilmaz.
            const float authoredGap = std::min(authored[j].y - authoredBottom, kMaxPushGap);
            const float gap = std::min(authoredGap, std::max(authoredGap * gapScale, kMinPushGap));
            limit = std::min(limit, boxes[j].y - gap);
        }
        if (std::isfinite(limit) && boxes[i].y + boxes[i].height > limit) {
            boxes[i].y = limit - boxes[i].height;
        }
    }
}

float topOfVisible(const std::vector<ChoiceBox>& boxes) {
    float top = std::numeric_limits<float>::infinity();
    for (const auto& box : boxes) {
        if (box.visible) top = std::min(top, box.y);
    }
    return top;
}

}  // namespace

void fitChoiceBoxes(std::vector<ChoiceBox>& boxes) {
    placeChoiceBoxes(boxes, 1.0f);
}

bool fitChoiceBoxesWithin(std::vector<ChoiceBox>& boxes, float safeTop) {
    const std::vector<ChoiceBox> original = boxes;
    for (const float gapScale : {1.0f, 0.5f, 0.25f, 0.0f}) {
        boxes = original;
        placeChoiceBoxes(boxes, gapScale);
        if (topOfVisible(boxes) >= safeTop - kEps) return true;
    }
    return false;  // `boxes` holds the tightest packing; the caller shrinks the text next
}

float choiceTextTop(float buttonTop, float buttonHeight, float fontPx, float blockHeight,
                    std::size_t lines, float textScale) {
    if (lines <= 1 && textScale <= 1.0001f) return buttonTop + (buttonHeight - fontPx) * 0.5f;
    return buttonTop + (buttonHeight - blockHeight) * 0.5f;
}

}  // namespace Rowl::Render
