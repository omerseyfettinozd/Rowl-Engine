#pragma once

#include "rowl/c_api.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace Rowl::Player {

/// D06-3: the packaged player's asset-prefetch driver. Header-only, built on
/// the exported C API alone, so the standalone player (linked with hidden
/// visibility - it only sees ROWL_API symbols) and the tests share ONE
/// definition, the same pattern as state/profile_language.hpp.
///
/// Prefetch is a warm-up and nothing else: the driver only reads the engine
/// (current node / chapter), asks it to read upcoming asset files through the
/// VFS and pumps that queue within a small per-frame budget. It never touches
/// story state, saves, history or audio, so the real play flow is unchanged
/// with or without it. Every failure is logged and swallowed - a missing
/// asset, a stale chapter split or a refused call never stops the game.
///
/// Thread: everything runs on the host update thread (the player calls
/// onFrame from the engine loop's frame callback). No threads are started.
class ChapterPrefetchDriver {
public:
    using LogSink = std::function<void(const std::string&)>;

    /// What the driver did, for logs and tests.
    struct Stats {
        bool attached = false;
        /// The loader was fed from the VFS chapter files (index + chapters).
        bool chaptersFed = false;
        int32_t fedFiles = 0;
        /// Prefetch windows accepted by PrefetchChapterAssets.
        uint32_t triggers = 0;
        /// Windows the engine refused (logged once each).
        uint32_t refusedTriggers = 0;
        uint32_t pumpCalls = 0;
        uint64_t pumpedAssets = 0;
    };

    /// `pumpMilliseconds` is the per-frame pump budget (default 2 ms; the
    /// engine clamps it to its own 50 ms ceiling).
    explicit ChapterPrefetchDriver(float pumpMilliseconds = 2.0F, LogSink log = {})
        : m_pumpMilliseconds(pumpMilliseconds), m_log(std::move(log)) {}

    /// Call once after the story graph is loaded. Feeds the chapter loader
    /// from the VFS when the project has a split chapter set
    /// (json/chapters/chapter_index.json); otherwise prefetch uses the graph's
    /// own chapters / the implicit single chapter. Returns the feed result
    /// code (OK, FILE_NOT_FOUND for a project without split chapters, or the
    /// reason the chapter files were ignored).
    RowlEngine_ResultCode attach(RowlEngineHandle handle) {
        m_stats = Stats{};
        m_stats.attached = true;
        m_active = false;
        m_lastNode = 0;
        m_lastChapter.clear();
        m_haveLastChapter = false;
        int32_t fed = 0;
        const RowlEngine_ResultCode code =
            m_feedEnabled ? RowlEngine_FeedChaptersFromVfs(handle, nullptr, &fed)
                          : ROWL_RESULT_FILE_NOT_FOUND;
        m_stats.chaptersFed = code == ROWL_RESULT_OK && fed > 0;
        m_stats.fedFiles = fed;
        if (code == ROWL_RESULT_OK) {
            note("chapters fed from the VFS (" + std::to_string(fed) + " files)");
        } else if (code == ROWL_RESULT_FILE_NOT_FOUND) {
            note("no split chapter files; prefetch follows the story graph's own chapters");
        } else {
            note("chapter files ignored (result " + std::to_string(static_cast<int>(code)) +
                 "); prefetch follows the story graph's own chapters");
        }
        return code;
    }

    /// Per-frame hook. Starts a new prefetch window when the story moved on,
    /// then pumps the queue for at most the configured budget.
    void onFrame(RowlEngineHandle handle) {
        if (!m_stats.attached) return;
        try {
            const uint64_t node = RowlEngine_GetCurrentNodeId(handle);
            if (node != m_lastNode) {
                m_lastNode = node;
                startWindowIfNeeded(handle);
            }
            if (m_active) pump(handle);
        } catch (const std::exception& error) {
            note(std::string("prefetch frame skipped: ") + error.what());
        } catch (...) {
            note("prefetch frame skipped: unknown error");
        }
    }

    const Stats& stats() const noexcept { return m_stats; }

    /// True while a prefetch queue is still being worked through.
    bool pumping() const noexcept { return m_active; }

    /// Latest progress snapshot (PrefetchProgress JSON), empty object when
    /// unavailable. For logs and tests.
    static nlohmann::json progress(RowlEngineHandle handle) {
        return parseObject(queryText(handle, RowlEngine_GetPrefetchProgressJson));
    }

    /// Test seam: disable the chapter feed (attach reports FILE_NOT_FOUND
    /// without touching the loader). Production never calls this.
    void setFeedEnabledForTest(bool enabled) noexcept { m_feedEnabled = enabled; }

    /// The frame callback the player hands to RowlEngine_RunWithFrameCallback
    /// (userData = the driver).
    static void frameCallback(RowlEngineHandle handle, float, void* userData) {
        if (userData == nullptr) return;
        static_cast<ChapterPrefetchDriver*>(userData)->onFrame(handle);
    }

private:
    template <typename Query>
    static std::string queryText(RowlEngineHandle handle, Query query) {
        uint32_t required = 0;
        if (query(handle, nullptr, 0, &required) != ROWL_RESULT_OK || required == 0) return {};
        std::string buffer(required, '\0');
        if (query(handle, buffer.data(), required, &required) != ROWL_RESULT_OK ||
            required == 0) {
            return {};
        }
        buffer.resize(required - 1);
        return buffer;
    }

    static nlohmann::json parseObject(const std::string& text) {
        nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
        return parsed.is_object() ? parsed : nlohmann::json::object();
    }

    void note(const std::string& message) const {
        if (m_log) m_log("[Player] Prefetch: " + message);
    }

    /// A chapter is the prefetch unit (the window is "this chapter + the next
    /// one"), so a chaptered story starts a window when the chapter changes -
    /// re-reading the same window on every node would only repeat the I/O.
    /// A story without chapters has no such unit: the window is the current
    /// node and its successors, refreshed on every node change.
    void startWindowIfNeeded(RowlEngineHandle handle) {
        const std::string chapter = queryText(handle, RowlEngine_GetCurrentChapterIdUtf8);
        if (!chapter.empty() && m_haveLastChapter && chapter == m_lastChapter) return;
        m_lastChapter = chapter;
        m_haveLastChapter = true;

        const RowlEngine_ResultCode code = RowlEngine_PrefetchChapterAssets(
            handle, chapter.empty() ? nullptr : chapter.c_str(), 0);
        if (code == ROWL_RESULT_OK) {
            ++m_stats.triggers;
            m_active = true;
        } else {
            ++m_stats.refusedTriggers;
            m_active = false;
            note("window refused for chapter '" + chapter + "' (result " +
                 std::to_string(static_cast<int>(code)) + "); the game plays on");
        }
    }

    void pump(RowlEngineHandle handle) {
        const int newlyReady = RowlEngine_PumpPrefetch(handle, m_pumpMilliseconds);
        ++m_stats.pumpCalls;
        if (newlyReady > 0) m_stats.pumpedAssets += static_cast<uint64_t>(newlyReady);
        if (newlyReady > 0) return;
        // Nothing newly ready: either the queue is finished, it stalled on the
        // byte budget (the asset stays queued by design), or only missing
        // assets were consumed. Stop pumping a queue that cannot advance.
        const nlohmann::json snapshot = progress(handle);
        if (snapshot.value("complete", true)) {
            m_active = false;
            return;
        }
        const std::string diagnostic = snapshot.value("last_diagnostic", std::string());
        if (diagnostic.rfind("prefetch paused", 0) == 0) {
            m_active = false;
            note(diagnostic);
        }
    }

    float m_pumpMilliseconds;
    LogSink m_log;
    Stats m_stats;
    bool m_feedEnabled = true;
    bool m_active = false;
    uint64_t m_lastNode = 0;
    std::string m_lastChapter;
    bool m_haveLastChapter = false;
};

}  // namespace Rowl::Player
