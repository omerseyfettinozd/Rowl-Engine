// engine_pause_mixer.cpp — D15: pause-menu/mixer dörtlüsünün davranışsız
// taşınmış karşılığı.
//
// engine.cpp'den birebir taşınmıştır (D02 deseni):
//   2992-3001  Engine::pauseMenuVolume (satır→kazanç okuma eşlemesi)
//   3003-3016  Engine::pauseMenuVolume'ya yazma + #86 setter-commit
//   3018-3027  Engine::pauseMenuAdjustSelected (seçili satır ±adım)
//   3634-3642  Engine::commitMixerVolumesToGameState (#86 state damgası)
// Koşul, sıra, clamp bandı ([0,1]), satır eşlemesi (3=master 4=bgm 5=sfx
// 6=voice) ve no-op kolları birebirdir.
//
// TU-local sözleşme (D13 kilit-sırası + D14 guard ile çelişmez): bu yordamlar
// kilit ALMAZ, yeni kilit sırası kurmaz. Kazanç okuma/yazma AudioEngine'in
// kendi kilidinde gerçekleşir; withMixerVolumes yapısal-paylaşımlı ve
// kilitsizdir (rewind zinciri uzamaz, step ilerlemez).
// Yeni export YOK, `RowlEngine_` sembolü YOK (motor-içi Engine üyeleri).

#include "rowl/core/engine.hpp"

#include "rowl/audio/audio_engine.hpp"

#include <algorithm>
#include <cmath>

namespace Rowl::Core {

float Engine::pauseMenuVolume(int row) const {
    if (!m_audio) return 1.0f;
    switch (row) {
        case 3: return m_audio->getMasterVolume();
        case 4: return m_audio->getBgmVolume();
        case 5: return m_audio->getSfxVolume();
        case 6: return m_audio->getVoiceVolume();
        default: return 1.0f;
    }
}

void Engine::setPauseMenuVolume(int row, float volume) {
    if (!m_audio) return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    bool mixerRow = true;
    switch (row) {
        case 3: m_audio->setMasterVolume(volume); break;
        case 4: m_audio->setBgmVolume(volume); break;
        case 5: m_audio->setSfxVolume(volume); break;
        case 6: m_audio->setVoiceVolume(volume); break;
        default: mixerRow = false; break;
    }
    // #86: setter commit — slider tıklaması state'e damgalanır (step yok).
    if (mixerRow) commitMixerVolumesToGameState();
}

float Engine::pauseMenuFraction(int row) const {
    if (row >= 3 && row <= 6) return std::clamp(pauseMenuVolume(row), 0.0f, 1.0f);
    if (row == 7) return std::clamp((m_textSpeedMultiplier - 0.25f) / 3.75f, 0.0f, 1.0f);
    if (row == 8) return std::clamp(m_autoDelay / 10.0f, 0.0f, 1.0f);
    return -1.0f;
}

void Engine::pauseMenuSetFraction(int row, float fraction) {
    if (!std::isfinite(fraction)) return;
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (row >= 3 && row <= 6) {
        setPauseMenuVolume(row, std::round(fraction * 100.0f) / 100.0f);
    } else if (row == 7) {
        setTextSpeedMultiplier(0.25f + std::round(fraction * 15.0f) * 0.25f);  // the keys' 0.25 steps
    } else if (row == 8) {
        setAutoDelay(std::round(fraction * 20.0f) * 0.5f);                      // the keys' 0.5 s steps
    } else {
        return;
    }
    persistPauseMenuPreference(row);
    m_pauseConfirmQuit = false;
}

void Engine::sliderDragTo(float physicalX) {
    if (m_sliderDragRow < 0 || !m_window || !std::isfinite(physicalX)) return;
    float vx = 0.0f, vy = 0.0f;
    bool bezel = false;
    // A drag may leave the canvas sideways: clamp into it instead of dropping the event.
    m_window->mapPhysicalToVirtual(physicalX, 0.0f, 1920, 1080, vx, vy, bezel);
    if (bezel) vx = physicalX < static_cast<float>(m_window->getWidth()) * 0.5f ? 0.0f : 1920.0f;
    pauseMenuSetFraction(m_sliderDragRow, activeMenuLayout().sliderFractionAt(vx));
}

void Engine::pauseMenuAdjustSelected(int direction) {
    // D02: the preferences page maps its rows onto value rows 3..7 (D04-2: 8/9
    // for auto-delay / skip-mode, D05-2: 10..13 language / text size / contrast /
    // motion); the pause main page has none since D05-2.
    if (isExtrasMode(m_pauseMode)) {  // D07-2: gallery grid / viewer stepping
        extrasAdjust(direction);
        return;
    }
    if (m_pauseMode == PauseMenuMode::SaveSlots || m_pauseMode == PauseMenuMode::LoadSlots) {
        // T2b: Left / Right turn the slot page (a card grid steps along its line instead).
        if (activeMenuLayout().cards) slotCardsMove(direction, 0);
        else slotPageMove(direction);
        return;
    }
    if (m_pauseMode != PauseMenuMode::Main && m_pauseMode != PauseMenuMode::Preferences) return;
    const int row = pauseMenuValueRow();
    if (row >= 3 && row <= 6) {
        setPauseMenuVolume(row, pauseMenuVolume(row) + direction * 0.05f);
        persistPauseMenuPreference(row);
    } else if (row == 7) {
        setTextSpeedMultiplier(m_textSpeedMultiplier + direction * 0.25f);
        persistPauseMenuPreference(row);
    } else if (row == 8) {
        // D04-2: auto-advance delay, 0.5 s steps, clamped to [0, 10] by the setter.
        setAutoDelay(m_autoDelay + direction * 0.5f);
        persistPauseMenuPreference(row);
    } else if (row == 9) {
        // D04-2: skip mode cycles Okunan -> Hepsi -> Kapali (Left reverses).
        static constexpr SkipPolicy kCycle[] = {SkipPolicy::ReadOnly, SkipPolicy::All, SkipPolicy::Off};
        int index = 0;
        for (int i = 0; i < 3; ++i) {
            if (kCycle[i] == m_skipPolicy) index = i;
        }
        setSkipPolicy(kCycle[(index + (direction > 0 ? 1 : 2)) % 3]);
        persistPauseMenuPreference(row);
    } else if (row == PauseMenuLayout::kPreferenceLanguageRow + 3) {
        // D05-2: cycles the game's locales; persists itself on a real change.
        cyclePreferenceLanguage(direction);
    } else if (row == PauseMenuLayout::kPreferenceTextScaleRow + 3) {
        // D05-2: 100% -> 125% -> 150% (wraps, Left reverses), applied live.
        static constexpr float kScales[] = {1.0f, 1.25f, 1.5f};
        int index = 0;
        for (int i = 1; i < 3; ++i) {
            if (std::fabs(textScale() - kScales[i]) < std::fabs(textScale() - kScales[index])) index = i;
        }
        setTextScale(kScales[(index + (direction > 0 ? 1 : 2)) % 3]);
        persistPauseMenuPreference(row);
    } else if (row == PauseMenuLayout::kPreferenceContrastRow + 3) {
        setHighContrast(!highContrast());  // D05-2: either direction toggles
        persistPauseMenuPreference(row);
    } else if (row == PauseMenuLayout::kPreferenceMotionRow + 3) {
        setReducedMotion(!reducedMotion());
        persistPauseMenuPreference(row);
    }
    m_pauseConfirmQuit = false;
}

void Engine::commitMixerVolumesToGameState() {
    // #86: setter commit — canlı kazançları step ilerletmeden state'e
    // damgalar (withMixerVolumes: yapısal-paylaşım, rewind zinciri uzamaz).
    if (!m_audio || !m_gameState) return;
    m_gameState = Rowl::State::GameState::withMixerVolumes(
        m_gameState,
        m_audio->getMasterVolume(), m_audio->getBgmVolume(),
        m_audio->getSfxVolume(), m_audio->getVoiceVolume());
}

}  // namespace Rowl::Core
