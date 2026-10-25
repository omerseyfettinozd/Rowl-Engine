// window_choice_layout.cpp — D-K3: konusmaci etiketi ve secenek dugmesi yerlesimi.
//
// Cizim yollari (GPU-MSDF, GPU-TrueType, ekran disi TrueType) ve Engine'in
// tiklama / gozlem geometrisi bu iki fonksiyonun ayni olcumunu paylasir.
#include "rowl/render/window.hpp"

#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/ui_text_fit.hpp"

#include <algorithm>

namespace Rowl::Render {

Window::SpeakerTagLayout Window::layoutSpeakerTag(const DialogueRenderData& dlg,
                                                  float scaledDialogueWidth, float scale) const {
    SpeakerTagLayout layout;
    layout.text = dlg.speaker;
    const float fontPx = dlg.speakerFontSize * scale;
    FontRenderer* const speakerFont = getSpeakerFontRenderer();  // T1: theme speaker font or the project font
    if (!speakerFont || !speakerFont->isLoaded()) {
        // Yazi tipi yok: debug-bitmap yolu 10 sanal piksel/karakter ile olcer.
        layout.tagWidth = speakerTagWidth(
            static_cast<float>(dlg.speaker.length()) * 10.0f * scale, scaledDialogueWidth, scale);
        return layout;
    }
    // Tek satir olcumu (sarma yok): kararı kutu verir. measureTextWidth sekilli
    // yolda cizilen sekille ayni onbellek girdisidir (textScale dahil).
    const auto measure = [&](const std::string& text) {
        return speakerFont->measureTextWidth(text, fontPx);
    };
    const float naturalWidth = measure(dlg.speaker);
    layout.tagWidth = speakerTagWidth(naturalWidth, scaledDialogueWidth, scale);
    const float inner = std::max(0.0f, layout.tagWidth - 32.0f * scale);
    // Kisaltma yalniz kutu siniri gercekten asildiginda: tagWidth = olcum + 32*olcek
    // ile inner = tagWidth - 32*olcek ayni sayiya float yuvarlamasiyla donmez; yarim
    // piksellik pay, sigan adin sondan "…" yemesini onler.
    if (naturalWidth > inner + 0.5f) layout.text = ellipsizeToWidth(dlg.speaker, inner, measure);
    layout.shaped = speakerFont->shapeTextShared(layout.text, fontPx, 0.0f);
    return layout;
}

void Window::fitChoiceButtons(std::vector<ChoiceButtonRenderData>& choices) const {
    if (choices.empty() || !m_fontRenderer || !m_fontRenderer->isLoaded()) return;
    const uint32_t width = m_width < 10 ? 1920 : m_width;
    const uint32_t height = m_height < 10 ? 1080 : m_height;
    const ViewportMetrics metrics = AspectGuardian::calculateViewport(width, height, 1920, 1080);
    if (!(metrics.scaleFactor > 0.0f)) return;
    const float scale = metrics.scaleFactor;

    std::vector<ChoiceBox> boxes;
    boxes.reserve(choices.size());
    bool anyNeeds = false;
    for (const auto& choice : choices) {
        ChoiceBox box;
        box.x = choice.x;
        box.y = choice.y;
        box.width = choice.width;
        box.height = choice.height;
        box.visible = choice.enabled;
        if (choice.enabled && !choice.text.empty()) {
            const FontRenderer* font = getDialogueFontRenderer();
            if (!choice.fontFamily.empty() && choice.fontFamily != "Default") {
                const auto found = m_buttonFontCache.find(choice.fontFamily);
                if (found != m_buttonFontCache.end() && found->second) font = found->second.get();
            }
            // Cizimle ayni cagri: fontPx = fontSize*olcek, sarma = w*olcek - 24*olcek.
            const auto shaped = font->shapeTextShared(
                choice.text, choice.fontSize * scale, choice.width * scale - 24.0f * scale);
            const bool multiLine = shaped->lines.size() > 1;
            // %100'de tek satir: bugunku gorunum aynen kalir (kutu dokunulmaz).
            if (multiLine || font->textScale() > 1.0001f) {
                box.neededHeight = shaped->height / scale + 10.0f;
                anyNeeds = anyNeeds || box.neededHeight > box.height;
            }
        }
        boxes.push_back(box);
    }
    if (!anyNeeds) return;
    fitChoiceBoxes(boxes);
    for (std::size_t i = 0; i < choices.size(); ++i) {
        choices[i].y = boxes[i].y;
        choices[i].height = boxes[i].height;
    }
}

}  // namespace Rowl::Render
