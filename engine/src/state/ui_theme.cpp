/**
 * ui_theme.cpp — T1 parser of `Assets/json/ui_theme.json` (player-interface
 * theme). Pure data: no VFS, no engine, no renderer; the engine reads the file
 * and hands the bytes to parseUiTheme. One field table (visitTheme) drives both
 * the parser and the serializer, so the file format and GetUiThemeJson cannot
 * drift apart.
 */
#include "rowl/state/ui_theme.hpp"

#include "rowl/text/hex_color.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <set>

namespace Rowl::State {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxPathBytes = 260;
constexpr std::size_t kMaxEchoBytes = 40;

char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Short, control-free echo of an offending value for a diagnostic.
std::string echo(const Json& value) {
    std::string text = value.is_string() ? value.get<std::string>() : value.dump();
    std::string out;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) continue;
        out.push_back(c);
        if (out.size() >= kMaxEchoBytes) {
            out += "...";
            break;
        }
    }
    return out;
}

// ── the field table ─────────────────────────────────────────────────────────
// `full` = the whole theme (false for the high_contrast override: menus,
// Ekstralar, toast, badges and scene colours only).
template <typename V>
void visitTheme(UiTheme& t, V& v, bool full) {
    auto& m = t.menu;
    v.color("menu", "dim", m.dim);
    v.color("menu", "panel_fill", m.panelFill);
    v.color("menu", "panel_border", m.panelBorder);
    v.num("menu", "panel_border_width", m.panelBorderWidth, 0.0f, 8.0f);
    v.color("menu", "title", m.title);
    v.color("menu", "text", m.text);
    v.color("menu", "dim_text", m.dimText);
    v.color("menu", "disabled_text", m.disabledText);
    v.color("menu", "selected_fill", m.selectedFill);
    v.color("menu", "selected_bar", m.selectedBar);
    v.num("menu", "selected_bar_width", m.selectedBarWidth, 0.0f, 64.0f);
    v.color("menu", "selected_text", m.selectedText);
    v.color("menu", "hint", m.hint);
    v.color("menu", "thumb_border", m.thumbBorder);
    v.color("menu", "history_speaker", m.historySpeaker);
    v.num("menu", "title_px", m.titlePx, 8.0f, 120.0f);
    v.num("menu", "row_px", m.rowPx, 8.0f, 120.0f);
    v.num("menu", "value_px", m.valuePx, 8.0f, 120.0f);
    v.num("menu", "detail_px", m.detailPx, 8.0f, 120.0f);
    v.num("menu", "hint_px", m.hintPx, 8.0f, 120.0f);
    v.num("menu", "history_speaker_px", m.historySpeakerPx, 8.0f, 120.0f);

    auto& e = t.extras;
    v.color("extras", "cell_fill", e.cellFill);
    v.color("extras", "locked_fill", e.lockedFill);
    v.color("extras", "cell_border_selected", e.cellBorderSelected);
    v.color("extras", "cell_border", e.cellBorder);
    v.color("extras", "locked_border", e.lockedBorder);
    v.color("extras", "plate", e.plate);
    v.color("extras", "plate_selected", e.plateSelected);
    v.color("extras", "plate_bar", e.plateBar);
    v.color("extras", "title_selected", e.titleSelected);
    v.color("extras", "text", e.text);
    v.color("extras", "dim_text", e.dimText);
    v.color("extras", "description", e.description);
    v.color("extras", "viewer_dim", e.viewerDim);
    v.color("extras", "viewer_frame", e.viewerFrame);
    v.color("extras", "viewer_title", e.viewerTitle);
    v.num("extras", "mark_px", e.markPx, 8.0f, 200.0f);
    v.num("extras", "caption_px", e.captionPx, 8.0f, 120.0f);
    v.num("extras", "viewer_title_px", e.viewerTitlePx, 8.0f, 120.0f);

    auto& o = t.toast;
    v.color("toast", "fill", o.fill);
    v.color("toast", "text", o.text);
    v.color("toast", "accent", o.accent);
    v.color("toast", "rule", o.rule);
    v.num("toast", "rule_px", o.rulePx, 0.0f, 16.0f);
    v.num("toast", "px", o.px, 8.0f, 120.0f);
    v.num("toast", "height", o.height, 16.0f, 200.0f);
    v.num("toast", "margin_top", o.marginTop, 0.0f, 400.0f);
    v.num("toast", "pad_x", o.padX, 0.0f, 200.0f);
    v.num("toast", "max_text_width", o.maxTextWidth, 100.0f, 1800.0f);

    auto& b = t.badges;
    v.color("badges", "auto_fill", b.autoFill);
    v.color("badges", "auto_text", b.autoText);
    v.color("badges", "skip_fill", b.skipFill);
    v.color("badges", "skip_text", b.skipText);
    v.num("badges", "px", b.px, 8.0f, 96.0f);
    v.num("badges", "width", b.width, 40.0f, 400.0f);
    v.num("badges", "height", b.height, 16.0f, 200.0f);
    v.num("badges", "gap", b.gap, 0.0f, 100.0f);
    v.num("badges", "margin_right", b.marginRight, 0.0f, 400.0f);
    v.num("badges", "margin_top", b.marginTop, 0.0f, 400.0f);

    auto& s = t.scene;
    v.color("scene", "letterbox", s.letterbox);
    v.color("scene", "clear", s.clear);
    v.color("scene", "missing_background", s.missingBackground);
    v.color("scene", "placeholder_fill", s.placeholderFill);
    v.color("scene", "placeholder_border", s.placeholderBorder);

    if (!full) return;

    // ── T2: skins and what goes with them (never part of the high_contrast override) ──
    v.skin("menu", "panel_skin", m.panelSkin);
    v.skin("menu", "row_skin", m.rowSkin);
    v.skin("menu", "overlay_skin", m.overlaySkin);
    for (std::size_t i = 0; i < kUiScreenCount; ++i) {
        const std::string sec = std::string("screens.") + kUiScreenNames[i];
        auto& screen = t.screens[i];
        v.skinOpt(sec.c_str(), "panel_skin", screen.panel);
        v.skinOpt(sec.c_str(), "row_skin", screen.row);
        v.optColor(sec.c_str(), "dim", screen.dim);
        v.optColor(sec.c_str(), "title_color", screen.titleColor);
        v.optNum(sec.c_str(), "title_px", screen.titlePx, 8.0f, 120.0f);
        v.flag(sec.c_str(), "title_hidden", screen.titleHidden);
        v.flag(sec.c_str(), "hint_hidden", screen.hintHidden);
        v.optColor(sec.c_str(), "counter_color", screen.counterColor);
        v.optNum(sec.c_str(), "counter_px", screen.counterPx, 8.0f, 120.0f);
    }
    v.skin("slots", "row_skin", t.slots.rowSkin);
    v.skin("slots", "thumb_frame_skin", t.slots.thumbFrameSkin);
    v.skin1("slots", "thumb_placeholder", t.slots.thumbPlaceholder);
    v.skin1("slots", "new_image", t.slots.newImage);
    auto& ts = t.titleScreen;
    v.skin1("title_screen", "background", ts.background);
    v.skin1("title_screen", "logo", ts.logo);
    v.num("title_screen", "logo_x", ts.logoX, -1920.0f, 1920.0f);
    v.num("title_screen", "logo_y", ts.logoY, -1080.0f, 1080.0f);
    v.num("title_screen", "logo_w", ts.logoW, 8.0f, 3840.0f);
    v.num("title_screen", "logo_h", ts.logoH, 8.0f, 2160.0f);
    v.audioPath("title_screen", "music", ts.music);
    v.optColor("title_screen", "dim", ts.dim);
    v.flag("title_screen", "hide_story_frame", ts.hideStoryFrame);
    v.imagePath("cursor", "image", t.cursor.image);
    v.num("cursor", "hotspot_x", t.cursor.hotspotX, 0.0f, 512.0f);
    v.num("cursor", "hotspot_y", t.cursor.hotspotY, 0.0f, 512.0f);
    v.num("cursor", "scale", t.cursor.scale, 0.25f, 4.0f);
    v.oneOf("preferences", "value_style", t.preferences.valueStyle, {"text", "slider"});
    v.skin("preferences", "track_skin", t.preferences.trackSkin);
    v.skin("preferences", "fill_skin", t.preferences.fillSkin);
    v.skin("preferences", "knob_skin", t.preferences.knobSkin);
    v.num("preferences", "track_height", t.preferences.trackHeight, 2.0f, 64.0f);
    v.num("preferences", "knob_size", t.preferences.knobSize, 4.0f, 96.0f);
    v.flag("preferences", "show_value", t.preferences.showValue);
    v.skin("extras", "cell_skin", e.cellSkin);
    v.skin1("extras", "locked_image", e.lockedImage);
    v.skin("extras", "viewer_frame_skin", e.viewerFrameSkin);
    v.skin("extras", "plate_skin", e.plateSkin);
    v.skin1("extras", "locked_icon", e.lockedIcon);
    v.num("extras", "icon_size", e.iconSize, 16.0f, 96.0f);
    v.skin("toast", "skin", o.skin);
    v.oneOf("toast", "anchor", o.anchor,
            {"top_center", "top_left", "top_right", "bottom_center", "bottom_left", "bottom_right"});
    v.num("toast", "margin_x", o.marginX, -960.0f, 960.0f);
    v.skin("badges", "auto_skin", b.autoSkin);
    v.skin("badges", "skip_skin", b.skipSkin);
    v.oneOf("badges", "anchor", b.anchor, {"top_right", "top_left", "bottom_right", "bottom_left"});

    auto& d = t.story.dialogue;
    v.color("story.dialogue", "box_color", d.boxColor);
    v.num("story.dialogue", "box_opacity", d.boxOpacity, 0.0f, 1.0f);
    v.color("story.dialogue", "border_color", d.borderColor);
    v.num("story.dialogue", "border_thickness", d.borderThickness, 0.0f, 16.0f);
    v.color("story.dialogue", "text_color", d.textColor);
    v.num("story.dialogue", "font_size", d.fontSize, 8.0f, 96.0f);
    v.num("story.dialogue", "speaker_font_size", d.speakerFontSize, 8.0f, 96.0f);
    v.color("story.dialogue", "speaker_color", d.speakerColor);
    v.align("story.dialogue", "text_alignment", d.textAlignment);
    v.num("story.dialogue", "corner_radius", d.cornerRadius, 0.0f, 64.0f);
    v.skin("story.dialogue", "skin", d.skin);

    auto& c = t.story.choice;
    v.color("story.choice", "background_color", c.backgroundColor);
    v.color("story.choice", "hover_color", c.hoverColor);
    v.color("story.choice", "border_color", c.borderColor);
    v.color("story.choice", "text_color", c.textColor);
    v.num("story.choice", "opacity", c.opacity, 0.0f, 1.0f);
    v.num("story.choice", "font_size", c.fontSize, 8.0f, 96.0f);
    v.align("story.choice", "text_alignment", c.textAlignment);
    v.num("story.choice", "border_thickness", c.borderThickness, 0.0f, 16.0f);
    v.num("story.choice", "corner_radius", c.cornerRadius, 0.0f, 64.0f);
    v.skin("story.choice", "skin", c.skin);
    v.flag("story.choice", "show_disabled", c.showDisabled);
    v.color("story.choice", "disabled_text", c.disabledText);

    auto& tag = t.story.speakerTag;
    v.color("story.speaker_tag", "text_color", tag.textColor);
    v.color("story.speaker_tag", "border_color", tag.borderColor);
    v.num("story.speaker_tag", "border_width", tag.borderWidth, 0.0f, 8.0f);
    v.skin("story.speaker_tag", "skin", tag.skin);
    v.num("story.speaker_tag", "offset_x", tag.offsetX, -960.0f, 960.0f);
    v.num("story.speaker_tag", "offset_y", tag.offsetY, -540.0f, 540.0f);

    auto& f = t.story.choiceFocus;
    v.optColor("story.choice_focus", "inner_color", f.innerColor);
    v.optColor("story.choice_focus", "bar_color", f.barColor);
    v.num("story.choice_focus", "bar_width", f.barWidth, 0.0f, 64.0f);
    v.num("story.choice_focus", "inset", f.inset, 0.0f, 32.0f);

    auto& ci = t.story.continueIndicator;
    v.skin1("story.continue_indicator", "skin", ci.skin);
    v.num("story.continue_indicator", "width", ci.width, 4.0f, 256.0f);
    v.num("story.continue_indicator", "height", ci.height, 4.0f, 256.0f);
    v.num("story.continue_indicator", "offset_x", ci.offsetX, -1920.0f, 1920.0f);
    v.num("story.continue_indicator", "offset_y", ci.offsetY, -1080.0f, 1080.0f);
    v.oneOf("story.continue_indicator", "animation", ci.animation, {"none", "bounce", "blink"});
    v.num("story.continue_indicator", "period", ci.period, 0.2f, 10.0f);
    v.num("story.continue_indicator", "amplitude", ci.amplitude, 0.0f, 64.0f);

    v.flag("story", "honor_shape_fields", t.story.honorShapeFields);

    v.path("fonts", "menu", t.fonts.menu);
    v.path("fonts", "dialogue", t.fonts.dialogue);
    v.path("fonts", "speaker", t.fonts.speaker);
}

// ── parsing visitor ─────────────────────────────────────────────────────────
struct Reader {
    const Json& root;
    std::vector<std::string>& diagnostics;
    std::set<std::string>& explicitKeys;
    std::string prefix;  ///< "" or "high_contrast." (for the diagnostics)
    std::set<std::string> badSections;

    void note(const std::string& message) {
        if (diagnostics.size() < kUiThemeMaxDiagnostics) {
            diagnostics.push_back("ui_theme.json: " + message);
        } else if (diagnostics.size() == kUiThemeMaxDiagnostics) {
            diagnostics.push_back("ui_theme.json: further diagnostics omitted");
        }
    }

    /// The section object, or nullptr (absent / not an object -> one diagnostic).
    const Json* section(const char* path) {
        const Json* node = &root;
        std::string walked;
        std::string rest = path;
        while (!rest.empty()) {
            const std::size_t dot = rest.find('.');
            const std::string part = rest.substr(0, dot);
            rest = dot == std::string::npos ? std::string() : rest.substr(dot + 1);
            walked += (walked.empty() ? "" : ".") + part;
            const auto it = node->find(part);
            if (it == node->end()) return nullptr;
            if (!it->is_object()) {
                if (badSections.insert(walked).second) {
                    note("'" + prefix + walked + "' is not an object; ignored");
                }
                return nullptr;
            }
            node = &*it;
        }
        return node;
    }

    const Json* field(const char* sec, const char* key) {
        const Json* object = section(sec);
        if (object == nullptr) return nullptr;
        const auto it = object->find(key);
        return it == object->end() ? nullptr : &*it;
    }

    std::string label(const char* sec, const char* key) const {
        return prefix + sec + "." + key;
    }

    static bool parseColor(const Json& value, UiColor& out) {
        if (!value.is_string()) return false;
        const std::string text = value.get<std::string>();
        if ((text.size() != 7 && text.size() != 9) || text.front() != '#') return false;
        Rowl::Text::HexColor parsed;
        if (!Rowl::Text::tryParseHexColor(text, parsed)) return false;
        out = UiColor{parsed.r, parsed.g, parsed.b, parsed.a};
        return true;
    }

    void color(const char* sec, const char* key, UiColor& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        UiColor parsed;
        if (!parseColor(*value, parsed)) {
            note(label(sec, key) + ": invalid colour '" + echo(*value) +
                 "' (want \"#RRGGBB\" or \"#RRGGBBAA\"); default kept");
            return;
        }
        out = parsed;
        explicitKeys.insert(std::string(sec) + "." + key);
    }

    void optColor(const char* sec, const char* key, std::optional<UiColor>& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (value->is_null()) {  // GetUiThemeJson writes null for "unset"
            out.reset();
            return;
        }
        UiColor parsed;
        if (!parseColor(*value, parsed)) {
            note(label(sec, key) + ": invalid colour '" + echo(*value) +
                 "' (want \"#RRGGBB\" or \"#RRGGBBAA\"); default kept");
            return;
        }
        out = parsed;
    }

    void num(const char* sec, const char* key, float& out, float lo, float hi) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (!value->is_number() || value->is_boolean()) {
            note(label(sec, key) + ": not a number '" + echo(*value) + "'; default kept");
            return;
        }
        const double raw = value->get<double>();
        if (!std::isfinite(raw)) {
            note(label(sec, key) + ": not a finite number; default kept");
            return;
        }
        const double clamped = std::clamp(raw, static_cast<double>(lo), static_cast<double>(hi));
        if (clamped != raw) {
            note(label(sec, key) + ": " + echo(*value) + " is outside " +
                 Json(lo).dump() + ".." + Json(hi).dump() + "; clamped");
        }
        out = static_cast<float>(clamped);
        explicitKeys.insert(std::string(sec) + "." + key);
    }

    void align(const char* sec, const char* key, std::string& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        std::string text = value->is_string() ? value->get<std::string>() : std::string();
        for (char& c : text) c = lowerAscii(c);
        if (text == "left") out = "Left";
        else if (text == "center") out = "Center";
        else if (text == "right") out = "Right";
        else note(label(sec, key) + ": invalid alignment '" + echo(*value) +
                  "' (Left / Center / Right); default kept");
    }

    void flag(const char* sec, const char* key, bool& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (!value->is_boolean()) {
            note(label(sec, key) + ": not a boolean '" + echo(*value) + "'; default kept");
            return;
        }
        out = value->get<bool>();
    }

    void path(const char* sec, const char* key, std::string& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (value->is_string() && value->get<std::string>().empty()) {  // "" = no theme font
            out.clear();
            return;
        }
        if (!value->is_string() || !isSafeThemeFontPath(value->get<std::string>())) {
            note(label(sec, key) + ": invalid font path '" + echo(*value) +
                 "' (VFS-relative .ttf / .otf, no '..'); default kept");
            return;
        }
        out = value->get<std::string>();
    }

    void optNum(const char* sec, const char* key, std::optional<float>& out, float lo, float hi) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (value->is_null()) {
            out.reset();
            return;
        }
        float parsed = out.value_or(0.0f);
        num(sec, key, parsed, lo, hi);  // reports and clamps like every number
        if (value->is_number() && std::isfinite(value->get<double>())) out = parsed;
    }

    void oneOf(const char* sec, const char* key, std::string& out, std::initializer_list<const char*> allowed) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        std::string text = value->is_string() ? value->get<std::string>() : std::string();
        for (char& c : text) c = lowerAscii(c);
        for (const char* option : allowed) {
            if (text == option) {
                out = text;
                return;
            }
        }
        std::string list;
        for (const char* option : allowed) list += (list.empty() ? "" : " / ") + std::string(option);
        note(label(sec, key) + ": invalid value '" + echo(*value) + "' (" + list + "); default kept");
    }

    void imagePath(const char* sec, const char* key, std::string& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (value->is_string() && value->get<std::string>().empty()) {
            out.clear();
            return;
        }
        if (!value->is_string() || !isSafeThemeImagePath(value->get<std::string>())) {
            note(label(sec, key) + ": invalid image path '" + echo(*value) +
                 "' (VFS-relative .png / .jpg / .bmp / .tga / .gif, no '..'); default kept");
            return;
        }
        out = value->get<std::string>();
    }

    void audioPath(const char* sec, const char* key, std::string& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (value->is_string() && value->get<std::string>().empty()) {
            out.clear();
            return;
        }
        if (!value->is_string() || !isSafeThemeAudioPath(value->get<std::string>())) {
            note(label(sec, key) + ": invalid audio path '" + echo(*value) +
                 "' (VFS-relative .ogg / .wav / .mp3 / .flac, no '..'); default kept");
            return;
        }
        out = value->get<std::string>();
    }

    // ── skins ──
    bool readColorField(const Json& object, const char* key, std::optional<UiColor>& out, const std::string& where) {
        const auto it = object.find(key);
        if (it == object.end()) return false;
        if (it->is_null()) {
            out.reset();
            return true;
        }
        UiColor parsed;
        if (!parseColor(*it, parsed)) {
            note(where + "." + key + ": invalid colour '" + echo(*it) + "'; kept");
            return true;
        }
        out = parsed;
        return true;
    }

    bool readNumberField(const Json& object, const char* key, double& out, double lo, double hi, const std::string& where) {
        const auto it = object.find(key);
        if (it == object.end()) return false;
        if (!it->is_number() || it->is_boolean() || !std::isfinite(it->get<double>())) {
            note(where + "." + key + ": not a number '" + echo(*it) + "'; kept");
            return false;
        }
        const double raw = it->get<double>();
        out = std::clamp(raw, lo, hi);
        if (out != raw) note(where + "." + key + ": " + echo(*it) + " is outside " + Json(lo).dump() + ".." +
                             Json(hi).dump() + "; clamped");
        return true;
    }

    /// 1 number (all sides) or 4 numbers (l, t, r, b) -> 4 floats.
    bool readInsets(const Json& object, const char* key, std::array<float, 4>& out, double hi, const std::string& where) {
        const auto it = object.find(key);
        if (it == object.end() || it->is_null()) return false;
        std::array<double, 4> v{};
        if (it->is_number() && !it->is_boolean()) {
            v.fill(it->get<double>());
        } else if (it->is_array() && (it->size() == 1 || it->size() == 4)) {
            for (std::size_t i = 0; i < 4; ++i) {
                const Json& item = (*it)[it->size() == 1 ? 0 : i];
                if (!item.is_number() || item.is_boolean()) {
                    note(where + "." + key + ": entries must be numbers; kept");
                    return false;
                }
                v[i] = item.get<double>();
            }
        } else {
            note(where + "." + key + ": want a number or [left, top, right, bottom]; kept");
            return false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (!std::isfinite(v[i])) {
                note(where + "." + key + ": not a finite number; kept");
                return false;
            }
            const double clamped = std::clamp(v[i], 0.0, hi);
            if (clamped != v[i]) note(where + "." + key + ": clamped to 0.." + Json(hi).dump());
            out[i] = static_cast<float>(clamped);
        }
        return true;
    }

    /// Reads the skin keys of `object` over `skin` (absent keys stay).
    void readSkinKeys(const Json& object, UiSkin& skin, const std::string& where) {
        if (const auto it = object.find("image"); it != object.end()) {
            if (it->is_string() && it->get<std::string>().empty()) {
                skin.image.clear();
            } else if (it->is_string() && isSafeThemeImagePath(it->get<std::string>())) {
                skin.image = it->get<std::string>();
            } else {
                note(where + ".image: invalid image path '" + echo(*it) +
                     "' (VFS-relative .png / .jpg / .bmp / .tga / .gif, no '..'); kept");
            }
        }
        if (const auto it = object.find("mode"); it != object.end()) {
            std::string text = it->is_string() ? it->get<std::string>() : std::string();
            for (char& c : text) c = lowerAscii(c);
            if (text == "nine_slice") skin.mode = UiSkinMode::NineSlice;
            else if (text == "stretch") skin.mode = UiSkinMode::Stretch;
            else if (text == "tile") skin.mode = UiSkinMode::Tile;
            else if (text == "fit") skin.mode = UiSkinMode::Fit;
            else if (text == "cover") skin.mode = UiSkinMode::Cover;
            else if (text == "center") skin.mode = UiSkinMode::Center;
            else note(where + ".mode: invalid value '" + echo(*it) +
                      "' (nine_slice / stretch / tile / fit / cover / center); kept");
        }
        readInsets(object, "slice", skin.slice, 4096.0, where);
        double number = 0.0;
        if (readNumberField(object, "slice_scale", number, 0.05, 16.0, where)) skin.sliceScale = static_cast<float>(number);
        if (const auto it = object.find("tint"); it != object.end()) {
            UiColor parsed;
            if (parseColor(*it, parsed)) skin.tint = parsed;
            else note(where + ".tint: invalid colour '" + echo(*it) + "'; kept");
        }
        if (readNumberField(object, "opacity", number, 0.0, 1.0, where)) skin.opacity = static_cast<float>(number);
        if (const auto it = object.find("filter"); it != object.end()) {
            const std::string text = it->is_string() ? it->get<std::string>() : std::string();
            if (text == "linear") skin.smooth = true;
            else if (text == "nearest") skin.smooth = false;
            else note(where + ".filter: invalid value '" + echo(*it) + "' (linear / nearest); kept");
        }
        readColorField(object, "fill", skin.fill, where);
        readColorField(object, "border", skin.border, where);
        if (const auto it = object.find("border_width"); it != object.end() && it->is_null()) {
            skin.borderWidth.reset();
        } else if (readNumberField(object, "border_width", number, 0.0, 32.0, where)) {
            skin.borderWidth = static_cast<float>(number);
        }
        if (const auto it = object.find("corner_radius"); it != object.end() && it->is_null()) {
            skin.cornerRadius.reset();
        } else if (readNumberField(object, "corner_radius", number, 0.0, 128.0, where)) {
            skin.cornerRadius = static_cast<float>(number);
        }
        if (const auto it = object.find("padding"); it != object.end() && it->is_null()) {
            skin.padding.reset();
        } else {
            std::array<float, 4> pad = skin.padding.value_or(std::array<float, 4>{0, 0, 0, 0});
            if (readInsets(object, "padding", pad, 1000.0, where)) skin.padding = pad;
        }
    }

    /// One skin (no variants); a bare string is the image path; false / "none" clears it.
    void skin1(const char* sec, const char* key, UiSkin& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        const std::string where = label(sec, key);
        if (value->is_object()) {
            readSkinKeys(*value, out, where);
        } else if (value->is_string() && isSafeThemeImagePath(value->get<std::string>())) {
            out.image = value->get<std::string>();
        } else if ((value->is_boolean() && !value->get<bool>()) ||
                   (value->is_string() && (value->get<std::string>() == "none" || value->get<std::string>().empty()))) {
            out = UiSkin{};
        } else {
            note(where + ": want an object, an image path or \"none\"; default kept");
        }
    }

    /// A skin with state variants.
    void readSet(const Json& value, UiSkinSet& out, const std::string& where) {
        out.none = false;
        if ((value.is_boolean() && !value.get<bool>()) || (value.is_string() && value.get<std::string>() == "none")) {
            out.none = true;
            return;
        }
        if (value.is_string() && isSafeThemeImagePath(value.get<std::string>())) {
            out.normal.image = value.get<std::string>();
            return;
        }
        if (!value.is_object()) {
            note(where + ": want an object, an image path or \"none\"; default kept");
            return;
        }
        readSkinKeys(value, out.normal, where);
        static const char* const kStateKeys[kUiSkinStateCount] = {
            nullptr, "selected", "focused", "hover", "pressed", "disabled", "locked", "empty", "corrupt"};
        for (std::size_t i = 1; i < kUiSkinStateCount; ++i) {
            const auto it = value.find(kStateKeys[i]);
            if (it == value.end()) continue;
            if (it->is_null()) {
                out.variant[i].reset();
                continue;
            }
            if (!it->is_object()) {
                note(where + "." + kStateKeys[i] + ": want an object; ignored");
                continue;
            }
            UiSkin skin = out.normal;
            readSkinKeys(*it, skin, where + "." + kStateKeys[i]);
            out.variant[i] = std::move(skin);
        }
    }

    void skin(const char* sec, const char* key, UiSkinSet& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        UiSkinSet parsed = out;
        readSet(*value, parsed, label(sec, key));
        out = std::move(parsed);
    }

    void skinOpt(const char* sec, const char* key, std::optional<UiSkinSet>& out) {
        const Json* value = field(sec, key);
        if (value == nullptr) return;
        if (value->is_null()) {
            out.reset();
            return;
        }
        UiSkinSet parsed = out.value_or(UiSkinSet{});
        readSet(*value, parsed, label(sec, key));
        out = std::move(parsed);
    }
};

// ── serializing visitor ─────────────────────────────────────────────────────
struct Writer {
    Json root = Json::object();

    Json& at(const char* sec) {
        Json* node = &root;
        std::string rest = sec;
        while (!rest.empty()) {
            const std::size_t dot = rest.find('.');
            const std::string part = rest.substr(0, dot);
            rest = dot == std::string::npos ? std::string() : rest.substr(dot + 1);
            node = &(*node)[part];
        }
        return *node;
    }
    void color(const char* sec, const char* key, UiColor& value) { at(sec)[key] = uiColorToHex(value); }
    void optColor(const char* sec, const char* key, std::optional<UiColor>& value) {
        at(sec)[key] = value ? Json(uiColorToHex(*value)) : Json(nullptr);
    }
    void num(const char* sec, const char* key, float& value, float, float) {
        at(sec)[key] = std::round(static_cast<double>(value) * 10000.0) / 10000.0;
    }
    void align(const char* sec, const char* key, std::string& value) { at(sec)[key] = value; }
    void flag(const char* sec, const char* key, bool& value) { at(sec)[key] = value; }
    void path(const char* sec, const char* key, std::string& value) { at(sec)[key] = value; }
    void optNum(const char* sec, const char* key, std::optional<float>& value, float, float) {
        at(sec)[key] = value ? Json(std::round(static_cast<double>(*value) * 10000.0) / 10000.0) : Json(nullptr);
    }
    void oneOf(const char* sec, const char* key, std::string& value, std::initializer_list<const char*>) {
        at(sec)[key] = value;
    }
    void imagePath(const char* sec, const char* key, std::string& value) { at(sec)[key] = value; }
    void audioPath(const char* sec, const char* key, std::string& value) { at(sec)[key] = value; }

    static double round4(double v) { return std::round(v * 10000.0) / 10000.0; }
    static Json optColorJson(const std::optional<UiColor>& c) { return c ? Json(uiColorToHex(*c)) : Json(nullptr); }
    static Json insetsJson(const std::array<float, 4>& a) {
        return Json::array({round4(a[0]), round4(a[1]), round4(a[2]), round4(a[3])});
    }
    static Json skinJson(const UiSkin& k) {
        static const char* const kModes[] = {"nine_slice", "stretch", "tile", "fit", "cover", "center"};
        Json o = Json::object();
        o["image"] = k.image;
        o["mode"] = kModes[static_cast<int>(k.mode)];
        o["slice"] = insetsJson(k.slice);
        o["slice_scale"] = round4(k.sliceScale);
        o["tint"] = uiColorToHex(k.tint);
        o["opacity"] = round4(k.opacity);
        o["filter"] = k.smooth ? "linear" : "nearest";
        o["fill"] = optColorJson(k.fill);
        o["border"] = optColorJson(k.border);
        o["border_width"] = k.borderWidth ? Json(round4(*k.borderWidth)) : Json(nullptr);
        o["corner_radius"] = k.cornerRadius ? Json(round4(*k.cornerRadius)) : Json(nullptr);
        o["padding"] = k.padding ? insetsJson(*k.padding) : Json(nullptr);
        return o;
    }
    static Json setJson(const UiSkinSet& set) {
        if (set.none) return "none";
        static const char* const kStateKeys[kUiSkinStateCount] = {
            nullptr, "selected", "focused", "hover", "pressed", "disabled", "locked", "empty", "corrupt"};
        Json o = skinJson(set.normal);
        for (std::size_t i = 1; i < kUiSkinStateCount; ++i) {
            if (set.variant[i]) o[kStateKeys[i]] = skinJson(*set.variant[i]);
        }
        return o;
    }
    void skin1(const char* sec, const char* key, UiSkin& value) { at(sec)[key] = skinJson(value); }
    void skin(const char* sec, const char* key, UiSkinSet& value) { at(sec)[key] = setJson(value); }
    void skinOpt(const char* sec, const char* key, std::optional<UiSkinSet>& value) {
        at(sec)[key] = value ? setJson(*value) : Json(nullptr);
    }
};

// ── collecting visitors ─────────────────────────────────────────────────────
struct NullVisitor {
    void color(const char*, const char*, UiColor&) {}
    void optColor(const char*, const char*, std::optional<UiColor>&) {}
    void num(const char*, const char*, float&, float, float) {}
    void optNum(const char*, const char*, std::optional<float>&, float, float) {}
    void align(const char*, const char*, std::string&) {}
    void oneOf(const char*, const char*, std::string&, std::initializer_list<const char*>) {}
    void flag(const char*, const char*, bool&) {}
    void path(const char*, const char*, std::string&) {}
    void imagePath(const char*, const char*, std::string&) {}
    void audioPath(const char*, const char*, std::string&) {}
    void skin1(const char*, const char*, UiSkin&) {}
    void skin(const char*, const char*, UiSkinSet&) {}
    void skinOpt(const char*, const char*, std::optional<UiSkinSet>&) {}
};

/// Calls `fn(label, skin&)` for every skin (a set's normal and each variant).
template <typename Fn>
struct SkinWalker : NullVisitor {
    Fn fn;
    explicit SkinWalker(Fn f) : fn(std::move(f)) {}
    void each(const char* sec, const char* key, UiSkinSet& set) {
        const std::string base = std::string(sec) + "." + key;
        fn(base, set.normal, &set);
        for (std::size_t i = 1; i < kUiSkinStateCount; ++i) {
            if (set.variant[i]) fn(base, *set.variant[i], nullptr);
        }
    }
    void skin1(const char* sec, const char* key, UiSkin& s) { fn(std::string(sec) + "." + key, s, nullptr); }
    void skin(const char* sec, const char* key, UiSkinSet& set) { each(sec, key, set); }
    void skinOpt(const char* sec, const char* key, std::optional<UiSkinSet>& set) {
        if (set) each(sec, key, *set);
    }
    void imagePath(const char*, const char*, std::string&) {}
};

UiColor withAlpha(UiColor color, uint8_t alpha) {
    color.a = alpha;
    return color;
}

/// Fields that follow another field unless the file names them (so one
/// `menu.title` recolours the whole chrome). Defaults are the old constants,
/// which agree with their source everywhere below.
void applyInheritance(UiTheme& t, const std::set<std::string>& named) {
    const auto follow = [&](const char* key, UiColor& field, const UiColor& source) {
        if (!named.contains(key)) field = source;
    };
    const auto followRgb = [&](const char* key, UiColor& field, const UiColor& source) {
        if (!named.contains(key)) field = withAlpha(source, field.a);
    };
    const auto& m = t.menu;
    follow("menu.disabled_text", t.menu.disabledText, m.dimText);
    follow("menu.hint", t.menu.hint, m.dimText);
    follow("menu.selected_text", t.menu.selectedText, m.title);
    follow("menu.history_speaker", t.menu.historySpeaker, m.title);
    follow("menu.thumb_border", t.menu.thumbBorder, m.panelBorder);
    follow("extras.cell_border_selected", t.extras.cellBorderSelected, m.selectedBar);
    follow("extras.plate_selected", t.extras.plateSelected, m.selectedFill);
    follow("extras.plate_bar", t.extras.plateBar, m.selectedBar);
    follow("extras.title_selected", t.extras.titleSelected, m.title);
    follow("extras.text", t.extras.text, m.text);
    follow("extras.viewer_title", t.extras.viewerTitle, m.text);
    follow("extras.dim_text", t.extras.dimText, m.dimText);
    follow("extras.viewer_frame", t.extras.viewerFrame, m.panelBorder);
    followRgb("toast.fill", t.toast.fill, m.panelFill);
    follow("toast.text", t.toast.text, m.text);
    follow("toast.accent", t.toast.accent, m.title);
    followRgb("toast.rule", t.toast.rule, m.title);
    followRgb("badges.auto_fill", t.badges.autoFill, m.panelFill);
    follow("badges.auto_text", t.badges.autoText, m.title);
    followRgb("badges.skip_fill", t.badges.skipFill, m.selectedFill);
    follow("badges.skip_text", t.badges.skipText, m.text);
}

}  // namespace

std::string uiColorToHex(const UiColor& color) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out = "#";
    const uint8_t channels[4] = {color.r, color.g, color.b, color.a};
    const int count = color.a == 255 ? 3 : 4;
    for (int i = 0; i < count; ++i) {
        out.push_back(kDigits[channels[i] >> 4]);
        out.push_back(kDigits[channels[i] & 0x0F]);
    }
    return out;
}

bool isSafeThemeFontPath(std::string_view path) {
    if (path.empty() || path.size() > kMaxPathBytes) return false;
    for (const char c : path) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    }
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;  // drive letter
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        std::size_t end = normalized.find('/', begin);
        if (end == std::string::npos) end = normalized.size();
        if (normalized.compare(begin, end - begin, "..") == 0) return false;
        begin = end + 1;
    }
    std::string lowered = normalized;
    for (char& c : lowered) c = lowerAscii(c);
    const auto endsWith = [&](const char* suffix) {
        const std::string tail(suffix);
        return lowered.size() > tail.size() &&
               lowered.compare(lowered.size() - tail.size(), tail.size(), tail) == 0;
    };
    return endsWith(".ttf") || endsWith(".otf");
}


const UiSkin* UiSkinSet::find(UiSkinState state) const {
    const auto at = [&](UiSkinState s) -> const UiSkin* {
        const auto& slot = variant[static_cast<std::size_t>(s)];
        return slot ? &*slot : nullptr;
    };
    switch (state) {
        case UiSkinState::Normal: return nullptr;
        case UiSkinState::Selected:
            if (const UiSkin* v = at(UiSkinState::Selected)) return v;
            return at(UiSkinState::Focused);
        case UiSkinState::Focused:
            if (const UiSkin* v = at(UiSkinState::Focused)) return v;
            if (const UiSkin* v = at(UiSkinState::Hover)) return v;
            return at(UiSkinState::Selected);
        case UiSkinState::Hover:
            if (const UiSkin* v = at(UiSkinState::Hover)) return v;
            if (const UiSkin* v = at(UiSkinState::Focused)) return v;
            return at(UiSkinState::Selected);
        default: return at(state);
    }
}

const UiSkin& UiSkinSet::pick(UiSkinState state) const {
    const UiSkin* v = find(state);
    return v != nullptr ? *v : normal;
}

bool UiSkinSet::anyImage() const {
    if (!normal.image.empty()) return true;
    for (const auto& v : variant) {
        if (v && !v->image.empty()) return true;
    }
    return false;
}

int uiScreenIndex(std::string_view name) {
    for (std::size_t i = 0; i < kUiScreenCount; ++i) {
        if (name == kUiScreenNames[i]) return static_cast<int>(i);
    }
    return -1;
}

namespace {
bool hasExtension(std::string_view path, std::initializer_list<const char*> extensions) {
    std::string lowered(path);
    for (char& c : lowered) c = lowerAscii(c);
    for (const char* ext : extensions) {
        const std::string tail(ext);
        if (lowered.size() > tail.size() && lowered.compare(lowered.size() - tail.size(), tail.size(), tail) == 0) {
            return true;
        }
    }
    return false;
}
bool isSafeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > kMaxPathBytes) return false;
    for (const char c : path) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    }
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        std::size_t end = normalized.find('/', begin);
        if (end == std::string::npos) end = normalized.size();
        if (normalized.compare(begin, end - begin, "..") == 0) return false;
        begin = end + 1;
    }
    return true;
}
}  // namespace

bool isSafeThemeImagePath(std::string_view path) {
    return isSafeRelativePath(path) && hasExtension(path, {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"});
}

bool isSafeThemeAudioPath(std::string_view path) {
    return isSafeRelativePath(path) && hasExtension(path, {".ogg", ".wav", ".mp3", ".flac"});
}

std::vector<std::pair<std::string, std::string>> uiThemeImagePaths(const UiTheme& theme) {
    UiTheme copy = theme;
    std::vector<std::pair<std::string, std::string>> out;
    SkinWalker walker([&](const std::string& label, UiSkin& skin, UiSkinSet*) {
        if (!skin.image.empty()) out.emplace_back(label + ".image", skin.image);
    });
    visitTheme(copy, walker, true);
    if (!copy.cursor.image.empty()) out.emplace_back("cursor.image", copy.cursor.image);
    // The same image named twice is probed once.
    std::vector<std::pair<std::string, std::string>> unique;
    for (auto& entry : out) {
        bool seen = false;
        for (const auto& known : unique) seen = seen || known.second == entry.second;
        if (!seen) unique.push_back(std::move(entry));
    }
    return unique;
}

bool uiThemeHasSkins(const UiTheme& theme) {
    UiTheme copy = theme;
    bool any = false;
    SkinWalker walker([&](const std::string&, UiSkin& skin, UiSkinSet* set) {
        if (!skin.image.empty() || (set != nullptr && set->none)) any = true;
    });
    visitTheme(copy, walker, true);
    return any;
}

UiTheme uiThemeWithoutSkins(const UiTheme& theme) {
    UiTheme copy = theme;
    SkinWalker walker([&](const std::string&, UiSkin& skin, UiSkinSet* set) {
        skin.image.clear();
        if (set != nullptr) set->none = false;
    });
    visitTheme(copy, walker, true);
    return copy;
}

std::string uiThemeToJson(const UiTheme& theme) {
    UiTheme copy = theme;
    Writer writer;
    writer.root["format"] = kUiThemeFormat;
    visitTheme(copy, writer, true);
    return writer.root.dump();
}

bool UiTheme::operator==(const UiTheme& other) const {
    return uiThemeToJson(*this) == uiThemeToJson(other);
}

UiThemeLoad parseUiTheme(std::string_view json) {
    UiThemeLoad load;
    load.fileFound = true;
    const auto fail = [&](const std::string& message) {
        load.diagnostics.push_back("ui_theme.json: " + message);
        return load;
    };
    if (json.size() > kUiThemeMaxFileBytes) {
        return fail("larger than " + std::to_string(kUiThemeMaxFileBytes) + " bytes; ignored");
    }
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return fail("not a valid JSON object; ignored");
    }
    const auto formatIt = root.find("format");
    if (formatIt == root.end() || !formatIt->is_number_integer() ||
        formatIt->get<long long>() != kUiThemeFormat) {
        return fail("must declare \"format\": " + std::to_string(kUiThemeFormat) + "; ignored");
    }
    load.usable = true;

    std::set<std::string> named;
    {
        Reader reader{root, load.diagnostics, named, "", {}};
        visitTheme(load.theme, reader, true);
        applyInheritance(load.theme, named);
    }

    const auto hcIt = root.find("high_contrast");
    if (hcIt != root.end()) {
        if (!hcIt->is_object()) {
            if (load.diagnostics.size() < kUiThemeMaxDiagnostics) {
                load.diagnostics.push_back("ui_theme.json: 'high_contrast' is not an object; ignored");
            }
        } else {
            UiTheme hc = load.theme;
            std::set<std::string> hcNamed = named;
            Reader reader{*hcIt, load.diagnostics, hcNamed, "high_contrast.", {}};
            visitTheme(hc, reader, false);
            applyInheritance(hc, hcNamed);
            // T2: image skins are decoration; a High Contrast player gets the plain
            // palette unless the author insists (`"keep_skins": true`).
            bool keepSkins = false;
            if (const auto keep = hcIt->find("keep_skins"); keep != hcIt->end() && keep->is_boolean()) {
                keepSkins = keep->get<bool>();
            }
            if (!keepSkins) hc = uiThemeWithoutSkins(hc);
            load.highContrast = std::move(hc);
        }
    }
    if (!load.highContrast && uiThemeHasSkins(load.theme)) {
        load.highContrast = uiThemeWithoutSkins(load.theme);  // no "high_contrast" key: skins still step aside
    }
    return load;
}

}  // namespace Rowl::State
