// window_choice_layout.cpp — D-K3: konusmaci etiketi ve secenek dugmesi yerlesimi.
//
// Cizim yollari (GPU-MSDF, GPU-TrueType, ekran disi TrueType) ve Engine'in
// tiklama / gozlem geometrisi bu iki fonksiyonun ayni olcumunu paylasir.
#include "rowl/render/window.hpp"

#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/ui_text_fit.hpp"
#include "rowl/text/markup_parser.hpp"

#include <algorithm>

namespace Rowl::Render {

Window::SpeakerTagLayout Window::layoutSpeakerTag(const DialogueRenderData& dlg,
                                                  float scaledDialogueWidth, float scale) const {
    SpeakerTagLayout layout;
    layout.text = dlg.speaker;
    if (!speakerHasVisibleText(dlg.speaker)) {  // F2 (D-D6): nothing to label
        layout.text.clear();
        return layout;
    }
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
    if (naturalWidth > inner + 0.5f) {
        // F2 (D-D6/D-O5): a name with markup is cut on its VISIBLE text (cutting the raw string
        // could split a tag), and the cut is memoised per frame-stable inputs.
        const std::string visible = Rowl::Text::stripMarkup(dlg.speaker);
        layout.text = speakerFont->ellipsizedToWidth(visible == dlg.speaker ? dlg.speaker : visible, fontPx, inner);
    }
    layout.shaped = speakerFont->shapeTextShared(layout.text, fontPx, 0.0f);
    return layout;
}

uint64_t Window::choiceLayoutKey() const {
    // Everything fitChoiceButtons reads besides the buttons themselves: the window size and
    // every font renderer's text scale (a theme font or a button font can differ). Cheap on purpose:
    // it runs on every layout request.
    uint64_t key = 1469598103934665603ull;
    const auto mix = [&key](uint64_t value) {
        key ^= value;
        key *= 1099511628211ull;
    };
    mix(m_width);
    mix(m_height);
    const auto mixFont = [&](const FontRenderer* font) {
        mix(reinterpret_cast<uintptr_t>(font));
        if (font) mix(static_cast<uint64_t>(font->textScale() * 1000.0f));
    };
    mixFont(m_fontRenderer.get());
    mixFont(getDialogueFontRenderer());
    mix(m_buttonFontCache.size());
    for (const auto& entry : m_buttonFontCache) mixFont(entry.second.get());
    return key;
}

void Window::fitChoiceButtons(std::vector<ChoiceButtonRenderData>& choices) const {
    if (choices.empty() || !m_fontRenderer || !m_fontRenderer->isLoaded()) return;
    const uint32_t width = m_width < 10 ? 1920 : m_width;
    const uint32_t height = m_height < 10 ? 1080 : m_height;
    const ViewportMetrics metrics = AspectGuardian::calculateViewport(width, height, 1920, 1080);
    if (!(metrics.scaleFactor > 0.0f)) return;
    const float scale = metrics.scaleFactor;

    // T2: the text inset of a skinned button (default 12 + 12) and whether locked options are shown.
    const auto& choiceTheme = uiTheme().story.choice;
    const float insetAcross = choiceTheme.skin.normal.padding
        ? (*choiceTheme.skin.normal.padding)[0] + (*choiceTheme.skin.normal.padding)[2] : 24.0f;
    const bool showDisabled = choiceTheme.showDisabled;
    const auto fontFor = [&](const ChoiceButtonRenderData& choice) {
        const FontRenderer* font = getDialogueFontRenderer();
        if (!choice.fontFamily.empty() && choice.fontFamily != "Default") {
            const auto found = m_buttonFontCache.find(choice.fontFamily);
            if (found != m_buttonFontCache.end() && found->second) font = found->second.get();
        }
        return font;
    };
    // F2 (D-O4): every option's text is shaped on each layout and draw; the shape memo (16
    // entries) was evicted before its entries were reused once a menu had 17+ options.
    for (const auto& choice : choices) fontFor(choice)->reserveShapeCache(choices.size() * 2 + 8);

    // The boxes for the text drawn at `factor` x the authored size.
    const auto buildBoxes = [&](float factor, bool& anyNeeds) {
        std::vector<ChoiceBox> boxes;
        boxes.reserve(choices.size());
        anyNeeds = false;
        for (const auto& choice : choices) {
            ChoiceBox box;
            box.x = choice.x;
            box.y = choice.y;
            box.width = choice.width;
            box.height = choice.height;
            box.visible = choice.enabled || showDisabled;
            if ((choice.enabled || showDisabled) && !choice.text.empty()) {
                const FontRenderer* font = fontFor(choice);
                // Cizimle ayni cagri: fontPx = fontSize*olcek, sarma = w*olcek - insetAcross*olcek.
                const auto shaped = font->shapeTextShared(
                    choice.text, choice.fontSize * factor * scale, choice.width * scale - insetAcross * scale);
                const bool multiLine = shaped->lines.size() > 1;
                // %100'de tek satir: bugunku gorunum aynen kalir (kutu dokunulmaz).
                if (multiLine || font->textScale() > 1.0001f) {
                    box.neededHeight = shaped->height / scale + 10.0f;
                    anyNeeds = anyNeeds || box.neededHeight > box.height;
                }
            }
            boxes.push_back(box);
        }
        return boxes;
    };

    bool anyNeeds = false;
    std::vector<ChoiceBox> boxes = buildBoxes(1.0f, anyNeeds);
    if (!anyNeeds) return;

    // F2 (D-O2): the boxes grow upward, and with a big text scale and many options the top
    // ones left the screen (invisible AND unclickable). First squeeze the authored gaps; if
    // that is not enough, shrink the text step by step, but never below the author's own size
    // (the player's text scale is what is being given back, not the layout).
    float largestScale = 1.0f;
    for (const auto& choice : choices) largestScale = std::max(largestScale, fontFor(choice)->textScale());
    constexpr int kShrinkSteps = 6;
    float usedFactor = 1.0f;
    bool fits = fitChoiceBoxesWithin(boxes, kChoiceSafeTop);
    for (int step = 1; !fits && step <= kShrinkSteps && largestScale > 1.0001f; ++step) {
        usedFactor = 1.0f + (1.0f / largestScale - 1.0f) * (static_cast<float>(step) / kShrinkSteps);
        bool needs = false;
        boxes = buildBoxes(usedFactor, needs);
        fits = fitChoiceBoxesWithin(boxes, kChoiceSafeTop);
    }
    if (!fits) {
        // Last resort (a menu too big for the screen even at the author's text size): keep every
        // box on screen, overlap is the lesser evil than an option nobody can see or click.
        for (auto& box : boxes) {
            if (box.visible && box.y < kChoiceSafeTop) box.y = kChoiceSafeTop;
        }
    }
    for (std::size_t i = 0; i < choices.size(); ++i) {
        choices[i].y = boxes[i].y;
        choices[i].height = boxes[i].height;
        if (usedFactor < 1.0f) choices[i].fontSize *= usedFactor;
    }
}

}  // namespace Rowl::Render
