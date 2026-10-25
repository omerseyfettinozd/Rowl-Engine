#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Rowl::State {

/// T1: the player-interface theme of a game (`Assets/json/ui_theme.json`,
/// format 1): colours, border widths, text sizes and fonts of every
/// engine-drawn piece of player UI (menus, gallery, achievements, toast,
/// AUTO/SKIP badges) plus the project-wide DEFAULTS of the dialogue box and the
/// choice buttons. Every default below is the look the engine had before the
/// theme existed: a project without the file (or a file without a key) renders
/// pixel for pixel as before. See docs/PLAYER_UI_THEME.md.

inline constexpr int kUiThemeFormat = 1;
/// Largest accepted ui_theme.json (bytes); a bigger file is ignored as a whole.
inline constexpr std::size_t kUiThemeMaxFileBytes = 256 * 1024;
/// Kept diagnostics.
inline constexpr std::size_t kUiThemeMaxDiagnostics = 32;

struct UiColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    bool operator==(const UiColor&) const = default;
};

/// "#RRGGBB" (alpha 255) or "#RRGGBBAA", upper-case hex.
std::string uiColorToHex(const UiColor& color);

/// Menu family: title screen, pause menu, slot pages, Preferences, History
/// (backlog), Ekstralar pages and the confirmation pages.
struct UiMenuTheme {
    UiColor dim{0, 0, 0, 160};               ///< darkening over the frozen story frame
    UiColor panelFill{15, 15, 26, 235};
    UiColor panelBorder{56, 189, 248, 255};
    float panelBorderWidth = 1.0f;           ///< physical px, 0..8 (0 = no border)
    UiColor title{56, 189, 248, 255};        ///< page title
    UiColor text{241, 245, 249, 255};        ///< row label
    UiColor dimText{148, 163, 184, 255};     ///< row value / detail / counter
    UiColor disabledText{148, 163, 184, 255};
    UiColor selectedFill{14, 116, 144, 255};
    UiColor selectedBar{56, 189, 248, 255};
    float selectedBarWidth = 6.0f;           ///< virtual px, 0..64
    UiColor selectedText{56, 189, 248, 255}; ///< label of the selected row
    UiColor hint{148, 163, 184, 255};
    UiColor thumbBorder{56, 189, 248, 255};  ///< save-slot thumbnail frame
    UiColor historySpeaker{56, 189, 248, 255};
    float titlePx = 40.0f;                   ///< text sizes, virtual px, 8..120
    float rowPx = 24.0f;
    float valuePx = 24.0f;
    float detailPx = 16.0f;
    float hintPx = 19.0f;
    float historySpeakerPx = 20.0f;
};

/// Ekstralar: gallery grid, achievement plates, full-screen viewer.
struct UiExtrasTheme {
    UiColor cellFill{8, 10, 18, 255};            ///< unlocked gallery cell background
    UiColor lockedFill{22, 25, 40, 255};         ///< locked gallery cell box
    UiColor cellBorderSelected{56, 189, 248, 255};
    UiColor cellBorder{71, 85, 105, 255};
    UiColor lockedBorder{46, 56, 78, 255};
    UiColor plate{22, 25, 42, 255};              ///< achievement plate
    UiColor plateSelected{14, 116, 144, 255};    ///< selected plate / selected gallery cell
    UiColor plateBar{56, 189, 248, 255};
    UiColor titleSelected{56, 189, 248, 255};    ///< achievement title when selected
    UiColor text{241, 245, 249, 255};            ///< unlocked titles, captions
    UiColor dimText{148, 163, 184, 255};         ///< locked texts, dates, "?" mark
    UiColor description{203, 213, 225, 255};     ///< unlocked achievement description
    UiColor viewerDim{0, 0, 0, 240};             ///< full-screen viewer backdrop
    UiColor viewerFrame{56, 189, 248, 255};
    UiColor viewerTitle{241, 245, 249, 255};
    float markPx = 44.0f;                        ///< "?" of a locked cell
    float captionPx = 16.0f;                     ///< gallery caption
    float viewerTitlePx = 30.0f;
};

/// Achievement toast.
struct UiToastTheme {
    UiColor fill{15, 15, 26, 215};
    UiColor text{241, 245, 249, 255};
    UiColor accent{56, 189, 248, 255};           ///< the "Basarim:" head
    UiColor rule{56, 189, 248, 190};             ///< rule along the bottom edge
    float rulePx = 2.0f;                         ///< 0..16 virtual px (0 = none)
    float px = 26.0f;                            ///< text size
    float height = 56.0f;
    float marginTop = 28.0f;
    float padX = 30.0f;
    float maxTextWidth = 1300.0f;
};

/// AUTO / SKIP status capsules.
struct UiBadgeTheme {
    UiColor autoFill{15, 15, 26, 200};
    UiColor autoText{56, 189, 248, 255};
    UiColor skipFill{14, 116, 144, 210};
    UiColor skipText{241, 245, 249, 255};
    float px = 24.0f;
    float width = 116.0f;
    float height = 44.0f;
    float gap = 12.0f;
    float marginRight = 40.0f;
    float marginTop = 28.0f;
};

/// Scene-level colours the renderer draws when an asset is missing and around
/// the 16:9 canvas.
struct UiSceneTheme {
    UiColor letterbox{11, 15, 25, 255};
    UiColor clear{26, 26, 36, 255};              ///< frame clear before the canvas
    UiColor missingBackground{20, 24, 38, 255};  ///< background texture not found
    UiColor placeholderFill{30, 41, 59, 220};    ///< character sprite not found
    UiColor placeholderBorder{56, 189, 248, 255};
};

/// Project-wide defaults of the dialogue box (component `dialogue`).
/// Precedence: a key written in the node's component > this > built-in.
struct UiDialogueDefaults {
    UiColor boxColor{15, 15, 26, 255};
    float boxOpacity = 0.88f;                    ///< 0..1
    UiColor borderColor{0, 240, 255, 255};
    float borderThickness = 2.0f;                ///< 0..16
    UiColor textColor{241, 245, 249, 255};
    float fontSize = 24.0f;                      ///< 8..96
    float speakerFontSize = 20.0f;
    UiColor speakerColor{56, 189, 248, 255};
    std::string textAlignment = "Left";          ///< Left | Center | Right
    float cornerRadius = 8.0f;                   ///< 0..64
};

/// Project-wide defaults of the choice buttons (component `choice`).
struct UiChoiceDefaults {
    UiColor backgroundColor{30, 41, 59, 255};
    UiColor hoverColor{14, 165, 233, 255};
    UiColor borderColor{56, 189, 248, 255};
    UiColor textColor{255, 255, 255, 255};
    float opacity = 1.0f;                        ///< 0..1
    float fontSize = 22.0f;
    std::string textAlignment = "Center";
    float borderThickness = 2.0f;                ///< 0..16
    float cornerRadius = 8.0f;                   ///< 0..64
};

/// Speaker name tag (the small badge above the dialogue box).
struct UiSpeakerTagTheme {
    UiColor textColor{255, 255, 255, 255};
    UiColor borderColor{255, 255, 255, 180};
    float borderWidth = 1.0f;                    ///< physical px, 0..8
};

/// Marks of the keyboard / mouse focused choice button.
struct UiChoiceFocusTheme {
    std::optional<UiColor> innerColor;           ///< default: the button's text colour
    std::optional<UiColor> barColor;             ///< default: the button's border colour
    float barWidth = 6.0f;                       ///< virtual px, 0..64
    float inset = 3.0f;                          ///< virtual px, 0..32
};

struct UiStoryTheme {
    UiDialogueDefaults dialogue;
    UiChoiceDefaults choice;
    UiSpeakerTagTheme speakerTag;
    UiChoiceFocusTheme choiceFocus;
    /// The components carry `corner_radius` (dialogue, choice) and the choice's
    /// `border_thickness`, but the renderer never drew them. They stay
    /// undrawn unless this is true (the editor writes 8 / 2 into every
    /// component, so honouring them silently would change old games).
    bool honorShapeFields = false;
};

struct UiFontTheme {
    std::string menu;      ///< menus, badges, toast, Ekstralar
    std::string dialogue;  ///< dialogue text
    std::string speaker;   ///< speaker tag
};

struct UiTheme {
    UiMenuTheme menu;
    UiExtrasTheme extras;
    UiToastTheme toast;
    UiBadgeTheme badges;
    UiSceneTheme scene;
    UiStoryTheme story;
    UiFontTheme fonts;

    bool operator==(const UiTheme&) const;
};

/// What the loader reports next to the theme.
struct UiThemeLoad {
    UiTheme theme;
    /// Optional palette that replaces `theme` while the player's high-contrast
    /// setting is on (`high_contrast` in the file). Menus, Ekstralar, toast,
    /// badges and scene colours only.
    std::optional<UiTheme> highContrast;
    std::vector<std::string> diagnostics;  ///< one line per rejected value / file
    bool fileFound = false;                ///< a ui_theme.json was read (even an unusable one)
    bool usable = false;                   ///< the file parsed (valid or not per field)
};

/// Parses a ui_theme.json document. Never throws. A file that is too large, is
/// not a JSON object or does not declare `"format": 1` yields the built-in
/// theme and one diagnostic. A bad value (colour, number, string, path) keeps
/// that field's default and adds a diagnostic; a number outside its range is
/// clamped (diagnostic too); unknown keys are ignored. At most 32 diagnostics.
UiThemeLoad parseUiTheme(std::string_view json);

/// The theme as a ui_theme.json-shaped object (every key, resolved values,
/// `"format": 1`); the editor reads it, the C API embeds it. Compact JSON.
std::string uiThemeToJson(const UiTheme& theme);

/// The built-in theme (== a default-constructed UiTheme).
inline UiTheme builtinUiTheme() { return UiTheme{}; }

/// True for a VFS-relative font path: .ttf / .otf, bounded, no control bytes,
/// not absolute, no ".." segment.
bool isSafeThemeFontPath(std::string_view path);

}  // namespace Rowl::State
