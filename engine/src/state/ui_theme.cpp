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
#include <cmath>
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

    auto& tag = t.story.speakerTag;
    v.color("story.speaker_tag", "text_color", tag.textColor);
    v.color("story.speaker_tag", "border_color", tag.borderColor);
    v.num("story.speaker_tag", "border_width", tag.borderWidth, 0.0f, 8.0f);

    auto& f = t.story.choiceFocus;
    v.optColor("story.choice_focus", "inner_color", f.innerColor);
    v.optColor("story.choice_focus", "bar_color", f.barColor);
    v.num("story.choice_focus", "bar_width", f.barWidth, 0.0f, 64.0f);
    v.num("story.choice_focus", "inset", f.inset, 0.0f, 32.0f);

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
            load.highContrast = std::move(hc);
        }
    }
    return load;
}

}  // namespace Rowl::State
