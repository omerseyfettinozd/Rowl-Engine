/**
 * character_layers.cpp
 *
 * Faz 5 Dilim 3 — katmanli karakter + expression preset implementasyonu.
 * Saf logic; render'a dokunmaz (cikti mevcut sprite hattina beslenir),
 * engine.cpp / window.cpp degismez.
 */

#include "rowl/scene/character_layers.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Rowl::Scene {

namespace {

constexpr std::array<std::string_view, kCharacterSlotCount> kSlotNames{
    "body", "face", "outfit", "accessory"};

bool hasNulByte(std::string_view text) noexcept {
    return text.find('\0') != std::string_view::npos;
}

bool isValidSlotObject(const nlohmann::json& value) noexcept {
    return value.is_object();
}

} // namespace

const char* characterSlotName(CharacterSlot slot) noexcept {
    const auto index = static_cast<std::size_t>(slot);
    if (index >= kCharacterSlotCount) return "";
    return kSlotNames[index].data();
}

std::optional<CharacterSlot> characterSlotFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kCharacterSlotCount; ++i) {
        if (name == kSlotNames[i]) return static_cast<CharacterSlot>(i);
    }
    return std::nullopt;
}

bool isSyntacticallyResolvableAsset(std::string_view asset) noexcept {
    if (asset.empty()) return false;
    if (asset.size() > kMaxCharacterAssetPathBytes) return false;
    if (hasNulByte(asset)) return false;
    return true;
}

bool CharacterLayers::setSlotAsset(CharacterSlot slot, std::string_view asset) {
    const auto index = static_cast<std::size_t>(slot);
    if (index >= kCharacterSlotCount) {
        m_lastError = "unknown character slot";
        return false;
    }
    if (asset.empty()) {
        m_slots[index].asset.clear();
        m_lastError.clear();
        return true;
    }
    if (asset.size() > kMaxCharacterAssetPathBytes) {
        m_lastError = std::string("slot '") + characterSlotName(slot) +
                      "': asset path exceeds " +
                      std::to_string(kMaxCharacterAssetPathBytes) + " bytes";
        return false;
    }
    if (hasNulByte(asset)) {
        m_lastError = std::string("slot '") + characterSlotName(slot) +
                      "': asset path contains NUL byte";
        return false;
    }
    m_slots[index].asset.assign(asset.data(), asset.size());
    m_lastError.clear();
    return true;
}

bool CharacterLayers::setSlotOpacity(CharacterSlot slot, float opacity) {
    const auto index = static_cast<std::size_t>(slot);
    if (index >= kCharacterSlotCount) {
        m_lastError = "unknown character slot";
        return false;
    }
    if (!std::isfinite(opacity)) {
        m_lastError = std::string("slot '") + characterSlotName(slot) +
                      "': non-finite opacity rejected";
        return false;
    }
    m_slots[index].opacity = std::clamp(opacity, 0.0f, 1.0f);
    m_lastError.clear();
    return true;
}

bool CharacterLayers::setSlotVisible(CharacterSlot slot, bool visible) {
    const auto index = static_cast<std::size_t>(slot);
    if (index >= kCharacterSlotCount) {
        m_lastError = "unknown character slot";
        return false;
    }
    m_slots[index].visible = visible;
    m_lastError.clear();
    return true;
}

const CharacterSlotState& CharacterLayers::slotState(CharacterSlot slot) const noexcept {
    // Gecerli slot disi erisim C++ tarafinda sozlesme ihlalidir; C API zaten
    // isim dogrulamasi yapar. Fail-closed: body doner, tasiyma yok.
    const auto index = static_cast<std::size_t>(slot);
    if (index >= kCharacterSlotCount) return m_slots[0];
    return m_slots[index];
}

void CharacterLayers::clearError() noexcept {
    m_lastError.clear();
    m_lastSkipped.clear();
}

std::vector<CharacterDrawItem> CharacterLayers::composeDrawList(
    const CharacterAssetResolver& resolver) const {
    m_lastSkipped.clear();
    m_lastError.clear();
    std::vector<CharacterDrawItem> out;
    out.reserve(kCharacterSlotCount);
    for (std::size_t i = 0; i < kCharacterSlotCount; ++i) {
        const auto slot = static_cast<CharacterSlot>(i);
        const auto& state = m_slots[i];
        if (!state.visible) continue;
        if (state.asset.empty()) continue;
        if (!(state.opacity > 0.0f)) continue; // <= 0 sessiz skip
        if (!isSyntacticallyResolvableAsset(state.asset)) {
            m_lastSkipped.emplace_back(std::string("slot '") +
                                       characterSlotName(slot) +
                                       "': unresolvable asset skipped");
            continue;
        }
        if (resolver && !resolver(slot, state.asset)) {
            m_lastSkipped.emplace_back(std::string("slot '") +
                                       characterSlotName(slot) +
                                       "': resolver rejected asset, skipped");
            continue;
        }
        out.push_back(CharacterDrawItem{slot, state.asset, state.opacity});
    }
    if (!m_lastSkipped.empty()) {
        m_lastError = m_lastSkipped.front();
    }
    return out;
}

std::vector<LayerSpriteDraw> CharacterLayers::toSpriteDraws(
    float x, float y, float w, float h,
    const CharacterAssetResolver& resolver) const {
    std::vector<LayerSpriteDraw> out;
    for (const auto& item : composeDrawList(resolver)) {
        out.push_back(LayerSpriteDraw{item.asset, x, y, w, h, item.opacity});
    }
    return out;
}

bool CharacterLayers::parseComponentData(const nlohmann::json& data,
                                         CharacterLayers& out,
                                         std::string& outError) {
    CharacterLayers staged;
    if (!data.is_object()) {
        outError = "character data is not an object";
        return false;
    }
    // Opsiyonel "layers": obje {slot: {asset, opacity, visible} | "asset"}.
    if (data.contains("layers")) {
        const auto& layers = data["layers"];
        if (!isValidSlotObject(layers)) {
            outError = "'layers' is not an object";
            return false;
        }
        for (const auto& [key, value] : layers.items()) {
            const auto slot = characterSlotFromName(key);
            if (!slot) continue; // bilinmeyen slot anahtari yoksayilir
            const auto index = static_cast<std::size_t>(*slot);
            if (value.is_string()) {
                const auto asset = value.get<std::string>();
                if (!asset.empty() && !isSyntacticallyResolvableAsset(asset)) {
                    outError = std::string("slot '") + key + "': invalid asset";
                    return false;
                }
                staged.m_slots[index].asset = asset;
            } else if (value.is_object()) {
                if (value.contains("asset")) {
                    if (!value["asset"].is_string()) {
                        outError = std::string("slot '") + key + "': 'asset' is not a string";
                        return false;
                    }
                    const auto asset = value["asset"].get<std::string>();
                    if (!asset.empty() && !isSyntacticallyResolvableAsset(asset)) {
                        outError = std::string("slot '") + key + "': invalid asset";
                        return false;
                    }
                    staged.m_slots[index].asset = asset;
                }
                if (value.contains("opacity")) {
                    if (!value["opacity"].is_number()) {
                        outError = std::string("slot '") + key + "': 'opacity' is not a number";
                        return false;
                    }
                    const float opacity = value["opacity"].get<float>();
                    if (!std::isfinite(opacity)) {
                        outError = std::string("slot '") + key + "': non-finite opacity";
                        return false;
                    }
                    staged.m_slots[index].opacity =
                        std::clamp(opacity, 0.0f, 1.0f);
                }
                if (value.contains("visible")) {
                    if (!value["visible"].is_boolean()) {
                        outError = std::string("slot '") + key + "': 'visible' is not a boolean";
                        return false;
                    }
                    staged.m_slots[index].visible = value["visible"].get<bool>();
                }
            } else {
                outError = std::string("slot '") + key + "': expected string or object";
                return false;
            }
        }
    }
    // Migration: eski tek-sprite "sprite" anahtari, acik body layer yokken
    // body slotuna duser. Yeni "layers.body.asset" doluysa o kazanir.
    if (staged.m_slots[0].asset.empty() && data.contains("sprite")) {
        if (!data["sprite"].is_string()) {
            outError = "legacy 'sprite' is not a string";
            return false;
        }
        const auto legacy = data["sprite"].get<std::string>();
        if (!legacy.empty()) {
            if (!isSyntacticallyResolvableAsset(legacy)) {
                outError = "legacy 'sprite' asset is invalid";
                return false;
            }
            staged.m_slots[0].asset = legacy;
        }
    }
    out = std::move(staged);
    outError.clear();
    return true;
}

nlohmann::json CharacterLayers::toLayersJson() const {
    nlohmann::json layers = nlohmann::json::object();
    for (std::size_t i = 0; i < kCharacterSlotCount; ++i) {
        const auto& state = m_slots[i];
        layers[kSlotNames[i]] = {
            {"asset", state.asset},
            {"opacity", state.opacity},
            {"visible", state.visible},
        };
    }
    return nlohmann::json{{"layers", std::move(layers)}};
}

bool CharacterPresetLibrary::registerPreset(std::string_view name,
                                            const nlohmann::json& expression,
                                            std::string& outError) {
    if (name.empty() || name.size() > kMaxCharacterPresetNameBytes ||
        hasNulByte(name)) {
        m_lastError = "preset name is empty, oversized or contains NUL";
        outError = m_lastError;
        return false;
    }
    if (!expression.is_object()) {
        m_lastError = "expression is not an object";
        outError = m_lastError;
        return false;
    }
    CharacterExpression staged;
    staged.name.assign(name.data(), name.size());
    for (const auto& [key, value] : expression.items()) {
        const auto slot = characterSlotFromName(key);
        if (!slot) {
            m_lastError = std::string("unknown slot '") + key + "'";
            outError = m_lastError;
            return false;
        }
        if (!value.is_string()) {
            m_lastError = std::string("slot '") + key + "': asset is not a string";
            outError = m_lastError;
            return false;
        }
        const auto asset = value.get<std::string>();
        if (!asset.empty() && !isSyntacticallyResolvableAsset(asset)) {
            m_lastError = std::string("slot '") + key + "': invalid asset";
            outError = m_lastError;
            return false;
        }
        const auto index = static_cast<std::size_t>(*slot);
        staged.slotAssets[index] = asset;
        staged.hasSlot[index] = true;
    }
    for (auto& existing : m_presets) {
        if (existing.name == staged.name) {
            existing = staged;
            m_lastError.clear();
            outError.clear();
            return true;
        }
    }
    m_presets.push_back(std::move(staged));
    m_lastError.clear();
    outError.clear();
    return true;
}

bool CharacterPresetLibrary::hasPreset(std::string_view name) const noexcept {
    for (const auto& preset : m_presets) {
        if (preset.name == name) return true;
    }
    return false;
}

bool CharacterPresetLibrary::removePreset(std::string_view name) {
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it) {
        if (it->name == name) {
            m_presets.erase(it);
            m_lastError.clear();
            return true;
        }
    }
    m_lastError = "unknown preset";
    return false;
}

void CharacterPresetLibrary::clear() noexcept {
    m_presets.clear();
    m_lastError.clear();
}

std::vector<std::string> CharacterPresetLibrary::presetNames() const {
    std::vector<std::string> out;
    out.reserve(m_presets.size());
    for (const auto& preset : m_presets) out.push_back(preset.name);
    return out;
}

nlohmann::json CharacterPresetLibrary::presetListJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& preset : m_presets) arr.push_back(preset.name);
    return arr;
}

bool CharacterPresetLibrary::applyExpression(const std::string& name,
                                             CharacterLayers& layers,
                                             std::string& outError,
                                             const CharacterAssetResolver& resolver) const {
    const CharacterExpression* found = nullptr;
    for (const auto& preset : m_presets) {
        if (preset.name == name) {
            found = &preset;
            break;
        }
    }
    if (!found) {
        outError = std::string("unknown preset '") + name + "'";
        return false;
    }
    // Atomiklik: once TAMAMINI dogrula, sonra uygula. Dogrulama basarisizsa
    // layers'a dokunulmaz.
    for (std::size_t i = 0; i < kCharacterSlotCount; ++i) {
        if (!found->hasSlot[i]) continue;
        const auto& asset = found->slotAssets[i];
        if (asset.empty()) continue; // explicit clear her zaman gecerli
        if (!isSyntacticallyResolvableAsset(asset)) {
            outError = std::string("preset '") + name + "': slot '" +
                       characterSlotName(static_cast<CharacterSlot>(i)) +
                       "' asset is invalid, nothing applied";
            return false;
        }
        if (resolver && !resolver(static_cast<CharacterSlot>(i), asset)) {
            outError = std::string("preset '") + name + "': slot '" +
                       characterSlotName(static_cast<CharacterSlot>(i)) +
                       "' asset rejected, nothing applied";
            return false;
        }
    }
    for (std::size_t i = 0; i < kCharacterSlotCount; ++i) {
        if (!found->hasSlot[i]) continue;
        layers.setSlotAsset(static_cast<CharacterSlot>(i), found->slotAssets[i]);
    }
    outError.clear();
    return true;
}

} // namespace Rowl::Scene
