// engine_backlog.cpp — D03-2: oyuncuda Geçmiş (backlog) ekranı.
//
// Backlog bir duraklatma-menüsü modudur (PauseMenuMode::Backlog): oyun
// duraklatma menüsündeki gibi donar, giriş yolu engine_pause_menu.cpp'nin
// komut/tıklama hunisinden geçer. Bu TU yalnız durum makinesinin liste
// kısmını taşır: değişken yükseklikli (sarılmış çok satırlı) girdilerin
// yerleşimi, seçimin görünür kalması, tıklama isabeti ve görünüm/JSON
// girdisi. Yerleşimi motor hesaplar (sarma FontRenderer::wrapText ile) ki
// tıklama isabeti ile çizim aynı geometriyi kullansın.
//
// Salt-okunur sözleşme: geçmiş, GameState, stepId, değişkenler, okundu işareti
// ve yuva dosyaları hiçbir yolda yazılmaz; ses yalnız replayBacklogVoice ile
// (D03-1) çalınır. TU-local: kilit ALMAZ. `RowlEngine_` sembolü YOK
// (C API sarmalayıcısı c_api_pause.cpp'de).

#include "rowl/core/engine.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/text/markup_parser.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Rowl::Core {

namespace {

using LM = PauseMenuLayout;

constexpr size_t kBacklogWrapInputBytes = 2048;  // unselected rows: never shape 64 KiB per frame
constexpr size_t kBacklogSelectedInputBytes = 64 * 1024;  // selected row: whole text, wrapped once
const std::string kChoiceMark = "> ";  // F1: marks the player's own answer row
constexpr size_t kFallbackLineCodepoints = 44;   // no font: coarse hard wrap

// UTF-8 safe prefix: never cuts inside a multi-byte sequence.
std::string utf8Prefix(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut);
}

// Font-less fallback (headless without a loaded font): hard wrap by codepoint
// count, newlines kept. Only the geometry matters there, nothing is drawn.
std::vector<std::string> fallbackWrap(const std::string& plain) {
    std::vector<std::string> lines;
    std::string current;
    size_t count = 0;
    for (size_t i = 0; i < plain.size();) {
        const unsigned char lead = static_cast<unsigned char>(plain[i]);
        size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
        len = std::min(len, plain.size() - i);
        if (plain[i] == '\n') {
            lines.push_back(current);
            current.clear();
            count = 0;
        } else {
            if (count >= kFallbackLineCodepoints) {
                lines.push_back(current);
                current.clear();
                count = 0;
            }
            current.append(plain, i, len);
            ++count;
        }
        i += len;
    }
    lines.push_back(current);
    return lines;
}

}  // namespace

Engine::BacklogCell Engine::backlogCell(size_t index) const {
    BacklogCell cell;
    const auto& history = getDialogueHistory();
    if (index >= history.size()) return cell;
    const auto& entry = history[index];

    Rowl::Render::FontRenderer* fonts = m_window ? m_window->getMenuFontRenderer() : nullptr;
    const bool haveFont = fonts && fonts->isLoaded();
    // Accessibility text scale grows glyphs (and so the line pitch) with it.
    const float scale = haveFont ? std::max(1.0f, fonts->textScale()) : 1.0f;
    const float lineH = LM::kBacklogLineH * scale;
    // F1: the player's own answer is a "> option" row without a speaker.
    const float speakerH = entry.speaker.empty() ? 0.0f : lineH;
    const std::string text = entry.choice ? kChoiceMark + entry.dialogue : entry.dialogue;

    // An entry never outgrows the list area (large text scale / long line).
    const float areaH = LM::kBacklogY1 - LM::kBacklogY0;
    const int fit = std::max(1, static_cast<int>((areaH - 2.0f * LM::kBacklogPadY - speakerH) / lineH));

    const bool selected =
        getDialogueHistory().size() > 0 &&
        static_cast<int>(index) == std::clamp(m_pauseSelected, 0, static_cast<int>(history.size()) - 1);

    if (selected) {
        // A-5: the selected row is shown in full (wrapped once, memoized); a
        // text longer than the area scrolls inside the row.
        const uint64_t identity = m_gameState ? m_gameState->dialogueHistory.identity() : 0;
        auto& cache = m_backlogWrapCache;
        if (!cache.valid || cache.identity != identity || cache.index != static_cast<int>(index) ||
            cache.locale != m_localization.getLocale() || cache.scale != scale ||
            cache.haveFont != haveFont) {
            const std::string input = utf8Prefix(text, kBacklogSelectedInputBytes);
            cache.lines = haveFont
                ? fonts->wrapText(input, LM::kBacklogFontPx, LM::kBacklogTextW)
                : fallbackWrap(Rowl::Text::stripMarkup(input));
            if (cache.lines.empty()) cache.lines.emplace_back();
            cache.identity = identity;
            cache.index = static_cast<int>(index);
            cache.locale = m_localization.getLocale();
            cache.scale = scale;
            cache.haveFont = haveFont;
            cache.valid = true;
        }
        const size_t total = cache.lines.size();
        cell.totalLines = total;
        cell.fitLines = static_cast<size_t>(fit);
        const size_t maxOffset = total > cell.fitLines ? total - cell.fitLines : 0;
        const size_t offset = std::min(static_cast<size_t>(std::max(0, m_backlogLineOffset)), maxOffset);
        m_backlogLineOffset = static_cast<int>(offset);
        const size_t end = std::min(total, offset + cell.fitLines);
        cell.lines.assign(cache.lines.begin() + static_cast<std::ptrdiff_t>(offset),
                          cache.lines.begin() + static_cast<std::ptrdiff_t>(end));
        if (offset > 0) cell.lines.front().insert(0, "... ");
        if (end < total) cell.lines.back() += " ...";
    } else {
        const std::string input = utf8Prefix(text, kBacklogWrapInputBytes);
        cell.lines = haveFont
            ? fonts->wrapText(input, LM::kBacklogFontPx, LM::kBacklogTextW)
            : fallbackWrap(Rowl::Text::stripMarkup(input));
        if (cell.lines.empty()) cell.lines.emplace_back();
        cell.totalLines = cell.lines.size();
        const size_t maxLines = static_cast<size_t>(std::min(LM::kBacklogMaxLines, fit));
        const bool cut = cell.lines.size() > maxLines || input.size() < text.size();
        if (cell.lines.size() > maxLines) cell.lines.resize(maxLines);
        if (cut) cell.lines.back() += " ...";
        cell.fitLines = cell.lines.size();
    }

    cell.height = 2.0f * LM::kBacklogPadY + speakerH +
                  static_cast<float>(cell.lines.size()) * lineH;
    return cell;
}

void Engine::backlogEnsureVisible() const {
    const int total = static_cast<int>(getDialogueHistory().size());
    if (total <= 0) {
        m_backlogTop = 0;
        return;
    }
    const int sel = std::clamp(m_pauseSelected, 0, total - 1);
    // Lowest first-visible index that still shows the selection: grow upwards
    // from it until the list area is full (bounded by the visible count).
    const float areaH = LM::kBacklogY1 - LM::kBacklogY0;
    int lowest = sel;
    float used = backlogCell(static_cast<size_t>(sel)).height;
    while (lowest > 0) {
        const float h = backlogCell(static_cast<size_t>(lowest - 1)).height;
        if (used + LM::kBacklogGap + h > areaH) break;
        used += LM::kBacklogGap + h;
        --lowest;
    }
    m_backlogTop = std::max(std::clamp(m_backlogTop, 0, sel), lowest);
}

PauseBacklogView Engine::buildBacklogView() const {
    PauseBacklogView view;
    view.active = true;
    view.voiceMark = uiText(UiText::VoiceMark, getUiLanguage());
    const auto& history = getDialogueHistory();
    const int total = static_cast<int>(history.size());
    view.total = total;
    if (total == 0) {
        m_backlogTop = 0;
        view.counter = "0 / 0";
        return view;
    }
    const int sel = std::clamp(m_pauseSelected, 0, total - 1);
    view.selected = sel;
    backlogEnsureVisible();
    view.scroll = m_backlogTop;
    view.counter = std::to_string(sel + 1) + " / " + std::to_string(total);

    float y = LM::kBacklogY0;
    for (int i = m_backlogTop; i < total; ++i) {
        BacklogCell cell = backlogCell(static_cast<size_t>(i));
        if (!view.entries.empty() && y + cell.height > LM::kBacklogY1) break;
        PauseBacklogEntryView entry;
        entry.index = i;
        entry.y = y;
        entry.h = cell.height;
        entry.speaker = history[static_cast<size_t>(i)].speaker;
        entry.dialogue = history[static_cast<size_t>(i)].dialogue;
        entry.lines = std::move(cell.lines);
        entry.hasVoice = !history[static_cast<size_t>(i)].voice.empty();
        entry.choice = history[static_cast<size_t>(i)].choice;
        entry.selected = (i == sel);
        y += entry.h + LM::kBacklogGap;
        view.entries.push_back(std::move(entry));
    }
    return view;
}

bool Engine::openBacklog() {
    if (!m_initialized) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot open the backlog: engine is not initialized",
                                "open_backlog", "");
        }
        return false;
    }
    if (m_titleActive) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot open the backlog on the title screen",
                                "open_backlog", "");
        }
        return false;
    }
    if (isBacklogOpen()) return true;
    const bool wasPaused = m_paused;
    if (!wasPaused) setPaused(true);  // freezes the story, resets the menu state
    m_backlogFromPause = wasPaused;   // Back returns to the pause menu then
    m_pauseMode = PauseMenuMode::Backlog;
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    m_pauseSelected = std::max<int>(0, static_cast<int>(getDialogueHistory().size()) - 1);
    m_backlogTop = 0;
    m_backlogLineOffset = 0;
    backlogEnsureVisible();  // newest row at the bottom, as many above as fit
    ROWL_LOG_INFO("[Player] Backlog open (" + std::to_string(getDialogueHistory().size()) +
                  " rows).");
    return true;
}

void Engine::backlogMoveSelection(int direction) {
    const int total = static_cast<int>(getDialogueHistory().size());
    if (total <= 0) return;
    // A-5: a long selected row scrolls inside itself before the selection moves.
    {
        const BacklogCell cell = backlogCell(static_cast<size_t>(std::clamp(m_pauseSelected, 0, total - 1)));
        const int hidden = static_cast<int>(cell.totalLines) - static_cast<int>(cell.fitLines);
        if (direction > 0 && m_backlogLineOffset < hidden) {
            ++m_backlogLineOffset;
            return;
        }
        if (direction < 0 && m_backlogLineOffset > 0) {
            --m_backlogLineOffset;
            return;
        }
    }
    const int before = std::clamp(m_pauseSelected, 0, total - 1);
    m_pauseSelected = std::clamp(before + direction, 0, total - 1);
    if (m_pauseSelected != before) m_backlogLineOffset = 0;
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    backlogEnsureVisible();
}

void Engine::backlogScroll(float deltaY) {
    if (deltaY == 0.0f || !std::isfinite(deltaY)) return;
    const int total = static_cast<int>(getDialogueHistory().size());
    if (deltaY > 0.0f) {  // wheel up: towards older rows
        backlogMoveSelection(-1);
        return;
    }
    // Wheel down: newer rows; at the newest row (or an empty history) it
    // closes the screen — the visual-novel convention.
    if (total == 0 || m_pauseSelected >= total - 1) backlogClose();
    else backlogMoveSelection(+1);
}

void Engine::backlogActivateSelected() {
    const auto& history = getDialogueHistory();
    if (history.empty()) return;
    const size_t index = static_cast<size_t>(std::clamp(m_pauseSelected, 0,
                                                        static_cast<int>(history.size()) - 1));
    // A voiceless row does nothing; the replay itself is a pure audio action.
    if (!history[index].voice.empty()) replayBacklogVoice(index);
}

void Engine::backlogClose() {
    // F1 (karar 6): a clip replayed from the backlog stops when the page
    // closes; the node's own voice (cut when the replay started) stays silent.
    if (m_backlogReplayActive) {
        m_backlogReplayActive = false;
        if (m_audio) m_audio->stopVoice();
    }
    if (m_backlogFromPause) {
        m_backlogFromPause = false;
        m_pauseMode = PauseMenuMode::Main;
        m_pauseSelected = PauseMenuLayout::kMainBacklogRow;
        m_pauseConfirmQuit = false;
        m_backlogTop = 0;
        return;
    }
    setPaused(false);  // straight back to the story
}

bool Engine::backlogClick(float virtualX, float virtualY) {
    if (virtualX < LM::kRowX || virtualX > LM::kRowX + LM::kRowW) return false;
    const PauseBacklogView view = buildBacklogView();
    for (const auto& entry : view.entries) {
        if (virtualY >= entry.y && virtualY <= entry.y + entry.h) {
            if (m_pauseSelected != entry.index) m_backlogLineOffset = 0;
            m_pauseSelected = entry.index;
            backlogActivateSelected();
            return true;
        }
    }
    return false;  // gaps and empty space keep the selection
}

}  // namespace Rowl::Core
