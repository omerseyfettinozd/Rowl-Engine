// engine_pause_menu.cpp — W8-d(1): pause-menu state-machine + view/JSON sunumu.
//
// engine.cpp'den birebir tasinmistir (D15 deseni):
//   2965-2978  Engine::setPaused
//   2980-2990  Engine::pauseMenuRowCount / pauseMenuMoveSelection
//   2992-3005  Engine::menuChooseSlot
//   3007-3031  Engine::menuActivateSelected
//   3033-3045  Engine::menuBack
//   3047-3057  Engine::pauseMenuCommand
//   3059-3078  Engine::pauseMenuClick
//   3080-3089  pauseMenuPercent / pauseMenuSpeedText (TU-local yardimcilar)
//   3091-3136  Engine::getPauseMenuView
//   3138-3155  appendPauseMenuJsonString (TU-local JSON kacisi)
//   3157-3186  Engine::getPauseMenuJson
// Kosul, sira, metinler ve JSON alan sirasi birebirdir. D15'in aldigi
// mixer-dortlusu engine_pause_mixer.cpp'de kaldi; bu TU yalnizca kalan
// kohezif kumeyi tasir. PauseMenuLayout::rowAt/adjustDirection zaten ayrik.
// TU-local sozlesme: bu yordamlar kilit ALMAZ (D13/D14 ile celismez).
// Yeni export YOK, `RowlEngine_` sembolu YOK (motor-ici Engine uyeleri).

#include "rowl/core/engine.hpp"

#include "rowl/core/logger.hpp"

#include <cstdio>
#include <string>

namespace Rowl::Core {

void Engine::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    // Opening always lands on a predictable main page; closing resumes.
    // Pausing never quits — exit requires the two-step menu confirmation.
    m_pauseMode = PauseMenuMode::Main;
    m_pauseSelected = 0;
    m_pauseConfirmQuit = false;
    // A2b: a fresh menu open rebuilds slot occupancy — saves or deletes may
    // have happened out-of-band while unpaused (editor, quick keys).
    m_pauseSlotCacheValid = false;
    ROWL_LOG_INFO(paused ? "[Player] Paused — menu open (Esc/P to resume)."
                         : "[Player] Resumed.");
}

int Engine::pauseMenuRowCount() const {
    return m_pauseMode == PauseMenuMode::Main ? PauseMenuLayout::kMainRows
                                             : PauseMenuLayout::kSlotRows;
}

void Engine::pauseMenuMoveSelection(int direction) {
    const int count = pauseMenuRowCount();
    if (count <= 0) return;
    m_pauseSelected = (m_pauseSelected + direction + count) % count;
    m_pauseConfirmQuit = false;
}

void Engine::menuChooseSlot(int32_t slotIndex) {
    if (m_pauseMode == PauseMenuMode::Main) return;
    if (slotIndex < kPauseMenuQuickSlotMin || slotIndex > kPauseMenuQuickSlotMax) return;
    if (m_pauseMode == PauseMenuMode::SaveSlots) {
        if (saveGameSlot(slotIndex)) {
            ROWL_LOG_INFO("[Player] Saved to slot #" + std::to_string(slotIndex) + " from pause menu.");
        }
    } else {
        if (loadGameSlot(slotIndex)) {
            ROWL_LOG_INFO("[Player] Loaded slot #" + std::to_string(slotIndex) + " from pause menu.");
        }
    }
    // Stay paused on the slot page so occupancy refreshes visibly.
}

void Engine::menuActivateSelected() {
    if (!m_paused) return;
    if (m_pauseMode != PauseMenuMode::Main) {
        menuChooseSlot(static_cast<int32_t>(m_pauseSelected));
        return;
    }
    switch (m_pauseSelected) {
        case 0: setPaused(false); break;
        case 1: m_pauseMode = PauseMenuMode::SaveSlots; m_pauseSelected = 0; m_pauseConfirmQuit = false; break;
        case 2: m_pauseMode = PauseMenuMode::LoadSlots; m_pauseSelected = 0; m_pauseConfirmQuit = false; break;
        case 8:
            if (m_pauseConfirmQuit) {
                ROWL_LOG_INFO("[Player] Exit confirmed from pause menu.");
                m_isRunning = false;
            } else {
                m_pauseConfirmQuit = true;
            }
            break;
        default:
            // Value rows (3-7) have no activation of their own; keyboard
            // Confirm still steps them up like Right for parity with clicks.
            pauseMenuAdjustSelected(+1);
            break;
    }
}

void Engine::menuBack() {
    if (!m_paused) return;
    if (m_pauseConfirmQuit) {
        m_pauseConfirmQuit = false;
        return;
    }
    if (m_pauseMode != PauseMenuMode::Main) {
        m_pauseMode = PauseMenuMode::Main;
        m_pauseSelected = 0;
        return;
    }
    setPaused(false);
}

void Engine::pauseMenuCommand(PauseMenuCommand command) {
    if (!m_paused) return;
    switch (command) {
        case PauseMenuCommand::Up: pauseMenuMoveSelection(-1); break;
        case PauseMenuCommand::Down: pauseMenuMoveSelection(+1); break;
        case PauseMenuCommand::Left: pauseMenuAdjustSelected(-1); break;
        case PauseMenuCommand::Right: pauseMenuAdjustSelected(+1); break;
        case PauseMenuCommand::Back: menuBack(); break;
        case PauseMenuCommand::Confirm: menuActivateSelected(); break;
    }
}

void Engine::pauseMenuClick(float virtualX, float virtualY) {
    if (!m_paused) return;
    const int row = PauseMenuLayout::rowAt(virtualX, virtualY, pauseMenuRowCount());
    if (row < 0) return; // gaps and outside miss; selection is kept
    const bool alreadySelected = (row == m_pauseSelected);
    m_pauseSelected = row;
    if (m_pauseMode == PauseMenuMode::Main && row >= 3 && row <= 7) {
        const int dir = PauseMenuLayout::adjustDirection(virtualX);
        if (dir != 0) {
            pauseMenuAdjustSelected(dir);
            return;
        }
        // Middle band only selects (a second tap confirms).
        if (!alreadySelected) {
            m_pauseConfirmQuit = false;
            return;
        }
    }
    menuActivateSelected();
}

static std::string pauseMenuPercent(float volume) {
    return std::to_string(static_cast<int>(volume * 100.0f + 0.5f)) + "%";
}

static std::string pauseMenuSpeedText(float multiplier) {
    const int quarters = static_cast<int>(multiplier * 4.0f + 0.5f);
    const int whole = quarters / 4;
    const int frac = (quarters % 4) * 25;
    return std::to_string(whole) + "." + (frac < 10 ? "0" : "") + std::to_string(frac) + "x";
}

PauseMenuView Engine::getPauseMenuView() const {
    PauseMenuView view;
    view.open = m_paused;
    if (!m_paused) return view;
    view.selected = m_pauseSelected;
    if (m_pauseMode == PauseMenuMode::Main) {
        view.title = "Duraklatildi";
        view.rows = {
            {"Devam Et", "", false},
            {"Oyunu Kaydet", "yuva sec >", false},
            {"Oyunu Yukle", "yuva sec >", false},
            {"Ana Ses", pauseMenuPercent(pauseMenuVolume(3)), true},
            {"Muzik", pauseMenuPercent(pauseMenuVolume(4)), true},
            {"SFX", pauseMenuPercent(pauseMenuVolume(5)), true},
            {"Seslendirme", pauseMenuPercent(pauseMenuVolume(6)), true},
            {"Metin Hizi", pauseMenuSpeedText(m_textSpeedMultiplier), true},
            {"Cikis", m_pauseConfirmQuit ? "emin misin?" : "", false},
        };
        view.hint = m_pauseConfirmQuit
            ? "ENTER: cikisi onayla   ESC: vazgec"
            : "YUKARI/ASAGI: sec   SOL/SAG: ayar   ENTER: tamam   ESC: devam et";
    } else {
        const bool saving = (m_pauseMode == PauseMenuMode::SaveSlots);
        view.title = saving ? "Kayit Yuvasi Sec" : "Yukleme Yuvasi Sec";
        // A2b: occupancy is memoized (see m_pauseSlotCacheValid) — a slot
        // page holds 10 rows and this view rebuilds per UI refresh.
        static_assert(kPauseMenuQuickSlotMax - kPauseMenuQuickSlotMin + 1 ==
                      10, "pause slot cache assumes the 0..9 quick-slot window");
        if (!m_pauseSlotCacheValid) {
            for (int32_t slot = kPauseMenuQuickSlotMin; slot <= kPauseMenuQuickSlotMax; ++slot) {
                m_pauseSlotPresent[static_cast<size_t>(slot - kPauseMenuQuickSlotMin)] =
                    hasSaveSlot(slot);
            }
            m_pauseSlotCacheValid = true;
        }
        for (int32_t slot = kPauseMenuQuickSlotMin; slot <= kPauseMenuQuickSlotMax; ++slot) {
            PauseMenuRow row;
            row.label = "Yuva " + std::to_string(slot);
            row.value = m_pauseSlotPresent[static_cast<size_t>(slot - kPauseMenuQuickSlotMin)]
                ? "dolu" : "bos";
            view.rows.push_back(row);
        }
        view.hint = "ENTER/tik: sec   0-9: dogrudan sec   ESC: geri";
    }
    return view;
}

static void appendPauseMenuJsonString(std::string& out, const std::string& text) {
    out.push_back('"');
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

std::string Engine::getPauseMenuJson() const {
    const PauseMenuView view = getPauseMenuView();
    std::string out = "{\"open\":";
    out += view.open ? "true" : "false";
    out += ",\"mode\":";
    out += std::to_string(static_cast<int>(m_pauseMode));
    out += ",\"selected\":";
    out += std::to_string(view.selected);
    out += ",\"confirm_quit\":";
    out += m_pauseConfirmQuit ? "true" : "false";
    out += ",\"quick_slot\":";
    out += std::to_string(m_activeQuickSlot);
    out += ",\"title\":";
    appendPauseMenuJsonString(out, view.title);
    out += ",\"hint\":";
    appendPauseMenuJsonString(out, view.hint);
    out += ",\"rows\":[";
    for (size_t i = 0; i < view.rows.size(); ++i) {
        if (i > 0) out.push_back(',');
        out += "{\"label\":";
        appendPauseMenuJsonString(out, view.rows[i].label);
        out += ",\"value\":";
        appendPauseMenuJsonString(out, view.rows[i].value);
        out += ",\"selected\":";
        out += (static_cast<int>(i) == view.selected) ? "true" : "false";
        out.push_back('}');
    }
    out += "]}";
    return out;
}

}  // namespace Rowl::Core
