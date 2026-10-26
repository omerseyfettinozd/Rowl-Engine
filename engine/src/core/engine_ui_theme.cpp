// engine_ui_theme.cpp — T1: the player-interface theme (Assets/json/ui_theme.json).
//
// The parser lives in state/ui_theme.cpp; the renderer reads the installed theme
// from the Window. This file loads the document from the VFS, hands it to the
// window and applies the project-wide DIALOGUE / CHOICE defaults:
//
//   precedence   key written in the node's component  >  theme  >  built-in
//
// The built-ins ARE the theme's default values, so a project without the file
// (or a theme without the key) hydrates exactly as before. Which fields a
// component wrote is remembered per dialogue / option (authoredMask) so a theme
// reload re-applies the theme to the others without re-entering the node.
//
// The theme is not part of the frame hash: Window::setUiTheme invalidates the
// cached frame and the baked badge / toast state instead.

#include "rowl/core/engine.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"

#include <nlohmann/json.hpp>

namespace Rowl::Core {

namespace {

enum DialogueField : uint32_t {
    kDlgFontSize = 1u << 0,
    kDlgSpeakerFontSize = 1u << 1,
    kDlgTextColor = 1u << 2,
    kDlgSpeakerColor = 1u << 3,
    kDlgTextAlignment = 1u << 4,
    kDlgBoxOpacity = 1u << 5,
    kDlgBoxColor = 1u << 6,
    kDlgBorderColor = 1u << 7,
    kDlgBorderThickness = 1u << 8,
    kDlgCornerRadius = 1u << 9,
};

enum ChoiceField : uint32_t {
    kBtnFontSize = 1u << 0,
    kBtnOpacity = 1u << 1,
    kBtnBorderThickness = 1u << 2,
    kBtnCornerRadius = 1u << 3,
    kBtnTextColor = 1u << 4,
    kBtnBackgroundColor = 1u << 5,
    kBtnHoverColor = 1u << 6,
    kBtnBorderColor = 1u << 7,
    kBtnTextAlignment = 1u << 8,
};

}  // namespace

uint32_t Engine::dialogueAuthoredMask(const nlohmann::json& data) {
    struct Key { const char* name; uint32_t bit; };
    static const Key kKeys[] = {
        {"font_size", kDlgFontSize}, {"speaker_font_size", kDlgSpeakerFontSize},
        {"text_color", kDlgTextColor}, {"speaker_color", kDlgSpeakerColor},
        {"text_alignment", kDlgTextAlignment}, {"box_opacity", kDlgBoxOpacity},
        {"box_color", kDlgBoxColor}, {"border_color", kDlgBorderColor},
        {"border_thickness", kDlgBorderThickness}, {"corner_radius", kDlgCornerRadius}};
    uint32_t mask = 0;
    if (!data.is_object()) return mask;
    for (const Key& key : kKeys) {
        if (data.contains(key.name)) mask |= key.bit;
    }
    return mask;
}

uint32_t Engine::choiceAuthoredMask(const nlohmann::json& data) {
    struct Key { const char* name; uint32_t bit; };
    static const Key kKeys[] = {
        {"font_size", kBtnFontSize}, {"opacity", kBtnOpacity},
        {"border_thickness", kBtnBorderThickness}, {"corner_radius", kBtnCornerRadius},
        {"text_color", kBtnTextColor}, {"background_color", kBtnBackgroundColor},
        {"hover_color", kBtnHoverColor}, {"border_color", kBtnBorderColor},
        {"text_alignment", kBtnTextAlignment}};
    uint32_t mask = 0;
    if (!data.is_object()) return mask;
    for (const Key& key : kKeys) {
        if (data.contains(key.name)) mask |= key.bit;
    }
    return mask;
}

void Engine::applyUiThemeTo(Rowl::Render::DialogueRenderData& dlg) const {
    const Rowl::State::UiDialogueDefaults& t = m_uiTheme.story.dialogue;
    const uint32_t own = dlg.authoredMask;
    if (!(own & kDlgFontSize)) dlg.fontSize = t.fontSize;
    if (!(own & kDlgSpeakerFontSize)) dlg.speakerFontSize = t.speakerFontSize;
    if (!(own & kDlgTextColor)) dlg.textColor = Rowl::State::uiColorToHex(t.textColor);
    if (!(own & kDlgSpeakerColor)) dlg.speakerColor = Rowl::State::uiColorToHex(t.speakerColor);
    if (!(own & kDlgTextAlignment)) dlg.textAlignment = t.textAlignment;
    if (!(own & kDlgBoxOpacity)) dlg.boxOpacity = t.boxOpacity;
    if (!(own & kDlgBoxColor)) dlg.boxColor = Rowl::State::uiColorToHex(t.boxColor);
    if (!(own & kDlgBorderColor)) dlg.borderColor = Rowl::State::uiColorToHex(t.borderColor);
    if (!(own & kDlgBorderThickness)) dlg.borderThickness = t.borderThickness;
    if (!(own & kDlgCornerRadius)) dlg.cornerRadius = t.cornerRadius;
}

void Engine::applyUiThemeTo(Rowl::Render::ChoiceButtonRenderData& button) const {
    const Rowl::State::UiChoiceDefaults& t = m_uiTheme.story.choice;
    const uint32_t own = button.authoredMask;
    if (!(own & kBtnFontSize)) button.fontSize = t.fontSize;
    if (!(own & kBtnOpacity)) button.opacity = t.opacity;
    if (!(own & kBtnBorderThickness)) button.borderThickness = t.borderThickness;
    if (!(own & kBtnCornerRadius)) button.cornerRadius = t.cornerRadius;
    if (!(own & kBtnTextColor)) button.textColor = Rowl::State::uiColorToHex(t.textColor);
    if (!(own & kBtnBackgroundColor)) button.backgroundColor = Rowl::State::uiColorToHex(t.backgroundColor);
    if (!(own & kBtnHoverColor)) button.hoverColor = Rowl::State::uiColorToHex(t.hoverColor);
    if (!(own & kBtnBorderColor)) button.borderColor = Rowl::State::uiColorToHex(t.borderColor);
    if (!(own & kBtnTextAlignment)) button.textAlignment = t.textAlignment;
}

Rowl::Render::DialogueRenderData Engine::themedDialogue() const {
    Rowl::Render::DialogueRenderData dlg;
    applyUiThemeTo(dlg);
    return dlg;
}

void Engine::installUiTheme(Rowl::State::UiThemeLoad load, std::string source) {
    m_uiTheme = std::move(load.theme);
    m_uiThemeHighContrast = std::move(load.highContrast);
    m_uiThemeDiagnostics = std::move(load.diagnostics);
    m_uiThemeFileFound = load.fileFound;
    m_uiThemeUsable = load.usable;
    m_uiThemeSource = std::move(source);
    // T2b: the page layouts; one that fails the checks keeps the built-in page and says why.
    m_menuLayouts = resolveMenuLayouts(m_uiTheme);
    for (const auto& line : m_menuLayouts.diagnostics) m_uiThemeDiagnostics.push_back(line);
    m_slotPage = 0;
    m_backlogWrapCache.valid = false;  // the history list may be wrapped to a new width
    for (const auto& line : m_uiThemeDiagnostics) ROWL_LOG_WARN("[UiTheme] " + line);

    // What is on screen follows the new theme without re-entering the node:
    // every field its component did not write itself takes the new default.
    for (auto& dlg : m_activeDialogues) applyUiThemeTo(dlg);
    applyUiThemeTo(m_activeDialogueData);
    for (auto& button : m_activeChoiceButtons) applyUiThemeTo(button);

    if (m_window) m_window->setUiTheme(m_uiTheme, m_uiThemeHighContrast);
}

void Engine::loadUiTheme() {
    auto* vfs = getVfs();
    Rowl::State::UiThemeLoad load;
    std::string source;
    if (vfs != nullptr) {
        // Same probing order as extras.json: the project's json/ folder first
        // (loose project and packaged game alike).
        static const char* const kCandidates[] = {"json/ui_theme.json", "ui_theme.json",
                                                  "Assets/json/ui_theme.json", "Assets/ui_theme.json"};
        for (const char* candidate : kCandidates) {
            if (!vfs->exists(candidate)) continue;
            const std::vector<uint8_t> bytes = vfs->readBytes(candidate);
            load = Rowl::State::parseUiTheme(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            source = candidate;
            break;
        }
    }
    if (load.fileFound && load.usable) {
        ROWL_LOG_INFO("[UiTheme] loaded " + source);
    }
    installUiTheme(std::move(load), std::move(source));
}

bool Engine::setUiThemeText(std::string_view json) {
    Rowl::State::UiThemeLoad load = Rowl::State::parseUiTheme(json);
    if (!load.usable) return false;
    installUiTheme(std::move(load), "(inline)");
    return true;
}

std::string Engine::getUiThemeJson() const {
    using Json = nlohmann::ordered_json;
    Json out = Json::object();
    out["format"] = Rowl::State::kUiThemeFormat;
    out["loaded"] = m_uiThemeFileFound && m_uiThemeUsable;
    out["source"] = m_uiThemeSource;
    // T2: problems the window found with the theme's images join the parser's lines.
    Json diagnostics = m_uiThemeDiagnostics;
    if (m_window) {
        for (const auto& line : m_window->skinDiagnostics()) diagnostics.push_back("ui_theme.json: " + line);
    }
    out["diagnostics"] = std::move(diagnostics);
    out["theme"] = Json::parse(Rowl::State::uiThemeToJson(m_uiTheme));
    out["high_contrast"] = m_uiThemeHighContrast
        ? Json::parse(Rowl::State::uiThemeToJson(*m_uiThemeHighContrast)) : Json(nullptr);
    return out.dump();
}

}  // namespace Rowl::Core
