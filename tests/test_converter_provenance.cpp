/**
 * test_converter_provenance.cpp — Faz 5 Dilim 5 converter provenance C API.
 *
 * RowlEngine_GetAssetProvenanceJson: sidecar var / yok / bozuk (3 kol) +
 * caller-buffer size-query / undersized + null-handle + capability biti.
 */
#include "rowl_test_harness.hpp"

#include <nlohmann/json.hpp>

namespace {

void checkProvenance(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "ConverterProvenance contract failed: " << message << std::endl;
        exit(1);
    }
}

std::string queryProvenance(RowlEngineHandle handle, const char* asset) {
    uint32_t required = 0;
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, asset, nullptr, 0, &required) ==
                            ROWL_RESULT_OK &&
                        required > 1,
                    "size query must succeed");
    std::vector<char> tiny(required - 1, 'x');
    uint32_t repeated = 0;
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, asset, tiny.data(),
                                                      static_cast<uint32_t>(tiny.size()),
                                                      &repeated) ==
                            ROWL_RESULT_BUFFER_TOO_SMALL &&
                        repeated == required && tiny.front() == '\0',
                    "undersized buffer must clear + BUFFER_TOO_SMALL");
    std::vector<char> buffer(required, '\0');
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, asset, buffer.data(),
                                                      static_cast<uint32_t>(buffer.size()),
                                                      &repeated) == ROWL_RESULT_OK &&
                        repeated == required && buffer.back() == '\0',
                    "exact-size copy must succeed");
    return std::string(buffer.data());
}

const char* kGoodSidecar = R"({
  "source_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "converter_name": "rowl_oggenc",
  "converter_version": "1.0.0",
  "settings": {
    "quality_q": 4,
    "sample_rate_hz": 44100,
    "channels": 2,
    "serial": 1234
  },
  "output_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "created_by": "rowl_oggenc 1.0.0"
})";

} // namespace

void test_converter_provenance() {
    TEST_SECTION("Converter Provenance (GetAssetProvenanceJson)");

    uint64_t capabilities = 0;
    checkProvenance(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK &&
                        (capabilities & ROWL_ENGINE_CAPABILITY_CONVERTER_PROVENANCE) != 0,
                    "CONVERTER_PROVENANCE capability bit must be set");

    const auto project =
        (std::filesystem::temp_directory_path() / "rowl-provenance-test").string();
    std::filesystem::create_directories(project + "/Assets/audio");
    std::filesystem::create_directories(project + "/Assets/images");
    {
        std::ofstream(project + "/Assets/audio/music.ogg", std::ios::binary) << "fake-ogg";
        std::ofstream(project + "/Assets/audio/music.ogg.rowlconv.json") << kGoodSidecar;
        std::ofstream(project + "/Assets/audio/plain.wav", std::ios::binary) << "fake-wav";
        std::ofstream(project + "/Assets/images/hero.png", std::ios::binary) << "fake-png";
        std::ofstream(project + "/Assets/images/hero.png.rowlconv.json")
            << "{ this is not json !!";
    }

    RowlEngineHandle handle = RowlEngine_Create();
    checkProvenance(handle != nullptr, "Create failed");
    checkProvenance(RowlEngine_Init(handle, 1920, 1080, 0) == 1, "Init failed");
    RowlEngine_SetProjectDirectory(handle, project.c_str());

    // 1. kol: sidecar var — doğrula + şema anahtarları.
    const std::string json = queryProvenance(handle, "audio/music.ogg");
    const auto parsed = nlohmann::json::parse(json);
    checkProvenance(parsed["converter_name"] == "rowl_oggenc" &&
                        parsed["converter_version"] == "1.0.0" &&
                        parsed["settings"]["quality_q"] == 4 &&
                        parsed["settings"]["sample_rate_hz"] == 44100 &&
                        parsed["settings"]["channels"] == 2 &&
                        parsed["created_by"] == "rowl_oggenc 1.0.0",
                    "sidecar schema keys mismatch");

    // 2. kol: sidecar yok — normal durum, FILE_NOT_FOUND (hata değil).
    uint32_t required = 0;
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, "audio/plain.wav", nullptr, 0,
                                                      &required) ==
                        ROWL_RESULT_FILE_NOT_FOUND,
                    "missing sidecar must report FILE_NOT_FOUND");

    // 3. kol: sidecar bozuk — PARSE_ERROR.
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, "images/hero.png", nullptr, 0,
                                                      &required) == ROWL_RESULT_PARSE_ERROR,
                    "corrupt sidecar must report PARSE_ERROR");

    // Girdi sınırı: null / boş / NUL-suz devasa giriş reddedilir.
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, nullptr, nullptr, 0, &required) ==
                        ROWL_RESULT_INVALID_ARGUMENT,
                    "null path must be INVALID_ARGUMENT");
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, "", nullptr, 0, &required) ==
                        ROWL_RESULT_INVALID_ARGUMENT,
                    "empty path must be INVALID_ARGUMENT");

    // Null-handle: boyut-sorgu dahil her form INVALID_HANDLE.
    checkProvenance(RowlEngine_GetAssetProvenanceJson(nullptr, "audio/music.ogg", nullptr, 0,
                                                      &required) == ROWL_RESULT_INVALID_HANDLE,
                    "null handle must be INVALID_HANDLE");
    char probe[8] = {};
    checkProvenance(RowlEngine_GetAssetProvenanceJson(nullptr, "audio/music.ogg", probe,
                                                      sizeof(probe),
                                                      &required) == ROWL_RESULT_INVALID_HANDLE,
                    "null handle with buffer must be INVALID_HANDLE");
    checkProvenance(RowlEngine_GetAssetProvenanceJson(handle, "audio/music.ogg", nullptr, 0,
                                                      nullptr) == ROWL_RESULT_INVALID_ARGUMENT,
                    "null outRequiredSize must be INVALID_ARGUMENT");

    RowlEngine_Destroy(handle);
    std::filesystem::remove_all(project);

    TEST_PASS("Converter Provenance (GetAssetProvenanceJson)");
}
