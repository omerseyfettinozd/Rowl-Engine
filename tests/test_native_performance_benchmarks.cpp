/**
 * test_native_performance_benchmarks.cpp — benchmark metrics, JSON writer,
 * and shared benchmark helpers.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) escaped += "?";
                else escaped += static_cast<char>(character);
        }
    }
    return escaped;
}


uint64_t processMemoryBytes() {
    // Linux exposes resident pages here. Other hosts retain a valid JSON
    // schema with zero when this portable fallback is unavailable.
    std::ifstream statm("/proc/self/statm");
    uint64_t pages = 0;
    uint64_t residentPages = 0;
    if (statm >> pages >> residentPages) {
        return residentPages * 4096ULL;
    }
    return 0;
}

uint64_t benchmarkTextureCacheBudgetBytes() {
    const auto configured = environmentValue("ROWL_BENCHMARK_TEXTURE_CACHE_BYTES", "67108864");
    try {
        size_t parsed = 0;
        const auto value = std::stoull(configured, &parsed);
        return parsed == configured.size() ? value : 64ULL * 1024ULL * 1024ULL;
    } catch (...) {
        return 64ULL * 1024ULL * 1024ULL;
    }
}

// Transition FPS measured by test_camera_and_transition_pipeline(), which runs
// before the benchmark writer. Negative means unmeasured (e.g. reordered runs).
double g_transitionFps = -1.0;

void writeBenchmarkJson(const std::string& outputPath, double startupMs, double vfsElapsedMs,
                        int vfsIterations, double jsonElapsedMs, int jsonIterations, double firstFrameMs,
                        double steadyFrameMs, double textureLoadMs, double nonTextureRenderMs,
                        uint64_t textureCount, uint64_t textureBytes, uint64_t textureBudgetBytes,
                        uint64_t textureEvictionCount) {
    const std::filesystem::path output(outputPath);
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    const std::filesystem::path temporary = output.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream.is_open()) {
        std::cerr << "Could not write benchmark JSON: " << outputPath << std::endl;
        exit(1);
    }
    stream << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"build\": {\"id\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_BUILD_ID", "unknown"))
           << "\", \"type\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_BUILD_TYPE", "unknown")) << "\"},\n"
           << "  \"environment\": {\"os\": \"" << jsonEscape(SDL_GetPlatform())
           << "\", \"cpu_count\": " << SDL_GetNumLogicalCPUCores()
           << ", \"machine\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_MACHINE", "unknown")) << "\"},\n"
           << "  \"fixture_id\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_FIXTURE", "native-default-v1")) << "\",\n"
           << "  \"metrics\": {\n"
           << "    \"startup_ms\": " << startupMs << ",\n"
           << "    \"vfs_io\": {\"iterations\": " << vfsIterations << ", \"total_ms\": " << vfsElapsedMs
           << ", \"avg_ms\": " << vfsElapsedMs / vfsIterations << "},\n"
           << "    \"json_update\": {\"iterations\": " << jsonIterations << ", \"total_ms\": " << jsonElapsedMs
           << ", \"avg_ms\": " << jsonElapsedMs / jsonIterations << "},\n"
           << "    \"first_frame_ms\": " << firstFrameMs << ",\n"
           << "    \"startup_profile\": {\"texture_load_ms\": " << textureLoadMs
           << ", \"non_texture_render_ms\": " << nonTextureRenderMs << "},\n"
           << "    \"steady_frame_ms\": " << steadyFrameMs << ",\n";
    if (g_transitionFps < 0.0) {
        stream << "    \"transition_fps\": null,\n";
    } else {
        stream << "    \"transition_fps\": " << g_transitionFps << ",\n";
    }
    stream << "    \"texture_cache\": {\"texture_count\": " << textureCount << ", \"bytes\": " << textureBytes
           << ", \"budget_bytes\": " << textureBudgetBytes << ", \"eviction_count\": " << textureEvictionCount << "},\n"
           << "    \"process_memory_bytes\": " << processMemoryBytes() << "\n"
           << "  }\n"
           << "}\n";
    stream.close();
    std::error_code replaceError;
    std::filesystem::remove(output, replaceError);
    std::filesystem::rename(temporary, output, replaceError);
    if (replaceError) {
        std::cerr << "Could not publish benchmark JSON: " << replaceError.message() << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] JSON report: " << outputPath << std::endl;
}

void writeGoldenBenchmarkJson(const std::string& outputPath, double startupMs, double projectLoadMs,
                               uint64_t startNodeId, double firstFrameMs, double textureLoadMs,
                               double nonTextureRenderMs, double steadyTotalMs, int steadyIterations,
                               double steadyFrameMs, uint64_t textureCount, uint64_t textureBytes,
                               uint64_t textureBudgetBytes, uint64_t textureEvictionCount) {
    const std::filesystem::path output(outputPath);
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    const std::filesystem::path temporary = output.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream.is_open()) {
        std::cerr << "Could not write golden benchmark JSON: " << outputPath << std::endl;
        exit(1);
    }
    stream << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"build\": {\"id\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_BUILD_ID", "unknown"))
           << "\", \"type\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_BUILD_TYPE", "unknown")) << "\"},\n"
           << "  \"environment\": {\"os\": \"" << jsonEscape(SDL_GetPlatform())
           << "\", \"cpu_count\": " << SDL_GetNumLogicalCPUCores()
           << ", \"machine\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_MACHINE", "unknown")) << "\"},\n"
           << "  \"fixture_id\": \"rowl-golden-project-v1\",\n"
           << "  \"metrics\": {\n"
           << "    \"startup_ms\": " << startupMs << ",\n"
           << "    \"project_load_ms\": " << projectLoadMs << ",\n"
           << "    \"start_node_id\": " << startNodeId << ",\n"
           << "    \"first_frame_ms\": " << firstFrameMs << ",\n"
           << "    \"startup_profile\": {\"texture_load_ms\": " << textureLoadMs
           << ", \"non_texture_render_ms\": " << nonTextureRenderMs << "},\n"
           << "    \"steady_frames\": {\"iterations\": " << steadyIterations << ", \"total_ms\": " << steadyTotalMs
           << "},\n"
           << "    \"steady_frame_ms\": " << steadyFrameMs << ",\n"
           << "    \"texture_cache\": {\"texture_count\": " << textureCount << ", \"bytes\": " << textureBytes
           << ", \"budget_bytes\": " << textureBudgetBytes << ", \"eviction_count\": " << textureEvictionCount << "},\n"
           << "    \"process_memory_bytes\": " << processMemoryBytes() << "\n"
           << "  }\n"
           << "}\n";
    stream.close();
    std::error_code replaceError;
    std::filesystem::remove(output, replaceError);
    std::filesystem::rename(temporary, output, replaceError);
    if (replaceError) {
        std::cerr << "Could not publish golden benchmark JSON: " << replaceError.message() << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Golden JSON report: " << outputPath << std::endl;
}

void test_native_performance_benchmarks(const std::string& benchmarkJsonPath = "") {
    TEST_SECTION("Performance & Profiling Benchmarks");

    // 0. Engine Startup Benchmark (handle creation + initialization).
    auto startupStart = std::chrono::high_resolution_clock::now();
    RowlEngineHandle startupHandle = RowlEngine_Create();
    const int startupInit = startupHandle ? RowlEngine_Init(startupHandle, 1920, 1080, 0) : 0;
    auto startupEnd = std::chrono::high_resolution_clock::now();
    const double startupMs =
        std::chrono::duration<double, std::milli>(startupEnd - startupStart).count();
    if (!startupHandle || startupInit != 1) {
        std::cerr << "Engine startup benchmark could not initialize" << std::endl;
        exit(1);
    }
    RowlEngine_Shutdown(startupHandle);
    RowlEngine_Destroy(startupHandle);
    std::cout << "  ⚡ [BENCHMARK] Engine Startup: " << startupMs << "ms" << std::endl;
    TEST_PASS("Engine Startup Latency Benchmark");

    // 1. VFS Query & Read Latency Benchmark
    Rowl::VFS::VFSManager vfs;
    vfs.remountProject(std::filesystem::current_path().string());
    const int VFS_ITERATIONS = 5000;
    auto vfsStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < VFS_ITERATIONS; ++i) {
        bool exists = vfs.exists("fonts/default.ttf");
        (void)exists;
    }
    auto vfsEnd = std::chrono::high_resolution_clock::now();
    double vfsElapsedMs = std::chrono::duration<double, std::milli>(vfsEnd - vfsStart).count();
    double vfsOpsPerSec = (VFS_ITERATIONS / (vfsElapsedMs / 1000.0));
    std::cout << "  ⚡ [BENCHMARK] VFS Exists Lookups: " << VFS_ITERATIONS << " queries in "
              << vfsElapsedMs << "ms (" << static_cast<uint64_t>(vfsOpsPerSec) << " queries/sec)" << std::endl;
    TEST_PASS("VFS High-Throughput Lookup Benchmark");

    // 2. Scene JSON Parse & Update Benchmark
    RowlEngineHandle handle = RowlEngine_Create();
    RowlEngine_Init(handle, 1920, 1080, 0);
    const auto configuredTextureBudgetBytes = benchmarkTextureCacheBudgetBytes();
    RowlEngine_SetTextureCacheBudgetBytes(handle, configuredTextureBudgetBytes);

    const std::string benchJson = R"([
        {"type":"speaker","id":"s1","enabled":true,"data":{"speaker":"Evelyn","dialogue":"Benchmark line"}},
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080}},
        {"type":"character","id":"c1","enabled":true,"data":{"sprite":"Margot.jpg","x":400,"y":200,"width":360,"height":540}},
        {"type":"dialogue_box","id":"d1","enabled":true,"data":{"x":80,"y":840,"width":1760,"height":200}},
        {"type":"audio","id":"a1","enabled":true,"data":{"dsp_filter":"Normal"}}
    ])";

    const int JSON_ITERATIONS = 500;
    auto jsonStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < JSON_ITERATIONS; ++i) {
        RowlEngine_UpdateSceneFromJson(handle, benchJson.c_str());
    }
    auto jsonEnd = std::chrono::high_resolution_clock::now();
    double jsonElapsedMs = std::chrono::duration<double, std::milli>(jsonEnd - jsonStart).count();
    double jsonOpsPerSec = (JSON_ITERATIONS / (jsonElapsedMs / 1000.0));
    std::cout << "  ⚡ [BENCHMARK] Scene JSON Updates: " << JSON_ITERATIONS << " updates in "
              << jsonElapsedMs << "ms (" << static_cast<uint64_t>(jsonOpsPerSec) << " updates/sec, "
              << (jsonElapsedMs / JSON_ITERATIONS) << "ms/op)" << std::endl;
    TEST_PASS("Scene JSON Ingestion & Entity Synchronization Benchmark");

    // 3. Native Frame Render Step Benchmark. Asset decode/cache population is
    // a startup cost, not a steady-state frame cost, so prime it before
    // timing the normal render loop.
    auto firstFrameStart = std::chrono::high_resolution_clock::now();
    RowlEngine_Step(handle, 0.0f);
    auto firstFrameEnd = std::chrono::high_resolution_clock::now();
    const double firstFrameMs = std::chrono::duration<double, std::milli>(firstFrameEnd - firstFrameStart).count();
    const double textureLoadMs = RowlEngine_GetLastFrameTextureLoadMilliseconds(handle);
    const double nonTextureRenderMs = RowlEngine_GetLastFrameNonTextureRenderMilliseconds(handle);
    const double textRasterizationMs = RowlEngine_GetLastFrameTextRasterizationMilliseconds(handle);
    const double rendererFlushMs = RowlEngine_GetLastFrameRendererFlushMilliseconds(handle);
    if (textureLoadMs < 0.0 || nonTextureRenderMs < 0.0 || textRasterizationMs < 0.0 || rendererFlushMs < 0.0 ||
        textRasterizationMs > nonTextureRenderMs + 0.1 || rendererFlushMs > nonTextureRenderMs + 0.1 ||
        textureLoadMs + nonTextureRenderMs > firstFrameMs + 5.0) {
        std::cerr << "First-frame profile timings are inconsistent" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] First Frame Profile: texture load " << textureLoadMs
              << "ms, non-texture render " << nonTextureRenderMs
              << "ms, text rasterization " << textRasterizationMs
              << "ms, renderer flush " << rendererFlushMs << "ms" << std::endl;
    TEST_PASS("First Frame Texture and Renderer Profile");
    const auto warmTextureCount = RowlEngine_GetTextureCacheTextureCount(handle);
    const auto warmTextureBytes = RowlEngine_GetTextureCacheBytes(handle);
    if (warmTextureCount == 0 || warmTextureBytes == 0) {
        std::cerr << "Texture cache warm-up did not load the benchmark assets" << std::endl;
        exit(1);
    }
    const int FRAME_ITERATIONS = 60;
    auto frameStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < FRAME_ITERATIONS; ++i) {
        RowlEngine_Step(handle, 0.01667f);
    }
    auto frameEnd = std::chrono::high_resolution_clock::now();
    double frameElapsedMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
    double avgFrameMs = frameElapsedMs / FRAME_ITERATIONS;
    double equivalentFps = 1000.0 / avgFrameMs;
    std::cout << "  ⚡ [BENCHMARK] Native Render Pipeline: " << FRAME_ITERATIONS << " offscreen frames in "
              << frameElapsedMs << "ms (Avg: " << avgFrameMs << "ms/frame ~ "
              << static_cast<uint64_t>(equivalentFps) << " FPS equivalent)" << std::endl;
    TEST_PASS("Offscreen Software Render Pipeline Latency Benchmark");

    const auto cachedTextureCount = RowlEngine_GetTextureCacheTextureCount(handle);
    const auto cachedTextureBytes = RowlEngine_GetTextureCacheBytes(handle);
    const auto textureEvictionCount = RowlEngine_GetTextureCacheEvictionCount(handle);
    if (cachedTextureCount != warmTextureCount || cachedTextureBytes != warmTextureBytes) {
        std::cerr << "Steady-state render unexpectedly changed texture cache usage" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Texture Cache: " << cachedTextureCount << " unique textures, "
              << cachedTextureBytes << " RGBA bytes" << std::endl;
    TEST_PASS("Texture Cache Memory Telemetry");

    if (!benchmarkJsonPath.empty()) {
        writeBenchmarkJson(benchmarkJsonPath, startupMs, vfsElapsedMs, VFS_ITERATIONS, jsonElapsedMs,
                           JSON_ITERATIONS, firstFrameMs, avgFrameMs, textureLoadMs, nonTextureRenderMs,
                           cachedTextureCount, cachedTextureBytes, configuredTextureBudgetBytes,
                           textureEvictionCount);
    }

    constexpr uint64_t kDefaultTextureCacheBudget = 64ULL * 1024ULL * 1024ULL;
    if (RowlEngine_GetTextureCacheBudgetBytes(handle) != configuredTextureBudgetBytes ||
        (configuredTextureBudgetBytes == kDefaultTextureCacheBudget &&
         RowlEngine_GetTextureCacheEvictionCount(handle) != 0)) {
        std::cerr << "C-API texture cache budget telemetry contract failed" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Texture Cache Budget and Eviction Telemetry");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
}

void test_golden_project_benchmarks(const std::string& goldenJsonPath = "") {
    TEST_SECTION("Golden Project Benchmark Baseline");

    namespace fs = std::filesystem;
    const auto sourceProject = fs::path("samples/second_signal");
    const auto projectRoot = fs::temp_directory_path() /
        ("rowl_golden_bench_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code copyError;
    fs::copy(sourceProject, projectRoot, fs::copy_options::recursive, copyError);
    if (copyError) {
        std::cerr << "Could not isolate the Golden Project fixture: "
                  << copyError.message() << std::endl;
        exit(1);
    }

    auto startupStart = std::chrono::high_resolution_clock::now();
    RowlEngineHandle handle = RowlEngine_Create();
    const int initResult = handle ? RowlEngine_Init(handle, 1920, 1080, 0) : 0;
    auto startupEnd = std::chrono::high_resolution_clock::now();
    const double startupMs =
        std::chrono::duration<double, std::milli>(startupEnd - startupStart).count();
    if (!handle || initResult != 1) {
        std::cerr << "Golden benchmark engine init failed" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Golden Startup: " << startupMs << "ms" << std::endl;
    TEST_PASS("Golden Project Startup Benchmark");

    auto loadStart = std::chrono::high_resolution_clock::now();
    RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
    const uint64_t startNodeId = RowlEngine_GetCurrentNodeId(handle);
    auto loadEnd = std::chrono::high_resolution_clock::now();
    const double projectLoadMs =
        std::chrono::duration<double, std::milli>(loadEnd - loadStart).count();
    if (startNodeId == 0) {
        std::cerr << "Golden Project graph did not auto-load on mount: "
                  << RowlEngine_GetLastStoryGraphError(handle) << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Golden Project Load: " << projectLoadMs
              << "ms (start node #" << startNodeId << ")" << std::endl;
    TEST_PASS("Golden Project Load Benchmark");

    auto firstFrameStart = std::chrono::high_resolution_clock::now();
    RowlEngine_Step(handle, 0.0f);
    auto firstFrameEnd = std::chrono::high_resolution_clock::now();
    const double firstFrameMs =
        std::chrono::duration<double, std::milli>(firstFrameEnd - firstFrameStart).count();
    const double textureLoadMs = RowlEngine_GetLastFrameTextureLoadMilliseconds(handle);
    const double nonTextureRenderMs = RowlEngine_GetLastFrameNonTextureRenderMilliseconds(handle);
    uint32_t frameW = 0, frameH = 0;
    const uint8_t* framePixels = RowlEngine_GetPixelBuffer(handle, &frameW, &frameH);
    if (!framePixels || frameW != 1920 || frameH != 1080 || RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "Golden Project first frame produced no 1920x1080 buffer" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Golden First Frame: " << firstFrameMs << "ms" << std::endl;
    TEST_PASS("Golden Project First-Frame Benchmark");

    const int GOLDEN_FRAME_ITERATIONS = 60;
    auto steadyStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < GOLDEN_FRAME_ITERATIONS; ++i) {
        RowlEngine_Step(handle, 0.01667f);
    }
    auto steadyEnd = std::chrono::high_resolution_clock::now();
    const double steadyTotalMs =
        std::chrono::duration<double, std::milli>(steadyEnd - steadyStart).count();
    const double steadyFrameMs = steadyTotalMs / GOLDEN_FRAME_ITERATIONS;
    if (RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "Golden Project steady-state loop stopped the engine" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Golden Steady Frame: " << steadyFrameMs
              << "ms/frame (~" << static_cast<uint64_t>(1000.0 / steadyFrameMs)
              << " FPS equivalent)" << std::endl;
    TEST_PASS("Golden Project Steady-Frame Benchmark");

    const auto textureCount = RowlEngine_GetTextureCacheTextureCount(handle);
    const auto textureBytes = RowlEngine_GetTextureCacheBytes(handle);
    const auto textureBudget = RowlEngine_GetTextureCacheBudgetBytes(handle);
    const auto evictionCount = RowlEngine_GetTextureCacheEvictionCount(handle);
    std::cout << "  ⚡ [BENCHMARK] Golden Texture Cache: " << textureCount
              << " textures, " << textureBytes << " RGBA bytes" << std::endl;
    TEST_PASS("Golden Project Texture and Memory Telemetry");

    if (!goldenJsonPath.empty()) {
        writeGoldenBenchmarkJson(goldenJsonPath, startupMs, projectLoadMs, startNodeId,
                                 firstFrameMs, textureLoadMs, nonTextureRenderMs,
                                 steadyTotalMs, GOLDEN_FRAME_ITERATIONS, steadyFrameMs,
                                 textureCount, textureBytes, textureBudget, evictionCount);
    }

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    std::error_code removeError;
    fs::remove_all(projectRoot, removeError);
}
