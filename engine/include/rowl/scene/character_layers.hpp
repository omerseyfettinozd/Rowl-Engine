/**
 * rowl/scene/character_layers.hpp
 *
 * Faz 5 Dilim 3 — katmanli karakter sistemi (native hat).
 *
 * Kapsam: body/face/outfit/accessory slotlari, sabit cizim sirasi
 * (body < face < outfit < accessory), slot basina bagimsiz asset +
 * opaklik + gorunurluk, isimli expression presetleri (atomik uygula),
 * component JSON migration (eski tek-sprite "sprite" -> body slotu).
 *
 * Compositing MEVCUT sprite hattini kullanir: composeDrawList()/toSpriteDraws()
 * ciktisi dogrudan Window::drawSprite(asset, x, y, w, h, opacity) cagrilarina
 * (veya CharacterRenderData listesine) beslenir. Yeni render backend yok;
 * frame_composition.hpp / window.cpp / engine.cpp degismez.
 *
 * Cozulemeyen slot asset'i atlanir + tani uretilir; diger slotlar cizilir,
 * crash yok. Gercek doku yukleme basarisizligi zaten window.cpp'deki
 * null-texture guard tarafindan yutulur; buradaki "cozulebilirlik" sentaktik
 * gecerlilik + opsiyonel dis resolver'dir (dosya varligi C# kalicilik
 * katmaninda / linter'da denetlenir).
 */

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Rowl::Scene {

/// Sabit slot sayisi ve cizim sirasi (dusuk index once cizilir).
inline constexpr std::size_t kCharacterSlotCount = 4;

enum class CharacterSlot : std::size_t {
    Body = 0,
    Face = 1,
    Outfit = 2,
    Accessory = 3,
};

/// Slot basina asset yolu icin sentaktik ust sinir (byte). Uzeri hem
/// setSlotAsset'te hem preset kaydinda/uygulamasinda "bozuk" sayilir ve
/// fail-closed reddedilir. C API giris tasiyicisi (256 KiB) bundan bagimsiz
/// ve daha genistir; ikisi de dokumante edilmistir.
inline constexpr std::size_t kMaxCharacterAssetPathBytes = 4096;

/// Preset/expression ismi icin ust sinir (byte, UTF-8).
inline constexpr std::size_t kMaxCharacterPresetNameBytes = 256;

/// Kanonik slot ismi (kucuk harf, tekil). Bilinmeyen isimler bos doner.
const char* characterSlotName(CharacterSlot slot) noexcept;
std::optional<CharacterSlot> characterSlotFromName(std::string_view name) noexcept;

/// Sentaktik cozulebilirlik: bos degil, NUL icermiyor, boyut siniri icinde.
/// Bos string "temizlenmis slot" anlamina gelir (gecerli, cizilmez, tani yok).
bool isSyntacticallyResolvableAsset(std::string_view asset) noexcept;

struct CharacterSlotState {
    std::string asset;
    float opacity = 1.0f;
    bool visible = true;
};

/// Cizilebilir tek katman: sabit siraya gore uretilir.
struct CharacterDrawItem {
    CharacterSlot slot = CharacterSlot::Body;
    std::string asset;
    float opacity = 1.0f;
};

/// Mevcut Window::drawSprite imzasina birebir beslenebilir cizim kaydi.
struct LayerSpriteDraw {
    std::string asset;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float opacity = 1.0f;
};

/// Dis cozunurluk probu (or. dosya/linter varlik kontrolu). Donus false ise
/// slot atlanir + tani uretilir. Bos (null) resolver = yalnizca sentaktik
/// kontrol kullanilir.
using CharacterAssetResolver =
    std::function<bool(CharacterSlot slot, const std::string& asset)>;

class CharacterLayers {
public:
    CharacterLayers() = default;

    // ── Slot yazma (fail-closed: red halinde slot degismez) ──
    // Bos asset slotu temizler (her zaman basarili). Sentaktik olarak bozuk
    // asset (NUL / asiri uzun) reddedilir, lastError dolar.
    bool setSlotAsset(CharacterSlot slot, std::string_view asset);
    // Non-finite reddedilir; finite deger [0,1]'e clamp'lenir.
    bool setSlotOpacity(CharacterSlot slot, float opacity);
    bool setSlotVisible(CharacterSlot slot, bool visible);

    // ── Slot okuma ──
    const CharacterSlotState& slotState(CharacterSlot slot) const noexcept;
    const std::string& lastError() const noexcept { return m_lastError; }
    const std::vector<std::string>& lastSkipped() const noexcept { return m_lastSkipped; }
    void clearError() noexcept;

    // ── Compositing (mevcut sprite hatti ustunde birlestir) ──
    // Sira garantisi: body < face < outfit < accessory. Gorunmez / bos /
    // opakligi 0 olan slot sessizce atlanir; cozulemez slot tani uretir.
    // Her cagri m_lastSkipped'i bastan yazar; basarili (skip'siz) cagri
    // m_lastError'i temizler.
    std::vector<CharacterDrawItem> composeDrawList(
        const CharacterAssetResolver& resolver = {}) const;
    // Ayni rect'i paylasan slot cizimleri (drawSprite'a birebir).
    std::vector<LayerSpriteDraw> toSpriteDraws(
        float x, float y, float w, float h,
        const CharacterAssetResolver& resolver = {}) const;

    // ── Component JSON (migration) ──
    // data: "character" bileseninin "data" objesi. Opsiyonel "layers" anahtari
    // okunur; yoksa/eksikse eski "sprite" anahtari body slotuna duser.
    // Bilinmeyen anahtarlar yoksayilir (eski hat zarar gormez). Donus false
    // ise outError dolar ve out degismez (atomik parse).
    static bool parseComponentData(const nlohmann::json& data,
                                   CharacterLayers& out,
                                   std::string& outError);
    // Katmanlarin kanonik JSON karsiligi ("layers" objesi).
    nlohmann::json toLayersJson() const;

private:
    std::array<CharacterSlotState, kCharacterSlotCount> m_slots;
    mutable std::string m_lastError;
    mutable std::vector<std::string> m_lastSkipped;
};

/// Isimli slot->asset eslemesi. Eksik slot (hasSlot=false) apply sirasinda
/// o slotu oldugu gibi birakir; mevcut ama bos asset slotu temizler.
struct CharacterExpression {
    std::string name;
    std::array<std::string, kCharacterSlotCount> slotAssets;
    std::array<bool, kCharacterSlotCount> hasSlot{false, false, false, false};
};

/// Bellek-ici preset listesi (kalicilik C# tarafinda JSON).
/// Tum yazma yollar fail-closed: red halinde liste degismez.
class CharacterPresetLibrary {
public:
    CharacterPresetLibrary() = default;

    // expressionJson: obje {slotIsmi: assetString}. Bilinmeyen anahtar,
    // non-string deger veya asiri uzun asset -> VALIDATION hatasi, liste
    // degismez. Ayni isim yeniden kaydolursa degistirir (replace).
    bool registerPreset(std::string_view name, const nlohmann::json& expression,
                        std::string& outError);
    bool hasPreset(std::string_view name) const noexcept;
    bool removePreset(std::string_view name);
    void clear() noexcept;
    std::vector<std::string> presetNames() const;
    nlohmann::json presetListJson() const;

    // ATOMIK uygula: 4 slotun tamami cozulebilirse uygula; biri bozuk/eksikse
    // HICBIRINI degistirme + outError doldur. "Eksik" = kayitli preset'te
    // hasSlot=true olup resolver'dan gecemeyen asset. Kayitsiz isim -> false.
    bool applyExpression(const std::string& name, CharacterLayers& layers,
                         std::string& outError,
                         const CharacterAssetResolver& resolver = {}) const;

    const std::string& lastError() const noexcept { return m_lastError; }

private:
    std::vector<CharacterExpression> m_presets;
    std::string m_lastError;
};

} // namespace Rowl::Scene
