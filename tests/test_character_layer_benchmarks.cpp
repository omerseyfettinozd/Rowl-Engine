/**
 * test_character_layer_benchmarks.cpp — Faz 5 Dilim 6 kalici katman benchmark'i
 * + opaklik mikro-fix testleri.
 *
 * Kapsam:
 *  a) Atlas guard benchmark: urun-tavani konfigurasyon (4 karakter x 4 slot =
 *     16 draw) steady render + 4/40/80-draw stres noktalari (kayit) + soguk
 *     first-frame + texture_load_ms. Esik: 16-draw steady <= 8 ms
 *     (ROWL_PERF_FLOOR=report modunda raporlanir, zorlanmaz; fail kapisi
 *     Linux-only — Faz 4.5 D1).
 *  b) Opaklik tasiyici: layers opacity -> CharacterRenderData.opacity +
 *     offscreen piksel probu (fail-closed: opaklik yoksa eski davranis).
 *
 * Kritik tuzak: window.cpp identical-frame fast-path ayni icerigi bedavaya
 * gecirir; benchmark her frame x'e jitter katar (cache-busting), yoksa 0 ms
 * olcer. Doku paylasimlidir (2 benzersiz doku); stres noktalari draw-bound
 * maliyeti olcer, bellek-bound degil.
 */
#include "rowl_test_harness.hpp"
#include "rowl/scene/character_layers.hpp"

#include <nlohmann/json.hpp>

namespace {

void failLayerBench(const std::string& message) {
    std::cerr << "character_layer_benchmarks FAILED: " << message << std::endl;
    exit(1);
}

void checkLayerBench(bool condition, const std::string& message) {
    if (!condition) failLayerBench(message);
}

// Urun-tavani sahnesi: characterCount karakter x 4 slot. jitterX her frame
// degisir (fast-path cache-busting). Dokular repo Assets'indendir.
std::string buildLayerSceneJson(int characterCount, float jitterX) {
    nlohmann::json items = nlohmann::json::array();
    for (int index = 0; index < characterCount; ++index) {
        const float x = 40.0f + static_cast<float>(index) * 88.0f +
                        (index == 0 ? jitterX : 0.0f);
        nlohmann::json layers;
        layers["body"] = "Margot.jpg";
        layers["face"] = "3a72957d667c9f393097f09b90a4f59f.jpg";
        layers["outfit"] = "Margot.jpg";
        layers["accessory"] = "3a72957d667c9f393097f09b90a4f59f.jpg";
        nlohmann::json data;
        data["sprite"] = "Margot.jpg";
        data["x"] = x;
        data["y"] = 340.0f;
        data["width"] = 180.0f;
        data["height"] = 270.0f;
        data["layers"] = layers;
        nlohmann::json item;
        item["type"] = "character";
        item["id"] = "bench_c" + std::to_string(index);
        item["enabled"] = true;
        item["data"] = data;
        items.push_back(item);
    }
    return items.dump();
}

// Tek konfigurasyonun steady ortalamasi (60 x sahne-guncelle + Step).
double measureSteadyFrameMs(RowlEngineHandle handle, int characterCount) {
    constexpr int kIterations = 60;
    auto start = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < kIterations; ++frame) {
        const float jitter = (frame % 2 == 0) ? 0.0f : 0.75f;
        const std::string scene = buildLayerSceneJson(characterCount, jitter);
        RowlEngine_UpdateSceneFromJson(handle, scene.c_str());
        RowlEngine_Step(handle, 0.01667f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() /
           static_cast<double>(kIterations);
}

void maybeWriteLayerBenchmarkJson(double firstFrameMs, double textureLoadMs,
                                   double draws4Ms, double draws16Ms,
                                   double draws40Ms, double draws80Ms) {
    const std::string outputPath =
        environmentValue("ROWL_LAYER_BENCHMARK_JSON", "");
    if (outputPath.empty()) return;
    const std::filesystem::path output(outputPath);
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    std::ofstream stream(output, std::ios::trunc);
    if (!stream.is_open()) {
        failLayerBench("could not write layer benchmark JSON: " + outputPath);
    }
    stream << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"fixture_id\": \"rowl-layer-atlas-guard-v1\",\n"
           << "  \"metrics\": {\n"
           << "    \"first_frame_ms\": " << firstFrameMs << ",\n"
           << "    \"texture_load_ms\": " << textureLoadMs << ",\n"
           << "    \"steady_4_draws_ms\": " << draws4Ms << ",\n"
           << "    \"steady_16_draws_ms\": " << draws16Ms << ",\n"
           << "    \"steady_40_draws_ms\": " << draws40Ms << ",\n"
           << "    \"steady_80_draws_ms\": " << draws80Ms << ",\n"
           << "    \"assert_threshold_16_draws_ms\": 8.0\n"
           << "  }\n"
           << "}\n";
    std::cout << "  ⚡ [BENCHMARK] Layer JSON report: " << outputPath << std::endl;
}

void testLayerAtlasBenchmark() {
    TEST_SECTION("Character Layer Atlas Guard Benchmark");

    // Soguk first-frame: taze motorda 16-draw sahnesi (doku cozumu dahil).
    double firstFrameMs = 0.0;
    double textureLoadMs = 0.0;
    {
        RowlEngineHandle cold = RowlEngine_Create();
        checkLayerBench(cold != nullptr, "cold engine create failed");
        checkLayerBench(RowlEngine_Init(cold, 1920, 1080, 0) == 1,
                        "cold engine init failed");
        const std::string scene = buildLayerSceneJson(4, 0.0f);
        RowlEngine_UpdateSceneFromJson(cold, scene.c_str());
        auto start = std::chrono::high_resolution_clock::now();
        RowlEngine_Step(cold, 0.0f);
        auto end = std::chrono::high_resolution_clock::now();
        firstFrameMs =
            std::chrono::duration<double, std::milli>(end - start).count();
        textureLoadMs = RowlEngine_GetLastFrameTextureLoadMilliseconds(cold);
        RowlEngine_Shutdown(cold);
        RowlEngine_Destroy(cold);
    }
    std::cout << "  ⚡ [BENCHMARK] Layer First Frame (16 draws, cold): "
              << firstFrameMs << "ms (texture load " << textureLoadMs << "ms)"
              << std::endl;
    TEST_PASS("layer first-frame + texture-load profile");

    // Steady konfigurasyonlar (ayni motorda,cache sicak).
    RowlEngineHandle handle = RowlEngine_Create();
    checkLayerBench(handle != nullptr, "bench engine create failed");
    checkLayerBench(RowlEngine_Init(handle, 1920, 1080, 0) == 1,
                    "bench engine init failed");
    const double draws4Ms = measureSteadyFrameMs(handle, 1);
    const double draws16Ms = measureSteadyFrameMs(handle, 4);
    const double draws40Ms = measureSteadyFrameMs(handle, 10);
    const double draws80Ms = measureSteadyFrameMs(handle, 20);
    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);

    std::cout << "  ⚡ [BENCHMARK] Layer Steady (sahne-guncelle + render):\n"
              << "      4 draws:  " << draws4Ms << "ms/frame\n"
              << "     16 draws (urun-tavani): " << draws16Ms << "ms/frame\n"
              << "     40 draws: " << draws40Ms << "ms/frame\n"
              << "     80 draws: " << draws80Ms << "ms/frame" << std::endl;

    // Muhurlu esik: yalnizca urun-tavani (16 draw) assert edilir. Stres
    // noktalari makineye gore degisir, sadece kaydedilir.
    // Tur-13: Faz 4.5 D1 karari — fail kapisi Linux-only. Windows CI
    // ROWL_PERF_FLOOR=report ile kosar (hosted software rasterizer 16-draw
    // tavanini tutamaz: CI-12'de 9.26ms > 8ms). test_camera desenindeki
    // ayni kapi; default enforced oldugu icin Linux CI + yerel davranis
    // birebir korunur.
    constexpr double kProductCeilingThresholdMs = 8.0;
    const bool perfFloorEnforced =
        environmentValue("ROWL_PERF_FLOOR", "enforced") == "enforced";
    if (perfFloorEnforced) {
        checkLayerBench(
            draws16Ms <= kProductCeilingThresholdMs,
            "product-ceiling 16-draw steady " + std::to_string(draws16Ms) +
                "ms exceeds " + std::to_string(kProductCeilingThresholdMs) + "ms");
    } else if (draws16Ms > kProductCeilingThresholdMs) {
        std::cout << "  (ROWL_PERF_FLOOR=report: 16-draw ceiling "
                  << draws16Ms << "ms > "
                  << kProductCeilingThresholdMs
                  << "ms, reported not enforced)" << std::endl;
    }
    TEST_PASS("layer product-ceiling (16 draws) <= 8ms");

    maybeWriteLayerBenchmarkJson(firstFrameMs, textureLoadMs, draws4Ms,
                                 draws16Ms, draws40Ms, draws80Ms);
}

void testLayerOpacityPlumbing() {
    TEST_SECTION("CharacterLayers opacity plumbing to render data");
    RowlEngineHandle handle = RowlEngine_Create();
    checkLayerBench(handle != nullptr, "RowlEngine_Create failed");
    checkLayerBench(RowlEngine_Init(handle, 64, 64, 0) == 1,
                    "plumbing fixture Init failed");
    Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(handle);
    checkLayerBench(engine != nullptr, "test bridge could not resolve engine");

    RowlEngine_UpdateSceneFromJson(
        handle,
        R"json([{"type":"character","enabled":true,"data":{"sprite":"legacy.png","x":100,"y":200,"width":300,"height":400,"layers":{"body":{"asset":"b.png","opacity":0.5},"face":"f.png"}}}])json");
    {
        const auto& chars = engine->getActiveCharacters();
        checkLayerBench(chars.size() == 2, "expected body+face draws");
        checkLayerBench(chars[0].sprite == "b.png" && chars[0].opacity == 0.5f,
                        "body opacity 0.5 did not reach CharacterRenderData");
        checkLayerBench(chars[1].sprite == "f.png" && chars[1].opacity == 1.0f,
                        "face default opacity must be 1.0");
    }

    // Fail-closed: layers yoksa eski davranis (opacity 1.0).
    RowlEngine_UpdateSceneFromJson(
        handle,
        R"json([{"type":"character","enabled":true,"data":{"sprite":"solo.png","x":10,"y":20,"width":30,"height":40}}])json");
    {
        const auto& chars = engine->getActiveCharacters();
        checkLayerBench(chars.size() == 1 && chars[0].opacity == 1.0f,
                        "legacy sprite must keep opacity 1.0");
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("character_layers opacity reaches CharacterRenderData");
}

// 24-bit BMP yazici (SDL'e bagimlilik yok): 64x64 duz beyaz.
bool writeWhiteBmp(const std::filesystem::path& path) {
    constexpr int32_t kSize = 64;
    constexpr int32_t kRowBytes = kSize * 3;
    constexpr int32_t kPixelBytes = kRowBytes * kSize;
    constexpr int32_t kFileBytes = 54 + kPixelBytes;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    const auto put16 = [&out](uint16_t v) {
        out.put(static_cast<char>(v & 0xFF));
        out.put(static_cast<char>((v >> 8) & 0xFF));
    };
    const auto put32 = [&out](uint32_t v) {
        out.put(static_cast<char>(v & 0xFF));
        out.put(static_cast<char>((v >> 8) & 0xFF));
        out.put(static_cast<char>((v >> 16) & 0xFF));
        out.put(static_cast<char>((v >> 24) & 0xFF));
    };
    out.put('B');
    out.put('M');
    put32(static_cast<uint32_t>(kFileBytes));
    put32(0);
    put32(54);
    put32(40);
    put32(static_cast<uint32_t>(kSize));
    put32(static_cast<uint32_t>(kSize));
    put16(1);
    put16(24);
    put32(0);
    put32(static_cast<uint32_t>(kPixelBytes));
    put32(2835);
    put32(2835);
    put32(0);
    put32(0);
    for (int i = 0; i < kPixelBytes; ++i) out.put(static_cast<char>(0xFF));
    return static_cast<bool>(out);
}

uint8_t sampleCenterRed(RowlEngineHandle handle) {
    uint32_t width = 0, height = 0, pitch = 0;
    const uint8_t* pixels =
        RowlEngine_GetPixelBufferEx(handle, &width, &height, &pitch);
    checkLayerBench(pixels != nullptr && width == 1920 && height == 1080 &&
                        pitch >= width * 4u,
                    "no 1920x1080 pixel buffer for opacity probe");
    // Karakter kutusu (928,508,64,64) -> merkez (960,540).
    const uint32_t cx = 960, cy = 540;
    const uint8_t* pixel = pixels + static_cast<size_t>(cy) * pitch +
                           static_cast<size_t>(cx) * 4u;
    return pixel[0];
}

void renderProbeScene(RowlEngineHandle handle, const std::string& layersJson) {
    std::string scene = R"json([{"type":"character","enabled":true,"data":{"sprite":"probe.bmp","x":928,"y":508,"width":64,"height":64)json";
    if (!layersJson.empty()) scene += R"json(,"layers":)json" + layersJson;
    scene += "}}]";
    RowlEngine_UpdateSceneFromJson(handle, scene.c_str());
    RowlEngine_Step(handle, 0.01667f);
}

void testLayerOpacityPixels() {
    TEST_SECTION("CharacterLayers opacity pixel probe");
    const auto probeDir = std::filesystem::temp_directory_path() /
                          ("rowl_opacity_probe_" +
                           std::to_string(std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()));
    std::error_code dirError;
    std::filesystem::create_directories(probeDir / "Assets", dirError);
    checkLayerBench(!dirError, "probe temp dir failed");
    checkLayerBench(writeWhiteBmp(probeDir / "Assets" / "probe.bmp"),
                    "probe BMP failed");

    RowlEngineHandle handle = RowlEngine_Create();
    checkLayerBench(handle != nullptr, "RowlEngine_Create failed");
    checkLayerBench(RowlEngine_Init(handle, 1920, 1080, 0) == 1,
                    "probe engine init failed");
    RowlEngine_SetProjectDirectory(handle, probeDir.string().c_str());

    // Opak (legacy, fail-closed referans) -> ornek A.
    renderProbeScene(handle, "");
    const uint8_t fullA = sampleCenterRed(handle);
    // Yari-seffaf katman -> ornek B (koyu zeminde belirgin koyu).
    renderProbeScene(handle, R"json({"body":{"asset":"probe.bmp","opacity":0.35}})json");
    const uint8_t half = sampleCenterRed(handle);
    // Tekrar opak -> onbellekteki dokunun AlphaMod'u geri alindigini gosterir.
    renderProbeScene(handle, "");
    const uint8_t fullB = sampleCenterRed(handle);

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    std::filesystem::remove_all(probeDir, dirError);

    std::cout << "  ⚡ [PROBE] opacity pixels R: full=" << static_cast<int>(fullA)
              << " half=" << static_cast<int>(half)
              << " full-again=" << static_cast<int>(fullB) << std::endl;
    checkLayerBench(fullA > 200, "opaque probe must stay near-white");
    checkLayerBench(half < 160 && half > 40,
                    "0.35 opacity must visibly blend, not skip or vanish");
    checkLayerBench(fullA == fullB,
                    "shared texture AlphaMod must be restored after draw");
    TEST_PASS("character_layers opacity applies to pixels");
}

} // namespace

void test_character_layer_benchmarks() {
    testLayerAtlasBenchmark();
    testLayerOpacityPlumbing();
    testLayerOpacityPixels();
}
