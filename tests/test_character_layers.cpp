/**
 * test_character_layers.cpp — Faz 5 Dilim 3 native hat testleri.
 *
 * Kapsam: slot sirasi (body<face<outfit<accessory), slot atlama (bozuk
 * asset -> digerleri cizilir + tani), expression atomikligi (bozuk iceren
 * preset HICBIR seyi degistirmez + hata), preset-unknown fail-closed,
 * opaklik/gorunurluk, C API vektorleri (32768 biti, alt bitler aynen,
 * null-handle, caller-buffer size-query/undersized, oversized-input red),
 * migration (eski tek-sprite JSON -> body slotu, yeni alanlar yokken
 * default; yeni anahtarli JSON eski hatta zararsiz).
 */
#include "rowl_test_harness.hpp"
#include "rowl/scene/character_layers.hpp"

#include <nlohmann/json.hpp>

namespace {

void failLayers(const std::string& message) {
    std::cerr << "character_layers FAILED: " << message << std::endl;
    exit(1);
}

void checkLayers(bool condition, const std::string& message) {
    if (!condition) failLayers(message);
}

// Resolver: "BROKEN" iceren asset yuklenemez sayilir.
bool testResolver(Rowl::Scene::CharacterSlot, const std::string& asset) {
    return asset.find("BROKEN") == std::string::npos;
}

std::string queryCallerBuffer(
    RowlEngineHandle handle,
    RowlEngine_ResultCode (*getter)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    if (getter(handle, nullptr, 0, &required) != ROWL_RESULT_OK || required < 1) {
        failLayers("caller-buffer size query failed");
    }
    if (required > 1) {
        std::vector<char> undersized(required - 1, 'x');
        uint32_t repeated = 0;
        if (getter(handle, undersized.data(),
                   static_cast<uint32_t>(undersized.size()),
                   &repeated) != ROWL_RESULT_BUFFER_TOO_SMALL ||
            repeated != required || undersized.front() != '\0') {
            failLayers("caller-buffer undersized contract failed");
        }
    }
    std::vector<char> buffer(required, '\0');
    uint32_t repeated = 0;
    if (getter(handle, buffer.data(), static_cast<uint32_t>(buffer.size()),
               &repeated) != ROWL_RESULT_OK ||
        repeated != required || buffer.back() != '\0') {
        failLayers("caller-buffer exact-size copy failed");
    }
    return std::string(buffer.data());
}

std::string querySlotAsset(RowlEngineHandle handle, const char* slot) {
    uint32_t required = 0;
    if (RowlEngine_GetCharacterSlotAssetUtf8(handle, slot, nullptr, 0,
                                             &required) != ROWL_RESULT_OK) {
        failLayers("slot asset size query failed");
    }
    std::vector<char> buffer(required, '\0');
    uint32_t repeated = 0;
    if (RowlEngine_GetCharacterSlotAssetUtf8(handle, slot, buffer.data(),
                                             static_cast<uint32_t>(buffer.size()),
                                             &repeated) != ROWL_RESULT_OK) {
        failLayers("slot asset copy failed");
    }
    return std::string(buffer.data());
}

void testSlotOrderAndSkip() {
    TEST_SECTION("CharacterLayers slot order + skip");
    Rowl::Scene::CharacterLayers layers;
    checkLayers(layers.setSlotAsset(Rowl::Scene::CharacterSlot::Accessory, "acc.png"),
                "accessory set failed");
    checkLayers(layers.setSlotAsset(Rowl::Scene::CharacterSlot::Body, "body.png"),
                "body set failed");
    checkLayers(layers.setSlotAsset(Rowl::Scene::CharacterSlot::Face, "face.png"),
                "face set failed");
    checkLayers(layers.setSlotAsset(Rowl::Scene::CharacterSlot::Outfit, "out.png"),
                "outfit set failed");
    auto draws = layers.composeDrawList();
    checkLayers(draws.size() == 4, "expected 4 draw items");
    checkLayers(draws[0].slot == Rowl::Scene::CharacterSlot::Body &&
                draws[1].slot == Rowl::Scene::CharacterSlot::Face &&
                draws[2].slot == Rowl::Scene::CharacterSlot::Outfit &&
                draws[3].slot == Rowl::Scene::CharacterSlot::Accessory,
                "draw order is not body<face<outfit<accessory>");
    checkLayers(layers.lastError().empty(), "no diagnostic expected for clean compose");

    // Bozuk asset: o slot atlanir, diger 3 cizilir, tani uretilir.
    checkLayers(layers.setSlotAsset(Rowl::Scene::CharacterSlot::Face, "BROKEN_face.png"),
                "broken asset set failed");
    auto partial = layers.composeDrawList(testResolver);
    checkLayers(partial.size() == 3, "broken slot must be skipped, others drawn");
    for (const auto& item : partial) {
        checkLayers(item.slot != Rowl::Scene::CharacterSlot::Face,
                    "broken face slot leaked into draw list");
    }
    checkLayers(!layers.lastError().empty() && !layers.lastSkipped().empty(),
                "skip must produce a diagnostic");
    // toSpriteDraws ayni sirayi ve rect/opaklik tasiyicisini korur.
    auto sprites = layers.toSpriteDraws(10.0f, 20.0f, 30.0f, 40.0f, testResolver);
    checkLayers(sprites.size() == 3, "sprite draws must mirror draw list");
    checkLayers(sprites[0].asset == "body.png" && sprites[0].x == 10.0f &&
                sprites[0].opacity == 1.0f,
                "sprite draw mapping wrong");
    TEST_PASS("character_layers slot order + skip");
}

void testOpacityVisibility() {
    TEST_SECTION("CharacterLayers opacity/visibility");
    Rowl::Scene::CharacterLayers layers;
    checkLayers(!layers.setSlotOpacity(Rowl::Scene::CharacterSlot::Body,
                                       std::numeric_limits<float>::quiet_NaN()),
                "NaN opacity must be rejected");
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).opacity == 1.0f,
                "rejected opacity must not change slot");
    checkLayers(layers.setSlotOpacity(Rowl::Scene::CharacterSlot::Body, 2.5f),
                "over-range opacity set failed");
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).opacity == 1.0f,
                "opacity must clamp to 1");
    checkLayers(layers.setSlotOpacity(Rowl::Scene::CharacterSlot::Body, -3.0f),
                "under-range opacity set failed");
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).opacity == 0.0f,
                "opacity must clamp to 0");
    checkLayers(layers.setSlotAsset(Rowl::Scene::CharacterSlot::Body, "b.png"),
                "body asset set failed");
    checkLayers(layers.composeDrawList().empty(),
                "zero-opacity slot must not draw");
    checkLayers(layers.setSlotOpacity(Rowl::Scene::CharacterSlot::Body, 0.5f),
                "opacity set failed");
    checkLayers(layers.setSlotVisible(Rowl::Scene::CharacterSlot::Body, false),
                "visible=false failed");
    checkLayers(layers.composeDrawList().empty(),
                "invisible slot must not draw");
    checkLayers(layers.setSlotVisible(Rowl::Scene::CharacterSlot::Body, true),
                "visible=true failed");
    auto draws = layers.composeDrawList();
    checkLayers(draws.size() == 1 && draws[0].opacity == 0.5f,
                "visible slot with opacity must draw");
    TEST_PASS("character_layers opacity/visibility");
}

void testExpressionAtomicity() {
    TEST_SECTION("CharacterLayers expression atomicity");
    Rowl::Scene::CharacterLayers layers;
    layers.setSlotAsset(Rowl::Scene::CharacterSlot::Body, "old_body.png");
    layers.setSlotAsset(Rowl::Scene::CharacterSlot::Face, "old_face.png");

    Rowl::Scene::CharacterPresetLibrary library;
    std::string error;
    const auto expression = nlohmann::json::parse(
        R"({"body":"new_body.png","face":"new_face.png","outfit":"new_out.png","accessory":"BROKEN_acc.png"})");
    checkLayers(library.registerPreset("smile", expression, error),
                "preset register failed: " + error);
    // 3 gecerli + 1 bozuk -> HICBIRI degismez + hata.
    checkLayers(!library.applyExpression("smile", layers, error, testResolver),
                "atomic apply must fail on broken slot");
    checkLayers(!error.empty(), "failed apply must report an error");
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).asset == "old_body.png" &&
                layers.slotState(Rowl::Scene::CharacterSlot::Face).asset == "old_face.png" &&
                layers.slotState(Rowl::Scene::CharacterSlot::Outfit).asset.empty(),
                "partial apply leaked into layer state");
    // Temiz resolver ile tamami uygulanir.
    checkLayers(library.applyExpression("smile", layers, error),
                "clean apply failed: " + error);
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).asset == "new_body.png" &&
                layers.slotState(Rowl::Scene::CharacterSlot::Outfit).asset == "new_out.png",
                "clean apply did not update all slots");
    // Bilinmeyen preset fail-closed.
    checkLayers(!library.applyExpression("nope", layers, error),
                "unknown preset must fail");
    checkLayers(!error.empty(), "unknown preset must report an error");
    // Kayit sirasinda bilinmeyen slot / non-string red (liste degismez).
    checkLayers(!library.registerPreset("bad",
                                        nlohmann::json::parse(R"({"tail":"x.png"})"), error),
                "unknown slot key must be rejected");
    checkLayers(!library.hasPreset("bad"), "rejected preset leaked into library");
    checkLayers(!library.registerPreset("bad2",
                                        nlohmann::json::parse(R"({"body":42})"), error),
                "non-string asset must be rejected");
    checkLayers(!library.hasPreset("bad2"), "rejected preset leaked into library");
    // Eksik slot maskesi: belirtilmeyen slotlar oldugu gibi birakilir.
    checkLayers(library.registerPreset("face-only",
                                       nlohmann::json::parse(R"({"face":"f2.png"})"), error),
                "face-only register failed");
    checkLayers(library.applyExpression("face-only", layers, error),
                "face-only apply failed");
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Face).asset == "f2.png" &&
                layers.slotState(Rowl::Scene::CharacterSlot::Body).asset == "new_body.png",
                "partial preset must leave unmentioned slots untouched");
    TEST_PASS("character_layers expression atomicity");
}

void testMigration() {
    TEST_SECTION("CharacterLayers component JSON migration");
    Rowl::Scene::CharacterLayers layers;
    std::string error;
    // Eski tek-sprite JSON -> body slotu, digerleri default.
    checkLayers(Rowl::Scene::CharacterLayers::parseComponentData(
                    nlohmann::json::parse(R"({"sprite":"hero.png"})"), layers, error),
                "legacy parse failed: " + error);
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).asset == "hero.png",
                "legacy sprite must land in body slot");
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Face).asset.empty() &&
                layers.slotState(Rowl::Scene::CharacterSlot::Body).opacity == 1.0f &&
                layers.slotState(Rowl::Scene::CharacterSlot::Body).visible,
                "missing new keys must yield defaults");
    // Yeni layers objesi (obje + string shorthand karisik).
    checkLayers(Rowl::Scene::CharacterLayers::parseComponentData(
                    nlohmann::json::parse(
                        R"({"sprite":"hero.png","layers":{"body":{"asset":"b2.png","opacity":0.5,"visible":false},"face":"f.png"}})"),
                    layers, error),
                "layered parse failed: " + error);
    checkLayers(layers.slotState(Rowl::Scene::CharacterSlot::Body).asset == "b2.png" &&
                layers.slotState(Rowl::Scene::CharacterSlot::Body).opacity == 0.5f &&
                !layers.slotState(Rowl::Scene::CharacterSlot::Body).visible &&
                layers.slotState(Rowl::Scene::CharacterSlot::Face).asset == "f.png",
                "layered parse produced wrong state");
    // Bilinmeyen anahtarlar yoksayilir.
    checkLayers(Rowl::Scene::CharacterLayers::parseComponentData(
                    nlohmann::json::parse(R"({"sprite":"h.png","frobnicate":1})"),
                    layers, error),
                "unknown keys must be ignored");
    // Bozuk tip atomik red (out degismez).
    Rowl::Scene::CharacterLayers before = layers;
    (void)before;
    checkLayers(!Rowl::Scene::CharacterLayers::parseComponentData(
                    nlohmann::json::parse(R"({"layers":{"body":{"opacity":"opak"}}})"),
                    layers, error),
                "mistyped opacity must be rejected");
    checkLayers(!error.empty(), "rejected parse must report an error");
    TEST_PASS("character_layers migration");
}

void testCapabilityAndNullHandle() {
    TEST_SECTION("CharacterLayers C API capability + null-handle");
    uint64_t capabilities = 0;
    checkLayers(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK,
                "GetCapabilities failed");
    checkLayers((capabilities & ROWL_ENGINE_CAPABILITY_CHARACTER_LAYERS) != 0,
                "capability bit 32768 missing");
    constexpr uint64_t kLowerBits = 0x7FFFu; // bit 1..16384 aynen
    checkLayers((capabilities & kLowerBits) == kLowerBits,
                "capability bits 1..16384 changed");

    RowlEngineHandle dead = nullptr;
    uint32_t required = 0;
    float opacity = 0.0f;
    checkLayers(RowlEngine_SetCharacterSlotAsset(dead, "body", "a.png") ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle set asset not fail-closed");
    checkLayers(RowlEngine_GetCharacterSlotAssetUtf8(dead, "body", nullptr, 0,
                                                     &required) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle get asset not fail-closed");
    checkLayers(RowlEngine_SetCharacterSlotOpacity(dead, "body", 0.5f) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle set opacity not fail-closed");
    checkLayers(RowlEngine_GetCharacterSlotOpacity(dead, "body", &opacity) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle get opacity not fail-closed");
    checkLayers(RowlEngine_SetCharacterSlotVisible(dead, "body", 1) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle set visible not fail-closed");
    checkLayers(RowlEngine_IsCharacterSlotVisible(dead, "body") == 0,
                "null-handle is-visible not fail-closed");
    checkLayers(RowlEngine_RegisterCharacterPreset(dead, "p", "{}") ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle register not fail-closed");
    checkLayers(RowlEngine_ApplyCharacterExpression(dead, "p") ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle apply not fail-closed");
    checkLayers(RowlEngine_GetCharacterPresetListJson(dead, nullptr, 0,
                                                      &required) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle preset list not fail-closed");
    checkLayers(RowlEngine_GetCharacterDrawListJson(dead, nullptr, 0,
                                                    &required) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle draw list not fail-closed");
    checkLayers(RowlEngine_GetLastCharacterErrorUtf8(dead, nullptr, 0,
                                                     &required) ==
                    ROWL_RESULT_INVALID_HANDLE,
                "null-handle error query not fail-closed");
    TEST_PASS("character_layers capability + null-handle");
}

void testCApiVectors(RowlEngineHandle handle) {
    TEST_SECTION("CharacterLayers C API vectors");
    // Bilinmeyen slot fail-closed (durum degismez, getLastError dolar).
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, "tail", "x.png") ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "unknown slot set must fail");
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, "Body", "x.png") ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "slot names are case-sensitive");
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, nullptr, "x.png") ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "null slot name must fail");
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, "body", nullptr) ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "null asset must fail");
    checkLayers(RowlEngine_GetCharacterSlotOpacity(handle, "body", nullptr) ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "null out-opacity must fail");
    checkLayers(RowlEngine_SetCharacterSlotOpacity(handle, "body",
                                                   std::numeric_limits<float>::quiet_NaN()) ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "NaN opacity must fail");

    // Slot asset set/get roundtrip (caller-buffer).
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, "body", "hero_body.png") ==
                    ROWL_RESULT_OK,
                "set body asset failed");
    checkLayers(querySlotAsset(handle, "body") == "hero_body.png",
                "body asset roundtrip failed");
    // Opaklik clamp + roundtrip.
    checkLayers(RowlEngine_SetCharacterSlotOpacity(handle, "face", 5.0f) ==
                    ROWL_RESULT_OK,
                "set opacity failed");
    float opacity = 0.0f;
    checkLayers(RowlEngine_GetCharacterSlotOpacity(handle, "face", &opacity) ==
                    ROWL_RESULT_OK && opacity == 1.0f,
                "opacity clamp roundtrip failed");
    // Gorunurluk.
    checkLayers(RowlEngine_SetCharacterSlotVisible(handle, "outfit", 0) ==
                    ROWL_RESULT_OK,
                "set visible failed");
    checkLayers(RowlEngine_IsCharacterSlotVisible(handle, "outfit") == 0,
                "is-visible roundtrip failed");
    checkLayers(RowlEngine_IsCharacterSlotVisible(handle, "nope") == 0,
                "unknown slot is-visible must be 0");

    // Oversized input red (256 KiB + 1).
    const std::string huge(262145, 'a');
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, "body", huge.c_str()) ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "oversized asset must be rejected");
    checkLayers(querySlotAsset(handle, "body") == "hero_body.png",
                "rejected oversized input must not change slot");
    const std::string hugeName(300, 'n');
    checkLayers(RowlEngine_RegisterCharacterPreset(handle, hugeName.c_str(), "{}") ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "oversized preset name must be rejected");
    checkLayers(RowlEngine_RegisterCharacterPreset(handle, "p", huge.c_str()) ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "oversized expression JSON must be rejected");

    // Preset register/list/apply.
    checkLayers(RowlEngine_RegisterCharacterPreset(handle, "happy",
                                                   R"({"body":"b.png","face":"f.png"})") ==
                    ROWL_RESULT_OK,
                "preset register failed");
    checkLayers(RowlEngine_RegisterCharacterPreset(handle, "bad",
                                                   R"({"tail":"x.png"})") ==
                    ROWL_RESULT_VALIDATION_ERROR,
                "unknown slot in preset must be a validation error");
    checkLayers(RowlEngine_RegisterCharacterPreset(handle, "bad2", "{oops") ==
                    ROWL_RESULT_PARSE_ERROR,
                "malformed expression JSON must be a parse error");
    const std::string presetList = queryCallerBuffer(
        handle, RowlEngine_GetCharacterPresetListJson);
    checkLayers(presetList.find("\"happy\"") != std::string::npos &&
                presetList.find("bad") == std::string::npos,
                "preset list JSON wrong: " + presetList);
    checkLayers(RowlEngine_ApplyCharacterExpression(handle, "ghost") ==
                    ROWL_RESULT_INVALID_ARGUMENT,
                "unknown preset apply must fail");
    {
        const std::string err = queryCallerBuffer(
            handle, RowlEngine_GetLastCharacterErrorUtf8);
        checkLayers(!err.empty(), "failed apply must leave a diagnosis");
    }
    checkLayers(RowlEngine_ApplyCharacterExpression(handle, "happy") ==
                    ROWL_RESULT_OK,
                "preset apply failed");
    checkLayers(querySlotAsset(handle, "face") == "f.png",
                "applied preset did not update face slot");

    // Draw-list JSON sira gozlemlenebilirligi.
    checkLayers(RowlEngine_SetCharacterSlotAsset(handle, "accessory", "a.png") ==
                    ROWL_RESULT_OK &&
                RowlEngine_SetCharacterSlotAsset(handle, "outfit", "o.png") ==
                    ROWL_RESULT_OK &&
                RowlEngine_SetCharacterSlotVisible(handle, "outfit", 1) ==
                    ROWL_RESULT_OK,
                "draw-list fixture setup failed");
    const std::string drawList = queryCallerBuffer(
        handle, RowlEngine_GetCharacterDrawListJson);
    const auto drawJson = nlohmann::json::parse(drawList);
    checkLayers(drawJson.is_array() && drawJson.size() == 4,
                "draw list must carry 4 slots: " + drawList);
    checkLayers(drawJson[0]["slot"] == "body" && drawJson[1]["slot"] == "face" &&
                drawJson[2]["slot"] == "outfit" &&
                drawJson[3]["slot"] == "accessory",
                "draw list order wrong: " + drawList);
    TEST_PASS("character_layers C API vectors");
}

void testLegacyScenePathToleratesNewKeys(RowlEngineHandle handle) {
    TEST_SECTION("CharacterLayers legacy scene path + new keys");
    // Yeni opsiyonel anahtarlar eski story hattini kirmaz (bilinmeyen
    // anahtarlar yoksayilir, legacy "sprite" okunur).
    const char* sceneJson = R"([
        {"type":"dialogue","enabled":true,"data":{"speaker":"Efe","dialogue":"Merhaba"}},
        {"type":"character","enabled":true,"data":{"sprite":"hero.png","x":1440,"y":340,"width":360,"height":540,"layers":{"body":{"asset":"b2.png","opacity":0.5},"face":"f.png"}}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, sceneJson);
    checkLayers(std::string(RowlEngine_GetSpeaker(handle)) == "Efe",
                "legacy scene path broke on layered character JSON");
    TEST_PASS("character_layers legacy scene path");
}

void testStoryPlaybackComposesLayers(RowlEngineHandle handle) {
    TEST_SECTION("CharacterLayers story playback compose hook");
    Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(handle);
    checkLayers(engine != nullptr, "test bridge could not resolve engine");

    // `layers`'li node -> compose yolu: 4 katman sirayla, ortak rect.
    RowlEngine_UpdateSceneFromJson(handle,
        R"json([{"type":"character","enabled":true,"data":{"sprite":"legacy.png","x":100,"y":200,"width":300,"height":400,"layers":{"body":"b.png","face":"f.png","outfit":"o.png","accessory":"a.png"}}}])json");
    {
        const auto& chars = engine->getActiveCharacters();
        checkLayers(chars.size() == 4, "layered node must compose 4 draws");
        checkLayers(chars[0].sprite == "b.png" && chars[1].sprite == "f.png" &&
                    chars[2].sprite == "o.png" && chars[3].sprite == "a.png",
                    "compose order is not body<face<outfit<accessory");
        for (const auto& ch : chars) {
            checkLayers(ch.x == 100.0f && ch.y == 200.0f &&
                        ch.width == 300.0f && ch.height == 400.0f,
                        "composed draws must share the node rect");
        }
    }

    // `layers`'siz node -> legacy sprite birebir.
    RowlEngine_UpdateSceneFromJson(handle,
        R"json([{"type":"character","enabled":true,"data":{"sprite":"solo.png","x":10,"y":20,"width":30,"height":40}}])json");
    {
        const auto& chars = engine->getActiveCharacters();
        checkLayers(chars.size() == 1 && chars[0].sprite == "solo.png" &&
                    chars[0].x == 10.0f && chars[0].y == 20.0f &&
                    chars[0].width == 30.0f && chars[0].height == 40.0f,
                    "legacy node must pass through unchanged");
    }

    // Bozuk `layers` -> legacy sprite + tani (davranis degismedi).
    RowlEngine_UpdateSceneFromJson(handle,
        R"json([{"type":"character","enabled":true,"data":{"sprite":"fallback.png","layers":{"face":123}}}])json");
    {
        const auto& chars = engine->getActiveCharacters();
        checkLayers(chars.size() == 1 && chars[0].sprite == "fallback.png",
                    "corrupt layers must fall back to legacy sprite");
    }
    TEST_PASS("character_layers story playback compose hook");
}

} // namespace

void test_character_layers() {
    testSlotOrderAndSkip();
    testOpacityVisibility();
    testExpressionAtomicity();
    testMigration();
    testCapabilityAndNullHandle();

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) failLayers("RowlEngine_Create failed");
    testCApiVectors(handle);
    if (RowlEngine_Init(handle, 64, 64, 0) != 1) {
        failLayers("fixture Init failed");
    }
    testLegacyScenePathToleratesNewKeys(handle);
    testStoryPlaybackComposesLayers(handle);
    RowlEngine_Destroy(handle);
}
