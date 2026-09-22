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

void Engine::pauseMenuAdjustSelected(int direction) {
    if (m_pauseMode != PauseMenuMode::Main) return;
    const int row = m_pauseSelected;
    if (row >= 3 && row <= 6) {
        setPauseMenuVolume(row, pauseMenuVolume(row) + direction * 0.05f);
    } else if (row == 7) {
        setTextSpeedMultiplier(m_textSpeedMultiplier + direction * 0.25f);
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
