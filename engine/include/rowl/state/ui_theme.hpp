#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// ── T2: skins ──────────────────────────────────────────────────────────────
// A skin is how one surface of the player UI is dressed: an image (nine-slice
// frame, stretched, tiled, ...) plus fill / border colours. Every surface uses
// the same shape. A surface WITHOUT an image keeps drawing with its T1 colours
// (the look before skins existed); with an image the colours of the surface
// are not drawn unless the skin names them (`fill`, `border`).

enum class UiSkinMode : uint8_t { NineSlice, Stretch, Tile, Fit, Cover, Center };

/// State variants of a skin set. `Selected`, `Focused` and `Hover` stand in for
/// one another (see UiSkinSet::find); Locked / Empty / Corrupt / Disabled /
/// Pressed fall back to Normal.
enum class UiSkinState : uint8_t {
    Normal, Selected, Focused, Hover, Pressed, Disabled, Locked, Empty, Corrupt
};
inline constexpr std::size_t kUiSkinStateCount = 9;

struct UiSkin {
    std::string image;                        ///< VFS path; empty = no image
    UiSkinMode mode = UiSkinMode::Stretch;
    std::array<float, 4> slice{0, 0, 0, 0};   ///< nine_slice insets l,t,r,b in image pixels
    float sliceScale = 1.0f;                  ///< on-screen scale of the image (edges, tiles, centre)
    UiColor tint{255, 255, 255, 255};         ///< multiplies the image
    float opacity = 1.0f;                     ///< 0..1
    bool smooth = true;                       ///< filter: linear (true) / nearest (false)
    std::optional<UiColor> fill;              ///< overrides the surface's own fill colour
    std::optional<UiColor> border;            ///< overrides the surface's own border colour
    std::optional<float> borderWidth;         ///< physical px, 0..32
    std::optional<float> cornerRadius;        ///< virtual px, 0..128 (rounded fill / border)
    std::optional<std::array<float, 4>> padding;  ///< content inset l,t,r,b, virtual px
    bool operator==(const UiSkin&) const = default;
    /// True when the skin draws something by itself (an image or named colours).
    bool visible() const { return !image.empty() || fill.has_value() || border.has_value(); }
};

/// A skin with state variants. A variant starts as a copy of `normal` and
/// overrides only the keys it names.
struct UiSkinSet {
    UiSkin normal;
    std::array<std::optional<UiSkin>, kUiSkinStateCount> variant;
    bool none = false;  ///< "none" / false: the surface draws no panel at all
    bool operator==(const UiSkinSet&) const = default;
    /// The variant that answers `state` (own variant, then the stand-ins), or null.
    const UiSkin* find(UiSkinState state) const;
    /// find(), else `normal`.
    const UiSkin& pick(UiSkinState state) const;
    bool anyImage() const;
};

inline UiSkin makeSkinWithMode(UiSkinMode mode) {
    UiSkin skin;
    skin.mode = mode;
    return skin;
}

/// Menu pages a theme can dress one by one (`screens.<name>`).
inline constexpr std::size_t kUiScreenCount = 11;
inline constexpr std::array<const char*, kUiScreenCount> kUiScreenNames = {
    "title", "pause", "save", "load", "preferences", "history",
    "extras_root", "gallery", "music", "achievements", "confirm"};
enum class UiScreen : uint8_t {
    Title, Pause, Save, Load, Preferences, History, ExtrasRoot, Gallery, Music, Achievements, Confirm
};
/// Index of a page name ("title", "save", ...), or -1.
int uiScreenIndex(std::string_view name);

/// T2b: where a page puts its parts (`screens.<page>.layout`, virtual 1920x1080
/// canvas). Every key is optional; what is not named keeps the built-in place (and
/// follows a moved panel / row block: title, hint, counter, thumbnail and the list
/// areas are placed relative to them unless named). The checks (off the canvas, zero
/// size, rows that do not fit) run when the engine installs the theme - see
/// core/menu_layout.hpp. Alignment strings are "left" | "center" | "right" or "" (unset).
struct UiScreenLayout {
    std::optional<float> panelX, panelY, panelW, panelH;
    std::optional<float> titleX, titleY, titleW;
    std::string titleAlign;
    std::optional<float> rowX, rowY, rowW, rowH, rowGap;
    std::optional<float> compactY, compactH, compactGap, comfortRows;
    std::string labelAlign, valueAlign;
    std::optional<float> labelWidth, valueX, valueWidth, rowPadX;
    std::optional<float> hintX, hintY, hintW;
    std::string hintAlign;
    std::optional<float> counterX, counterY, counterW;
    std::optional<float> thumbX, thumbY, thumbW, thumbH;
    bool thumbHidden = false;          ///< no thumbnail box on this slot page
    std::optional<float> sliderStart, sliderEndInset, sliderValueInset;
    std::optional<float> galleryColumns, galleryCellW, galleryThumbH, galleryGapX, galleryGapY, galleryY,
        galleryBottom, galleryCaptionH;
    std::optional<float> achY, achBottom, achRowH, achGap;
    std::optional<float> historyY, historyBottom, historyPadX, historyGap;
    std::optional<float> cardColumns, cardH, cardGapX, cardGapY, cardY, cardBottom;
    std::optional<float> slotsPerPage;
    std::optional<float> pagerX, pagerY, pagerW, pagerH;
    bool operator==(const UiScreenLayout&) const = default;
};

/// Per-page overrides of the menu look. Whatever a page does not name follows
/// the `menu` defaults.
struct UiScreenTheme {
    std::optional<UiSkinSet> panel;       ///< panel skin ("none" = no panel at all)
    std::optional<UiSkinSet> row;         ///< row skin (normal / selected / disabled)
    std::optional<UiColor> dim;           ///< veil over the story frame
    std::optional<UiColor> titleColor;
    std::optional<float> titlePx;         ///< 8..120
    bool titleHidden = false;
    bool hintHidden = false;
    std::optional<UiColor> counterColor;  ///< "3 / 10" counter of History / Ekstralar
    std::optional<float> counterPx;
    UiScreenLayout layout;                ///< T2b: where the page puts its parts
    bool operator==(const UiScreenTheme&) const = default;
};

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
    UiSkinSet panelSkin;    ///< T2: the panel behind the rows (default for every page)
    UiSkinSet rowSkin;      ///< T2: normal / selected / disabled row
    UiSkinSet overlaySkin;  ///< T2: full-window image over the veil
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
    UiSkinSet cellSkin;                          ///< T2: gallery cell (normal / locked / selected)
    UiSkin lockedImage = makeSkinWithMode(UiSkinMode::Fit);  ///< T2: picture in a locked cell (instead of "?")
    UiSkinSet viewerFrameSkin;                   ///< T2: frame around the viewed picture
    UiSkinSet plateSkin;                         ///< T2: achievement plate (normal / locked / selected)
    UiSkin lockedIcon = makeSkinWithMode(UiSkinMode::Fit);   ///< T2: icon of a locked achievement
    float iconSize = 72.0f;                      ///< achievement icon box, virtual px, 16..96
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
    UiSkinSet skin;                              ///< T2
    std::string anchor = "top_center";           ///< top_center|top_left|top_right|bottom_center|bottom_left|bottom_right
    float marginX = 0.0f;                        ///< shifts the capsule sideways (towards the centre for corners)
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
    UiSkinSet autoSkin;                          ///< T2
    UiSkinSet skipSkin;
    std::string anchor = "top_right";            ///< top_right|top_left|bottom_right|bottom_left
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
    UiSkinSet skin;                              ///< T2: the box (a component's custom_box_texture wins)
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
    UiSkinSet skin;                              ///< T2: normal / focused / pressed / disabled
    bool showDisabled = false;                   ///< T2: draw options whose condition is false
    UiColor disabledText{148, 163, 184, 255};
};

/// Speaker name tag (the small badge above the dialogue box).
struct UiSpeakerTagTheme {
    UiColor textColor{255, 255, 255, 255};
    UiColor borderColor{255, 255, 255, 180};
    float borderWidth = 1.0f;                    ///< physical px, 0..8
    UiSkinSet skin;                              ///< T2
    float offsetX = 0.0f;                        ///< virtual px, moves the tag from its place
    float offsetY = 0.0f;
};

/// T2: "text finished, press to go on" mark on the dialogue box.
struct UiContinueIndicator {
    UiSkin skin = makeSkinWithMode(UiSkinMode::Fit);  ///< off while skin.image is empty
    float width = 32.0f;                         ///< virtual px, 4..256
    float height = 32.0f;
    float offsetX = -24.0f;                      ///< from the box's bottom-right corner (inward is negative)
    float offsetY = -20.0f;
    std::string animation = "bounce";            ///< none | bounce | blink
    float period = 1.0f;                         ///< seconds, 0.2..10
    float amplitude = 6.0f;                      ///< bounce height, virtual px, 0..64
    bool operator==(const UiContinueIndicator&) const = default;
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
    UiContinueIndicator continueIndicator;
    /// The components carry `corner_radius` (dialogue, choice) and the choice's
    /// `border_thickness`, but the renderer never drew them. They stay
    /// undrawn unless this is true (the editor writes 8 / 2 into every
    /// component, so honouring them silently would change old games).
    bool honorShapeFields = false;
};

/// T2: save / load pages.
struct UiSlotsTheme {
    std::string layout = "list";                 ///< T2b: "list" (rows) | "cards" (grid of cards with thumbnail)
    float numberBase = 0.0f;                        ///< T2b: 0 | 1 - the number shown in "Slot 0" labels (files unchanged)
    UiSkinSet rowSkin;                           ///< normal = filled slot; selected; empty; corrupt
    UiSkinSet thumbFrameSkin;                    ///< frame around the thumbnail box
    UiSkin thumbPlaceholder = makeSkinWithMode(UiSkinMode::Fit);  ///< shown where the slot has no thumbnail
    UiSkin newImage = makeSkinWithMode(UiSkinMode::Fit);          ///< shown on the Save page for an empty slot
};

/// T2: the title screen's own background, logo and music.
struct UiTitleScreenTheme {
    UiSkin background = makeSkinWithMode(UiSkinMode::Cover);  ///< full-window image (mode: cover / fit / stretch ...)
    UiSkin logo = makeSkinWithMode(UiSkinMode::Fit);
    float logoX = 560.0f;                        ///< virtual canvas rect of the logo
    float logoY = 30.0f;
    float logoW = 800.0f;
    float logoH = 220.0f;
    std::string music;                           ///< VFS path of the title music ("" = none)
    std::optional<UiColor> dim;                  ///< veil on the title (default: none while a background shows)
    bool hideStoryFrame = false;                 ///< hide the story frame even without a background
};

/// T2: custom mouse cursor of the game window.
struct UiCursorTheme {
    std::string image;                           ///< PNG; "" = the system cursor
    float hotspotX = 0.0f;                       ///< image pixels
    float hotspotY = 0.0f;
    float scale = 1.0f;                          ///< 0.25..4 (cursor edge max 128 px)
};

/// T2: Preferences value rows.
struct UiPreferencesTheme {
    std::string valueStyle = "text";             ///< text | slider
    UiSkinSet trackSkin;
    UiSkinSet fillSkin;
    UiSkinSet knobSkin;                          ///< normal / pressed (while dragging)
    float trackHeight = 14.0f;                   ///< virtual px, 2..64
    float knobSize = 28.0f;                      ///< 4..96
    bool showValue = true;                       ///< keep "100%" next to the slider
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
    std::array<UiScreenTheme, kUiScreenCount> screens;  ///< T2
    UiSlotsTheme slots;
    UiTitleScreenTheme titleScreen;
    UiCursorTheme cursor;
    UiPreferencesTheme preferences;

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

/// Every image path a theme refers to, with the key that names it
/// ("menu.panel_skin.image"); the engine checks them when the theme is installed.
std::vector<std::pair<std::string, std::string>> uiThemeImagePaths(const UiTheme& theme);

/// Copy of `theme` without images, "none" panels, the title background / logo,
/// the cursor and slider styling: what a player with High Contrast on gets
/// unless the file says `"high_contrast": {"keep_skins": true}`.
UiTheme uiThemeWithoutSkins(const UiTheme& theme);

/// True when the theme draws any image skin / custom background.
bool uiThemeHasSkins(const UiTheme& theme);

/// True for a VFS-relative image path (.png .jpg .jpeg .bmp .tga .gif).
bool isSafeThemeImagePath(std::string_view path);
/// True for a VFS-relative audio path (.ogg .wav .mp3 .flac).
bool isSafeThemeAudioPath(std::string_view path);

/// True for a VFS-relative font path: .ttf / .otf, bounded, no control bytes,
/// not absolute, no ".." segment.
bool isSafeThemeFontPath(std::string_view path);

}  // namespace Rowl::State
