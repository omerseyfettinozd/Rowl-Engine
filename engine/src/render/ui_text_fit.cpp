// ui_text_fit.cpp — D-K3 saf yerlesim kararlari (konusmaci etiketi, secenek dugmeleri).
#include "rowl/render/ui_text_fit.hpp"

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

std::string ellipsizeToWidth(const std::string& text, float maxWidth,
                             const std::function<float(const std::string&)>& measure) {
    if (!(maxWidth > 0.0f) || !measure) return {};
    if (measure(text) <= maxWidth) return text;
    // Kod noktasi sinirlari (bayt degil): cok baytli karakter ortadan kesilmez.
    std::vector<std::size_t> ends;
    for (std::size_t i = 0; i < text.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        if (decoded.length == 0) break;
        i += decoded.length;
        ends.push_back(i);
    }
    static const std::string kEllipsis = "\xE2\x80\xA6";  // …
    // En uzun on ek: sigan en buyuk kod noktasi sayisi (sondaki bosluk "…"i
    // kelimeden ayirmasin diye kirpilir).
    for (std::size_t keep = ends.size(); keep-- > 0;) {
        std::string prefix = text.substr(0, ends[keep]);
        while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
        const std::string candidate = prefix + kEllipsis;
        if (measure(candidate) <= maxWidth) return candidate;
    }
    return kEllipsis;
}

void fitChoiceBoxes(std::vector<ChoiceBox>& boxes) {
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
    constexpr float kEps = 0.01f;
    constexpr float kMaxPushGap = 24.0f;
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
            const float gap = std::min(authored[j].y - authoredBottom, kMaxPushGap);
            limit = std::min(limit, boxes[j].y - gap);
        }
        if (std::isfinite(limit) && boxes[i].y + boxes[i].height > limit) {
            boxes[i].y = limit - boxes[i].height;
        }
    }
}

float choiceTextTop(float buttonTop, float buttonHeight, float fontPx, float blockHeight,
                    std::size_t lines, float textScale) {
    if (lines <= 1 && textScale <= 1.0001f) return buttonTop + (buttonHeight - fontPx) * 0.5f;
    return buttonTop + (buttonHeight - blockHeight) * 0.5f;
}

}  // namespace Rowl::Render
