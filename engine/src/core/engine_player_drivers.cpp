// engine_player_drivers.cpp — D04-1: the player's Auto and Skip drivers.
//
// Both drivers advance the story through Engine::advanceToNextNode, i.e.
// exactly the path of a manual Space/click, so node-entry effects (variables,
// sfx, scripts, transitions, history, read marks) behave as in normal play.
// They are separate from the author's per-line auto_advance flag, which keeps
// its own credit (m_autoAdvanceElapsed) in Engine::step.

#include "rowl/core/engine.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace Rowl::Core {

namespace {

constexpr float kSkipStepIntervalSeconds = 0.05f;
constexpr float kAutoWaitMin = 0.8f;
constexpr float kAutoWaitMax = 30.0f;
constexpr float kAutoSecondsPerChar = 0.04f;
constexpr float kAutoDelayMax = 10.0f;

const char* skipPolicyName(SkipPolicy policy) {
    switch (policy) {
        case SkipPolicy::Off: return "off";
        case SkipPolicy::All: return "all";
        case SkipPolicy::ReadOnly: break;
    }
    return "read_only";
}

// UTF-8 code points (not bytes): continuation bytes 10xxxxxx are not counted.
size_t utf8CodePoints(const std::string& text) {
    size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

}  // namespace

void Engine::resetAutoWait() {
    m_autoWaitElapsed = 0.0f;
    m_autoWaitRemaining = 0.0f;
}

void Engine::setAutoMode(bool enabled) {
    m_autoMode = enabled;
    resetAutoWait();
}

void Engine::toggleAutoMode() {
    setAutoMode(!m_autoMode);
    nlohmann::json patch = nlohmann::json::object();
    patch["auto_enabled"] = m_autoMode;
    m_playerProfile.setPreferencesJson(patch.dump());
}

bool Engine::setSkipActive(bool active) {
    if (!active) {
        m_skipActive = false;
        return true;
    }
    if (m_skipPolicy == SkipPolicy::Off || m_paused) return false;
    if (!m_skipActive) m_skipStepCredit = kSkipStepIntervalSeconds;  // first step acts at once
    m_skipActive = true;
    resetAutoWait();
    return true;
}

void Engine::setSkipPolicy(SkipPolicy policy) {
    m_skipPolicy = policy;
    if (policy == SkipPolicy::Off) {
        m_skipActive = false;
        m_skipHold = false;
    }
}

void Engine::setAutoDelay(float seconds) {
    if (!std::isfinite(seconds)) {
        ROWL_LOG_WARN("Non-finite auto delay rejected, keeping last value");
        return;
    }
    m_autoDelay = std::clamp(seconds, 0.0f, kAutoDelayMax);
}

void Engine::refreshReadSnapshot() {
    // Fail-closed: a line without a content id (legacy payload) or one the
    // profile has not recorded counts as unread. Empty lines carry nothing to
    // read, so they never decide. No lines at all: nothing to skip over.
    bool allRead = true;
    for (const auto& dialogue : m_activeDialogues) {
        if (dialogue.dialogue.empty()) continue;
        if (dialogue.contentId.empty() || !m_playerProfile.isRead(dialogue.contentId)) {
            allRead = false;
            break;
        }
    }
    m_activeLinesReadBeforePresentation = allRead;
}

float Engine::autoWaitSeconds() const {
    size_t chars = 0;
    for (const auto& dialogue : m_activeDialogues) chars += utf8CodePoints(dialogue.dialogue);
    const float wait = m_autoDelay +
        static_cast<float>(chars) * kAutoSecondsPerChar / std::max(m_textSpeedMultiplier, 0.25f);
    return std::clamp(wait, kAutoWaitMin, kAutoWaitMax);
}

bool Engine::isSkipping() const {
    if (!isSkipEffective() || !m_isPlaying || !m_activeChoiceButtons.empty()) return false;
    const StoryNode* node = m_storyRuntime.currentNode();
    if (!node || node->nextNodes.empty()) return false;
    return m_skipPolicy == SkipPolicy::All ||
           (m_skipPolicy == SkipPolicy::ReadOnly && m_activeLinesReadBeforePresentation);
}

void Engine::playDeferredEntrySounds() {
    // F1 (A-6): Skip muted the node's sfx / voice clip on entry. When Skip
    // stops ON that node (unread line, choice menu, end of chain, key
    // released) its sound plays once, now. Skip moving on drops the deferral.
    if (m_deferredEntryNodeId == 0) return;
    if (m_deferredEntryNodeId != m_storyRuntime.currentNodeId()) {
        m_deferredEntryNodeId = 0;
        m_deferredEntrySfx.clear();
        m_deferredEntryVoice = false;
        return;
    }
    if (m_paused || isSkipping()) return;
    if (m_audio) {
        for (const auto& sfx : m_deferredEntrySfx) {
            m_audio->playAudio(sfx, Rowl::Audio::AudioChannelType::Sfx);
        }
        if (m_deferredEntryVoice && !m_activeNodeVoiceTrack.empty()) {
            m_audio->stopVoice();
            m_audio->playAudio(m_activeNodeVoiceTrack, Rowl::Audio::AudioChannelType::Voice);
            m_voiceClipNodeId = m_deferredEntryNodeId;
        }
    }
    m_deferredEntryNodeId = 0;
    m_deferredEntrySfx.clear();
    m_deferredEntryVoice = false;
}

void Engine::stepPlayerDrivers(float deltaTime, bool advancedThisStep) {
    // ---- Skip (takes priority over Auto) -----------------------------------
    // At most one node per step and at least kSkipStepIntervalSeconds between
    // two nodes.
    bool advancedNow = advancedThisStep;
    if (isSkipEffective() && m_isPlaying && !advancedThisStep) {
        if (!isSkipping()) {
            // Choice menu, chain end or an unread line under read_only.
            m_skipActive = false;
        } else {
            m_skipStepCredit += deltaTime;
            if (m_skipStepCredit >= kSkipStepIntervalSeconds - 1e-6f) {
                m_skipStepCredit = 0.0f;
                const uint64_t nodeBefore = m_storyRuntime.currentNodeId();
                const uint64_t stepBefore = getCurrentStepId();
                completeTypewriterIfTyping();
                advanceToNextNode();
                advancedNow = true;
                if (m_storyRuntime.currentNodeId() == nodeBefore && getCurrentStepId() == stepBefore) {
                    m_skipActive = false;  // the advance did not happen: end of the line
                }
            }
        }
    }
    if (!isSkipEffective()) m_skipStepCredit = kSkipStepIntervalSeconds;

    // ---- Auto --------------------------------------------------------------
    if (!m_autoMode || isSkipEffective() || advancedNow) {
        resetAutoWait();
        return;
    }
    const StoryNode* node = m_storyRuntime.currentNode();
    const bool armed = m_isPlaying && m_activeChoiceButtons.empty() && node &&
        !node->nextNodes.empty() && areActiveDialoguesComplete();
    if (!armed) {
        resetAutoWait();  // a choice keeps Auto on but idle; the next node starts fresh
        return;
    }
    // Same #61 rule as the author driver: a running transition freezes the
    // credit instead of wiping or growing it.
    if (m_window && m_window->isTransitionActive()) return;

    m_autoWaitElapsed += deltaTime;
    const float wait = autoWaitSeconds();
    m_autoWaitRemaining = std::max(0.0f, wait - m_autoWaitElapsed);
    if (m_autoWaitElapsed < wait) return;
    // The node's own voice clip is waited out too, but never past the cap (a
    // stuck "playing" flag on a deviceless run must not freeze Auto).
    const bool voicePlaying = !m_activeNodeVoiceTrack.empty() &&
        m_voiceClipNodeId == m_storyRuntime.currentNodeId() &&
        m_audio && m_audio->isVoicePlaying();
    if (voicePlaying && m_autoWaitElapsed < kAutoWaitMax) return;
    advanceToNextNode();  // resets the credit
}

std::string Engine::getPlayerDriverStateJson() const {
    nlohmann::json state = nlohmann::json::object();
    state["auto"] = m_autoMode;
    state["skip_active"] = m_skipActive;
    state["skipping"] = isSkipping();  // F1 (A-7): really skipping (the badge)
    state["skip_policy"] = skipPolicyName(m_skipPolicy);
    state["auto_delay"] = m_autoDelay;
    state["line_read_before"] = m_activeLinesReadBeforePresentation;
    state["auto_wait_remaining"] = m_autoWaitRemaining;
    return state.dump();
}

}  // namespace Rowl::Core
