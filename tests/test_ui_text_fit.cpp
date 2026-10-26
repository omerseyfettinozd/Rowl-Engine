/**
 * test_ui_text_fit.cpp — D-K3 (2, 3): konusmaci etiketi ve secenek dugmesi
 * yazi boyutuyla (textScale) birlikte dogru yerlesir.
 *
 * Gercek pencere bulgulari (1896x1016, GPU yolu): 100% yazida "Madam Eleni" ->
 * "Madam", 150%'de "Anlatici" -> "Anlatic" (etiket genisligi ile cizim farkli
 * olcumden geliyor, ikinci kelime alt satira dusup gorunmuyordu); 150%'de secenek
 * metni dugmeden tasiyor / kesiliyordu.
 *
 * Kilit:
 *  1. Saf kararlar (ui_text_fit.hpp): etiket genisligi siniri, "…" kisaltmasi
 *     (kod noktasi sinirinda, sigan en uzun on ek, sarma degil), secenek
 *     kutularinin alt kenar sabit YUKARI buyumesi + ustteki komsularin itilmesi
 *     (yazarin araligi korunur, yazarin ust uste koydugu kutular ayrilmaz,
 *     sigmayan yoksa girdi aynen), metin ustu formulu (%100 tek satir eski).
 *  2. Ekran disi karede (1280x720 ve 1920x1080; %100 ve %150) iki kelimelik
 *     konusmaci adi tek satirda tam cizilir: iki kelimenin murekkebi tek tek
 *     cizilenlerin toplamina esit; cok uzun ad kutu sinirini asmaz ve tek satir
 *     kalir.
 *  3. Ekran disi karede %150'de uc uzun secenek: yerlesim ust uste binmez, alt
 *     kenar sabit (diyalog kutusuna yaklasmaz), ikinci satir dahil tum metin
 *     kendi dugmesinde cizilir (referans: devasa dugmedeki murekkep), dugme
 *     disinda yazi murekkebi yok, tiklama yeni geometrinin her dugmesinin
 *     ortasinda o secenegi secer. %100'de yerlesim yazarin verisine esit.
 *
 * Bilerek-boz: window.cpp'yi D-K3 oncesine (etiket renderText + sabit dugme
 * yukseklik/ust) geri almak 2 ve 3'u kirmizi yapar; fitChoiceBoxes'in asagi
 * buyumesi / itmeyi atlamasi 1'i kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/render/ui_text_fit.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace Rowl::Render;

constexpr const char* kSection = "ui-text-fit";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

// UTF-8 sanity: every lead byte is followed by its continuation bytes.
bool validUtf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += n;
    }
    return true;
}

void pureDecisions() {
    // speakerTagWidth: text + 32 padding, floor 120, cap 80% of the box.
    req(near(speakerTagWidth(100.0f, 1760.0f, 1.0f), 132.0f), "tag width = text + 32");
    req(near(speakerTagWidth(10.0f, 1760.0f, 1.0f), 120.0f), "tag width floor 120");
    req(near(speakerTagWidth(5000.0f, 1000.0f, 1.0f), 800.0f), "tag width capped at 80% of the box");
    req(near(speakerTagWidth(5000.0f, 100.0f, 2.0f), 240.0f), "narrow box: the floor wins (no clamp UB)");
    req(near(speakerTagWidth(20.0f, 1760.0f, 0.5f), 60.0f), "floor scales: 120 * 0.5");

    // ellipsizeToWidth with a fixed-advance measure (10 per code point).
    const auto measure = [](const std::string& s) {
        float n = 0.0f;
        for (unsigned char c : s) if ((c & 0xC0) != 0x80) n += 10.0f;
        return n;
    };
    req(ellipsizeToWidth("Anlatıcı", 80.0f, measure) == "Anlatıcı", "a fitting name is returned unchanged");
    const std::string cut = ellipsizeToWidth("Madam Eleni Hanım", 100.0f, measure);
    req(cut == "Madam Ele…", "longest prefix that fits with the ellipsis: '" + cut + "'");
    req(measure(cut) <= 100.0f, "ellipsized text fits");
    const std::string trimmed = ellipsizeToWidth("Madam Eleni", 70.0f, measure);
    req(trimmed == "Madam…", "a trailing space never sits before the ellipsis: '" + trimmed + "'");
    const std::string multi = ellipsizeToWidth("çğıöşüÇĞİÖŞÜ", 55.0f, measure);
    req(validUtf8(multi) && multi == "çğıö…", "multi-byte code points are never split: '" + multi + "'");
    req(ellipsizeToWidth("anything", 5.0f, measure) == "…", "nothing but the ellipsis fits: it still returns it");
    req(ellipsizeToWidth("x", 0.0f, measure).empty(), "no room: empty");
    TEST_PASS("Speaker tag width + ellipsis decisions");

    // fitChoiceBoxes: three stacked buttons (the shipped layout), all too small.
    {
        std::vector<ChoiceBox> boxes = {
            {1180, 544, 620, 70, true, 100},
            {1180, 632, 620, 70, true, 100},
            {1180, 720, 620, 70, true, 100},
        };
        fitChoiceBoxes(boxes);
        req(near(boxes[2].y + boxes[2].height, 790.0f) && near(boxes[2].height, 100.0f),
            "the bottom button keeps its bottom edge (never nearer the dialogue box)");
        req(near(boxes[1].height, 100.0f) && near(boxes[0].height, 100.0f), "every button holds its text");
        req(near(boxes[1].y + boxes[1].height, boxes[2].y - 18.0f), "authored 18 px gap kept (middle)");
        req(near(boxes[0].y + boxes[0].height, boxes[1].y - 18.0f), "authored 18 px gap kept (top)");
        for (std::size_t i = 1; i < boxes.size(); ++i) {
            req(boxes[i - 1].y + boxes[i - 1].height <= boxes[i].y, "buttons never overlap");
        }
    }
    // Nothing needs more room: the input stays byte-identical.
    {
        const std::vector<ChoiceBox> original = {
            {780, 544, 620, 70, true, 0}, {780, 632, 620, 70, true, 60}, {780, 720, 620, 70, true, 70}};
        auto boxes = original;
        fitChoiceBoxes(boxes);
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            req(boxes[i].y == original[i].y && boxes[i].height == original[i].height,
                "buttons that already fit are untouched");
        }
    }
    // Authored overlap (two mutually exclusive buttons in the same spot) stays; a
    // hidden box and a box in another column do not push anything.
    {
        std::vector<ChoiceBox> boxes = {
            {1180, 720, 620, 70, true, 100},   // grows upward
            {1180, 720, 620, 70, true, 0},     // authored twin: same spot, not pushed
            {1180, 632, 620, 70, false, 400},  // hidden: ignored entirely
            {100, 640, 400, 70, true, 0},      // other column: independent
        };
        fitChoiceBoxes(boxes);
        req(near(boxes[0].y, 690.0f) && near(boxes[0].height, 100.0f), "grown twin extends upward");
        req(near(boxes[1].y, 720.0f) && near(boxes[1].height, 70.0f), "authored twin is not separated");
        req(near(boxes[2].y, 632.0f) && near(boxes[2].height, 70.0f), "hidden box untouched");
        req(near(boxes[3].y, 640.0f), "other column untouched");
    }
    // A grown button pushes only the ones above it, and only as far as needed.
    {
        std::vector<ChoiceBox> boxes = {
            {780, 300, 600, 70, true, 0}, {780, 500, 600, 70, true, 0}, {780, 600, 600, 70, true, 120}};
        fitChoiceBoxes(boxes);
        req(near(boxes[2].y, 550.0f) && near(boxes[2].height, 120.0f), "grown from its bottom edge");
        req(near(boxes[1].y + boxes[1].height, 550.0f - 24.0f),
            "pushed up, keeping the authored gap (at most 24 px)");
        req(near(boxes[0].y, 300.0f), "a far button above is left alone");
    }
    // Text top: %100 single line keeps the legacy formula; scaled / multi-line centres the block.
    req(near(choiceTextTop(100.0f, 70.0f, 22.0f, 25.3f, 1, 1.0f), 100.0f + (70.0f - 22.0f) * 0.5f),
        "100% single line: legacy text top");
    req(near(choiceTextTop(100.0f, 100.0f, 33.0f, 76.0f, 2, 1.5f), 100.0f + (100.0f - 76.0f) * 0.5f),
        "scaled two lines: block centred");
    TEST_PASS("Choice button fitting decisions (grow upward, push neighbours, leave the rest)");
}

// ── offscreen frames ───────────────────────────────────────────────────────

struct Frame {
    uint32_t w = 0, h = 0;
    std::string px;  // RGBA bytes as handed out by the C API
    const unsigned char* at(int x, int y) const {
        return reinterpret_cast<const unsigned char*>(px.data()) + (size_t(y) * w + size_t(x)) * 4;
    }
};

Frame grab(RowlEngineHandle h, uint32_t w, uint32_t hgt) {
    for (int i = 0; i < 4; ++i) RowlEngine_Step(h, 0.016f);
    uint32_t ow = 0, oh = 0;
    const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &ow, &oh);
    req(pixels != nullptr && ow == w && oh == hgt, "offscreen pixel buffer");
    Frame f;
    f.w = ow;
    f.h = oh;
    f.px.assign(reinterpret_cast<const char*>(pixels), size_t(ow) * oh * 4);
    return f;
}

struct Scene {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    ~Scene() {
        if (h) {
            RowlEngine_Shutdown(h);
            RowlEngine_Destroy(h);
        }
    }
    void open(const fs::path& graph, uint32_t w, uint32_t hgt, float textScale) {
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, w, hgt, 0) == 1, "engine init failed");
        RowlEngine_LoadStoryGraph(h, graph.string().c_str());
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        RowlEngine_SetTextScale(h, textScale);
        RowlEngine_SetPlayState(h, 1);
        for (int i = 0; i < 12; ++i) RowlEngine_Step(h, 0.25f);
    }
};

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    req(static_cast<bool>(out), "could not stage " + path.string());
    out << content;
}

std::string speakerGraph(const std::string& speaker) {
    return std::string(R"json({"format_version":4,"start_node_id":1,"nodes":[
      {"id":1,"speaker":")json") + speaker + R"json(","dialogue":" ","next_nodes":[]}]})json";
}

struct Ink {
    long count = 0;
    long spill = 0;  // white ink right of the tag
    int minX = 1 << 30, maxX = -1, minY = 1 << 30, maxY = -1;
};

bool isTagFill(const unsigned char* p) {
    return std::abs(int(p[0]) - 56) <= 3 && std::abs(int(p[1]) - 189) <= 3 && std::abs(int(p[2]) - 248) <= 3;
}

// White (>= 240 on every channel) text ink INSIDE the speaker tag (the tag border is
// white too, so the tag's fill colour defines the interior) and white ink spilling
// out of its right edge, in the band above / just inside the dialogue box.
Ink speakerInk(const Frame& f, float scale) {
    Ink ink;
    const int boxX = static_cast<int>(80.0f * scale);
    const int x1 = static_cast<int>((80.0f + 1760.0f) * scale);
    const int y0 = static_cast<int>((860.0f - 70.0f) * scale);
    const int y1 = static_cast<int>(860.0f * scale + 20.0f * scale);
    int fx0 = 1 << 30, fx1 = -1, fy0 = 1 << 30, fy1 = -1;
    for (int y = y0; y < y1; ++y) {
        for (int x = boxX; x < x1; ++x) {
            if (!isTagFill(f.at(x, y))) continue;
            fx0 = std::min(fx0, x);
            fx1 = std::max(fx1, x);
            fy0 = std::min(fy0, y);
            fy1 = std::max(fy1, y);
        }
    }
    req(fx1 > fx0 && fy1 > fy0, "speaker tag fill not found");
    for (int y = y0; y < y1; ++y) {
        for (int x = boxX; x < x1; ++x) {
            const unsigned char* p = f.at(x, y);
            if (!(p[0] >= 240 && p[1] >= 240 && p[2] >= 240)) continue;
            const bool inside = x >= fx0 && x <= fx1 && y >= fy0 && y <= fy1;
            if (inside) {
                ++ink.count;
                ink.minX = std::min(ink.minX, x);
                ink.maxX = std::max(ink.maxX, x);
                ink.minY = std::min(ink.minY, y);
                ink.maxY = std::max(ink.maxY, y);
            } else if (x > fx1 + 3 && y >= fy0 && y <= fy1) {
                ++ink.spill;
            }
        }
    }
    return ink;
}

Ink inkFor(const fs::path& dir, const std::string& name, uint32_t w, uint32_t hgt, float textScale) {
    const fs::path path = dir / ("speaker_" + std::to_string(std::hash<std::string>{}(name)) + ".json");
    writeFile(path, speakerGraph(name));
    Scene scene;
    scene.open(path, w, hgt, textScale);
    return speakerInk(grab(scene.h, w, hgt), static_cast<float>(w) / 1920.0f);
}

void speakerTagFrames(const fs::path& dir) {
    struct Config { uint32_t w, h; float scale; };
    for (const Config cfg : {Config{1280, 720, 1.0f}, Config{1280, 720, 1.5f},
                             Config{1920, 1080, 1.0f}, Config{1920, 1080, 1.5f}}) {
        const std::string where = std::to_string(cfg.w) + "x" + std::to_string(cfg.h) + " @" +
                                  std::to_string(static_cast<int>(cfg.scale * 100)) + "%";
        const Ink both = inkFor(dir, "Madam Eleni", cfg.w, cfg.h, cfg.scale);
        const Ink first = inkFor(dir, "Madam", cfg.w, cfg.h, cfg.scale);
        const Ink second = inkFor(dir, "Eleni", cfg.w, cfg.h, cfg.scale);
        req(first.count > 50 && second.count > 50, where + ": reference names are drawn");
        req(both.spill == 0 && first.spill == 0 && second.spill == 0, where + ": name ink spills out of the tag");
        req(both.count >= static_cast<long>(0.93 * double(first.count + second.count)),
            where + ": 'Madam Eleni' lost a word (ink " + std::to_string(both.count) + " vs " +
                std::to_string(first.count) + " + " + std::to_string(second.count) + ")");
        req(both.maxX - both.minX > first.maxX - first.minX + (second.maxX - second.minX) / 2,
            where + ": the second word is not beside the first");
        // Single line: the vertical extent of the ink stays within one line of text.
        const float scale = static_cast<float>(cfg.w) / 1920.0f;
        const int lineMax = static_cast<int>(20.0f * cfg.scale * 1.4f * scale);
        req(both.maxY - both.minY <= lineMax, where + ": the name wrapped onto a second line");

        // A name far wider than the box is shortened with an ellipsis, never wrapped
        // and never wider than the tag cap (80% of the dialogue box).
        const Ink longName = inkFor(
            dir, "Çok Uzun Bir Konuşmacı Adı Gerçekten Çok Uzun Olduğunda Kutuyu Aşar Ve Kısalır Mutlaka",
            cfg.w, cfg.h, cfg.scale);
        req(longName.count > 50, where + ": long name is drawn");
        req(longName.spill == 0, where + ": the long name spills out of the tag");
        req(longName.maxY - longName.minY <= lineMax, where + ": the long name wrapped");
        const int capRight = static_cast<int>((80.0f + 20.0f + 1760.0f * 0.8f) * scale);
        req(longName.maxX < capRight, where + ": the long name overruns the tag cap");
    }
    TEST_PASS("Offscreen speaker tag: two-word and very long names stay on one line (100% / 150%)");
}

// ── choice buttons at 150% ─────────────────────────────────────────────────

struct Opt {
    int text;
    float y;
    float height;
};

std::string choiceGraph(const std::vector<Opt>& opts) {
    const char* texts[] = {
        "Sizi suçlamıyorum, yalnızca bilgi vermenizi rica ediyorum",
        "Vicdanınıza seslenip sorumluluğunuzdan söz etmek istiyorum",
        "Hiçbir şey söylemeden onun yüzüne uzun uzun bakıyorum"};
    std::string options;
    std::string edges;
    for (std::size_t i = 0; i < opts.size(); ++i) {
        const std::string id = "o" + std::to_string(i);
        if (i) {
            options += ",";
            edges += ",";
        }
        options += R"({"option_id":")" + id + R"(","text":")" + texts[opts[i].text] +
                   R"(","text_color":"#FFFF00","background_color":"#000080",)"
                   R"("border_color":"#00FF00","hover_color":"#000080","x":1180,"y":)" +
                   std::to_string(opts[i].y) + R"(,"width":620,"height":)" +
                   std::to_string(opts[i].height) + "}";
        edges += R"({"id":)" + std::to_string(i + 2) + R"(,"label":"x","option_id":")" + id + R"("})";
    }
    std::string nodes = R"json({"format_version":4,"start_node_id":1,"nodes":[
      {"id":1,"speaker":"Selim","dialogue":"Ne diyeceksin?",
       "objects":[{"id":"c","name":"C","is_active":true,"components":[
         {"type":"choice","id":"cc","enabled":true,"data":{"options":[)json" + options + R"json(]}}]}],
       "next_nodes":[)json" + edges + "]}";
    for (std::size_t i = 0; i < opts.size(); ++i) {
        nodes += R"(,{"id":)" + std::to_string(i + 2) + R"(,"speaker":"Selim","dialogue":"Devam."})";
    }
    return nodes + "]}";
}

// The shipped layout: three 70 px buttons 88 px apart, bottom edge at 790.
const std::vector<Opt> kShipped = {{0, 544, 70}, {1, 632, 70}, {2, 720, 70}};

// Yellow (R,G high, B low) ink strictly inside / outside the rect.
long yellowIn(const Frame& f, float x0, float y0, float x1, float y1) {
    long n = 0;
    for (int y = std::max(0, int(y0)); y < std::min<int>(int(y1), int(f.h)); ++y) {
        for (int x = std::max(0, int(x0)); x < std::min<int>(int(x1), int(f.w)); ++x) {
            const unsigned char* p = f.at(x, y);
            if (p[0] >= 200 && p[1] >= 200 && p[2] <= 80) ++n;
        }
    }
    return n;
}

void choiceFrames(const fs::path& dir) {
    struct Config { uint32_t w, h; };
    for (const Config cfg : {Config{1280, 720}, Config{1920, 1080}}) {
        const float s = static_cast<float>(cfg.w) / 1920.0f;
        const std::string where = std::to_string(cfg.w) + "x" + std::to_string(cfg.h);

        // %100: today's geometry, byte for byte.
        {
            const fs::path path = dir / "choices100.json";
            writeFile(path, choiceGraph(kShipped));
            Scene scene;
            scene.open(path, cfg.w, cfg.h, 1.0f);
            const auto buttons = scene.engine->getLaidOutChoiceButtons();
            const auto& authored = scene.engine->getActiveChoiceButtons();
            req(buttons.size() == 3 && authored.size() == 3, where + ": three buttons");
            for (std::size_t i = 0; i < 3; ++i) {
                req(buttons[i].y == authored[i].y && buttons[i].height == authored[i].height &&
                        buttons[i].x == authored[i].x && buttons[i].width == authored[i].width,
                    where + ": 100% geometry must equal the authored one");
            }
        }

        // %150: three two-line options.
        const fs::path path = dir / "choices150.json";
        writeFile(path, choiceGraph(kShipped));
        Scene scene;
        scene.open(path, cfg.w, cfg.h, 1.5f);
        const auto buttons = scene.engine->getLaidOutChoiceButtons();
        req(buttons.size() == 3, where + ": three buttons");
        bool grew = false;
        for (std::size_t i = 0; i < 3; ++i) grew = grew || buttons[i].height > 70.5f;
        req(grew, where + ": at 150% a long option must get a taller button");
        for (std::size_t i = 1; i < 3; ++i) {
            req(buttons[i - 1].y + buttons[i - 1].height <= buttons[i].y + 0.01f,
                where + ": options overlap at 150%");
        }
        req(near(buttons[2].y + buttons[2].height, 790.0f, 0.5f),
            where + ": the last button keeps its bottom edge (dialogue box clearance)");

        const Frame frame = grab(scene.h, cfg.w, cfg.h);
        // Reference: each text alone in a button too tall to ever clip, same scale.
        for (std::size_t i = 0; i < 3; ++i) {
            const fs::path refPath = dir / ("ref" + std::to_string(i) + ".json");
            writeFile(refPath, choiceGraph({{static_cast<int>(i), 100.0f, 400.0f}}));
            Scene ref;
            ref.open(refPath, cfg.w, cfg.h, 1.5f);
            const auto refButtons = ref.engine->getLaidOutChoiceButtons();
            const Frame refFrame = grab(ref.h, cfg.w, cfg.h);
            const auto& b = buttons[i];
            const auto& r = refButtons[0];
            const long inkFit = yellowIn(frame, (b.x + 14) * s, (b.y + 4) * s, (b.x + b.width - 14) * s,
                                         (b.y + b.height - 4) * s);
            const long inkRef = yellowIn(refFrame, (r.x + 14) * s, (r.y + 4) * s, (r.x + r.width - 14) * s,
                                         (r.y + r.height - 4) * s);
            req(inkRef > 200, where + ": reference option " + std::to_string(i) + " is drawn");
            req(inkFit >= static_cast<long>(0.97 * double(inkRef)),
                where + ": option " + std::to_string(i) + " lost text at 150% (ink " +
                    std::to_string(inkFit) + " of " + std::to_string(inkRef) + ")");
        }
        // No text ink outside the buttons: split the frame into the three rects + the rest.
        long inside = 0;
        for (const auto& b : buttons) {
            inside += yellowIn(frame, (b.x - 2) * s, (b.y - 2) * s, (b.x + b.width + 2) * s, (b.y + b.height + 2) * s);
        }
        const long total = yellowIn(frame, 0, 0, float(cfg.w), float(cfg.h));
        req(total == inside, where + ": option text ink outside its button (" + std::to_string(total - inside) + " px)");

        // Hit-test follows the drawn geometry: the centre of every laid-out button picks
        // exactly that option, also the part that exists only because the button grew.
        for (std::size_t i = 0; i < 3; ++i) {
            Scene click;
            click.open(path, cfg.w, cfg.h, 1.5f);
            const auto laid = click.engine->getLaidOutChoiceButtons();
            const float cx = (laid[i].x + laid[i].width * 0.5f) * s;
            const float cy = (laid[i].y + laid[i].height * 0.5f) * s;
            RowlEngine_PointerDown(click.h, cx, cy);
            req(RowlEngine_GetCurrentNodeId(click.h) == i + 2,
                where + ": clicking the centre of the drawn option " + std::to_string(i) + " picked node " +
                    std::to_string(RowlEngine_GetCurrentNodeId(click.h)));
        }
        // Grown-only area: the top strip of the first (grown) button was empty canvas before.
        {
            Scene click;
            click.open(path, cfg.w, cfg.h, 1.5f);
            const auto laid = click.engine->getLaidOutChoiceButtons();
            const auto& authored = click.engine->getActiveChoiceButtons();
            if (laid[0].y < authored[0].y - 1.0f) {
                RowlEngine_PointerDown(click.h, (laid[0].x + 10) * s, (laid[0].y + 2.0f) * s);
                req(RowlEngine_GetCurrentNodeId(click.h) == 2, where + ": the grown strip is part of the button");
            }
        }
    }
    TEST_PASS("Offscreen choice buttons at 150%: fitted, stacked upward, nothing clipped, hit-test matches");
}

}  // namespace

void test_ui_text_fit() {
    TEST_SECTION("UI text fit (speaker tag + choice buttons at larger text sizes)");
    const fs::path dir = fs::temp_directory_path() / "rowl_ui_text_fit";
    std::error_code ec;
    fs::create_directories(dir, ec);
    pureDecisions();
    speakerTagFrames(dir);
    choiceFrames(dir);
    fs::remove_all(dir, ec);
}
