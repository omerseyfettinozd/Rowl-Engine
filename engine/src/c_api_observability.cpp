#include "c_api_internal.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/vfs/vfs.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <atomic>

static std::atomic<uint64_t> s_worldQuerySeq{1};

extern "C" {

ROWL_API void RowlEngine_SetLogCallback(RowlEngine_LogCallbackFn callback) {
    Rowl::Core::Logger::setLogCallback(callback);
}

ROWL_API int RowlEngine_GetRecentLogsJson(char* outBuffer, int bufferCap, int minLevel, int maxCount) {
    if (minLevel < 0) minLevel = 0;
    if (minLevel > 5) minLevel = 5;
    if (maxCount <= 0) maxCount = 50;
    if (maxCount > 256) maxCount = 256;

    std::string json = Rowl::Core::Logger::getRecentLogsJson(minLevel, static_cast<size_t>(maxCount));
    int requiredSize = static_cast<int>(json.size() + 1);

    if (!outBuffer || bufferCap <= 0) {
        return requiredSize;
    }

    if (bufferCap < requiredSize) {
        std::memcpy(outBuffer, json.data(), bufferCap - 1);
        outBuffer[bufferCap - 1] = '\0';
        return -requiredSize;
    }

    std::memcpy(outBuffer, json.data(), json.size());
    outBuffer[json.size()] = '\0';
    return static_cast<int>(json.size());
}

ROWL_API int RowlEngine_QueryWorldJson(
    RowlEngineHandle handle,
    const char* filterJson,
    char* outBuffer,
    int bufferCap) {
    return invokeNoexcept([&]() -> int {
        auto engine = copyEngineAnyThread(handle);
        if (!engine) return 0;

        bool compact = true;
        bool checkAnomalies = true;

        if (filterJson && filterJson[0] != '\0') {
            try {
                auto parsed = nlohmann::json::parse(filterJson, nullptr, false);
                if (!parsed.is_discarded() && parsed.is_object()) {
                    if (parsed.contains("compact")) compact = parsed["compact"].get<bool>();
                    if (parsed.contains("anomalies")) checkAnomalies = parsed["anomalies"].get<bool>();
                    // seq_since / dirty_only desteklenmez: sessizce yoksaymak yerine
                    // reddet ki arayan filtrelenmemiş tam anlık görüntüyü filtreli sanmasın.
                    if (parsed.contains("seq_since") || parsed.contains("dirty_only")) return 0;
                }
            } catch (...) {}
        }

        uint64_t currentSeq = s_worldQuerySeq.fetch_add(1, std::memory_order_relaxed);

        nlohmann::json root;
        root["seq"] = currentSeq;
        root["isPlaying"] = engine->isPlaying();

        // Story State
        nlohmann::json story;
        std::string spk = engine->getActiveSpeaker();
        std::string dlg = engine->getActiveDialogue();
        std::string bg = engine->getActiveBackground();
        std::string ch = engine->getActiveCharacter();
        float charX = engine->getActiveCharacterX();
        float charY = engine->getActiveCharacterY();
        float charW = engine->getActiveCharacterWidth();
        float charH = engine->getActiveCharacterHeight();
        float charRot = engine->getActiveCharacterRotation();

        story["spk"] = spk;
        story["dlg"] = dlg;
        story["bg"] = bg;
        if (!ch.empty()) {
            story["ch"] = ch;
            story["bounds"] = {
                {"x", std::round(charX * 100.0f) / 100.0f},
                {"y", std::round(charY * 100.0f) / 100.0f},
                {"w", std::round(charW * 100.0f) / 100.0f},
                {"h", std::round(charH * 100.0f) / 100.0f},
                {"rot", std::round(charRot * 100.0f) / 100.0f}
            };
        }
        // Story Choices
        nlohmann::json choicesArray = nlohmann::json::array();
        for (const auto& chBtn : engine->getActiveChoiceButtons()) {
            choicesArray.push_back({
                {"id", chBtn.optionId},
                {"text", chBtn.text},
                {"bounds", {
                    {"x", chBtn.x}, {"y", chBtn.y}, {"w", chBtn.width}, {"h", chBtn.height}
                }}
            });
        }
        story["choices"] = choicesArray;
        root["story"] = story;

        // Game State Variables, Progress & History Rewind Depth
        nlohmann::json vars = nlohmann::json::object();
        auto gs = engine->getGameState();
        uint32_t rewindSteps = 0;
        if (gs) {
            root["nodeId"] = gs->activeNodeId;
            root["stepId"] = gs->stepId;
            if (gs->variables) {
                for (const auto& [k, v] : gs->variables->data) {
                    vars[k] = v;
                }
            }
            auto cur = gs->previousState;
            while (cur && rewindSteps < 50) {
                rewindSteps++;
                cur = cur->previousState;
            }

            if (gs->dialogueHistory) {
                nlohmann::json historyArr = nlohmann::json::array();
                const auto& hist = *gs->dialogueHistory;
                size_t startIdx = hist.size() > 5 ? hist.size() - 5 : 0;
                for (size_t i = startIdx; i < hist.size(); ++i) {
                    historyArr.push_back({
                        {"nodeId", hist[i].nodeId},
                        {"spk", hist[i].speaker},
                        {"dlg", hist[i].dialogue}
                    });
                }
                root["history"] = historyArr;
            }
        }
        root["vars"] = vars;
        root["rewindStepsAvailable"] = rewindSteps;

        // Automated Anomalies Detection
        if (checkAnomalies) {
            nlohmann::json anom = nlohmann::json::array();
            if (!std::isfinite(charX) || !std::isfinite(charY) || !std::isfinite(charW) || !std::isfinite(charH)) {
                anom.push_back({
                    {"code", "NAN_INF_COORDINATES"},
                    {"detail", "Karakter koordinat veya boyutları NaN ya da sonsuz (Inf) içeriyor."}
                });
            }
            if (!ch.empty() && (charW <= 0.0f || charH <= 0.0f)) {
                anom.push_back({
                    {"code", "ZERO_DIMENSION"},
                    {"detail", "Aktif karakterin genişlik veya yükseklik boyutu sıfır ya da negatif."}
                });
            }
            if (!spk.empty() && dlg.empty()) {
                anom.push_back({
                    {"code", "EMPTY_DIALOGUE"},
                    {"detail", "Konuşmacı tanımlı ancak diyalog metni boş."}
                });
            }
            root["anom"] = anom;
        }

        std::string jsonStr = root.dump(compact ? -1 : 2);
        int requiredSize = static_cast<int>(jsonStr.size() + 1);

        if (!outBuffer || bufferCap <= 0) {
            return requiredSize;
        }

        if (bufferCap < requiredSize) {
            std::memcpy(outBuffer, jsonStr.data(), bufferCap - 1);
            outBuffer[bufferCap - 1] = '\0';
            return -requiredSize;
        }

        std::memcpy(outBuffer, jsonStr.data(), jsonStr.size());
        outBuffer[jsonStr.size()] = '\0';
        return static_cast<int>(jsonStr.size());
    }, 0);
}

ROWL_API int RowlEngine_InjectInputJson(
    RowlEngineHandle handle,
    const char* inputJson) {
    return invokeNoexcept([&]() -> int {
        if (!inputJson || inputJson[0] == '\0') return 0;
        auto engine = copyEngineAnyThread(handle);
        if (!engine) return 0;

        auto parsed = nlohmann::json::parse(inputJson, nullptr, false);
        if (parsed.is_discarded()) return 0;

        auto processEvent = [&](const nlohmann::json& ev) {
            if (!ev.is_object()) return;
            std::string t = ev.value("t", ev.value("type", ""));
            if (t == "tap" || t == "pointer_down" || t == "click") {
                float x = ev.value("x", 0.0f);
                float y = ev.value("y", 0.0f);
                engine->handlePointerDown(x, y);
            } else if (t == "choice") {
                uint32_t idx = ev.value("index", 0u);
                engine->advanceToNextNode(idx);
            } else if (t == "choice_id") {
                std::string optId = ev.value("id", "");
                if (!optId.empty()) {
                    engine->advanceToChoice(optId);
                }
            } else if (t == "advance" || t == "next") {
                engine->advanceToNextNode(0);
            } else if (t == "rewind" || t == "back") {
                uint32_t steps = ev.value("steps", 1u);
                engine->rewind(steps);
            } else if (t == "swipe_back") {
                engine->rewind(1);
            } else if (t == "drag" || t == "swipe") {
                float fromX = ev.value("fromX", 0.0f);
                float fromY = ev.value("fromY", 0.0f);
                float toX = ev.value("toX", 0.0f);
                float toY = ev.value("toY", 0.0f);
                float dx = toX - fromX;
                float dy = toY - fromY;
                if (dx < -50.0f && std::fabs(dx) >= std::fabs(dy)) {
                    engine->advanceToNextNode(0); // Swipe forward
                } else if (dx > 50.0f && std::fabs(dx) >= std::fabs(dy)) {
                    engine->rewind(1); // Swipe back
                } else {
                    if (!engine->handlePointerDown(fromX, fromY)) {
                        engine->advanceToNextNode(0);
                    }
                }
            }
        };

        if (parsed.is_array()) {
            for (const auto& item : parsed) {
                processEvent(item);
            }
        } else {
            processEvent(parsed);
        }

        return 1;
    }, 0);
}

ROWL_API int RowlEngine_GetAudioStatsJson(
    RowlEngineHandle handle,
    char* outBuffer,
    int bufferCap) {
    return invokeNoexcept([&]() -> int {
        auto engine = copyEngineAnyThread(handle);
        if (!engine) return 0;

        auto audio = engine->getAudio();
        nlohmann::json root;

        if (!audio) {
            root["available"] = false;
            root["reason"] = "AudioEngine not initialized";
        } else {
            root["available"] = true;
            root["deviceAvailable"] = audio->isAudioDeviceAvailable();
            root["bgmPlaying"] = audio->isBgmPlaying();
            root["bgmPath"] = audio->getCurrentBgmPath();
            root["isStreaming"] = audio->isStreaming();
            root["voicePlaying"] = audio->isVoicePlaying();
            root["duckingActive"] = audio->isDuckingActive();

            float master = audio->getMasterVolume();
            float bgm = audio->getBgmVolume();
            float voice = audio->getVoiceVolume();
            float sfx = audio->getSfxVolume();
            float amb = audio->getAmbienceVolume();
            float ui = audio->getUiVolume();

            root["volumes"] = {
                {"master", std::round(master * 100.0f) / 100.0f},
                {"bgm", std::round(bgm * 100.0f) / 100.0f},
                {"voice", std::round(voice * 100.0f) / 100.0f},
                {"sfx", std::round(sfx * 100.0f) / 100.0f},
                {"ambience", std::round(amb * 100.0f) / 100.0f},
                {"ui", std::round(ui * 100.0f) / 100.0f}
            };

            // Real Channel Telemetry (Left & Right RMS/Peak)
            // 0: BGM, 1: Voice, 2: SFX, 3: Master, 4: Ambience, 5: UI
            auto makeChanObj = [&](int type) {
                float rmsL = audio->getChannelRms(type, 0);
                float rmsR = audio->getChannelRms(type, 1);
                float peakL = audio->getChannelPeak(type, 0);
                float peakR = audio->getChannelPeak(type, 1);
                return nlohmann::json{
                    {"rmsL", std::round(rmsL * 1000.0f) / 1000.0f},
                    {"rmsR", std::round(rmsR * 1000.0f) / 1000.0f},
                    {"peakL", std::round(peakL * 1000.0f) / 1000.0f},
                    {"peakR", std::round(peakR * 1000.0f) / 1000.0f}
                };
            };

            root["channels"] = {
                {"master", makeChanObj(3)},
                {"bgm", makeChanObj(0)},
                {"voice", makeChanObj(1)},
                {"sfx", makeChanObj(2)},
                {"ambience", makeChanObj(4)},
                {"ui", makeChanObj(5)}
            };

            // Real 4-Band Audio Spectrum
            float bands[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            audio->getSpectrumBands(bands, 4);
            root["spectrum"] = {
                std::round(bands[0] * 1000.0f) / 1000.0f,
                std::round(bands[1] * 1000.0f) / 1000.0f,
                std::round(bands[2] * 1000.0f) / 1000.0f,
                std::round(bands[3] * 1000.0f) / 1000.0f
            };

            float masterPeakL = audio->getChannelPeak(3, 0);
            float masterPeakR = audio->getChannelPeak(3, 1);
            float masterRmsL = audio->getChannelRms(3, 0);
            float masterRmsR = audio->getChannelRms(3, 1);
            float masterPeak = std::max(masterPeakL, masterPeakR);
            float masterRms = std::max(masterRmsL, masterRmsR);

            // Heuristic metrics & Clipping risk calculation
            float activeGain = 0.0f;
            if (audio->isBgmPlaying()) activeGain += bgm;
            if (audio->isVoicePlaying()) activeGain += voice;
            float totalGain = activeGain * master;

            bool isClipping = masterPeak >= 1.0f || totalGain > 1.05f;
            bool isSilent = !audio->isBgmPlaying() && !audio->isVoicePlaying() && masterRms < 0.001f;

            root["metrics"] = {
                {"estimatedRms", std::round(std::min(1.0f, totalGain * 0.45f) * 100.0f) / 100.0f},
                {"isHeuristic", true},
                {"masterPeak", std::round(masterPeak * 1000.0f) / 1000.0f},
                {"masterRms", std::round(masterRms * 1000.0f) / 1000.0f},
                {"clippingRisk", isClipping},
                {"isSilent", isSilent}
            };

            std::string lastErr = audio->getLastError();
            if (!lastErr.empty()) {
                root["lastError"] = lastErr;
            }
        }

        std::string jsonStr = root.dump();
        int requiredSize = static_cast<int>(jsonStr.size() + 1);

        if (!outBuffer || bufferCap <= 0) {
            return requiredSize;
        }

        if (bufferCap < requiredSize) {
            std::memcpy(outBuffer, jsonStr.data(), bufferCap - 1);
            outBuffer[bufferCap - 1] = '\0';
            return -requiredSize;
        }

        std::memcpy(outBuffer, jsonStr.data(), jsonStr.size());
        outBuffer[jsonStr.size()] = '\0';
        return static_cast<int>(jsonStr.size());
    }, 0);
}

ROWL_API int RowlEngine_QueryVfsStatusJson(
    RowlEngineHandle handle,
    const char* probePath,
    char* outBuffer,
    int bufferCap) {
    return invokeNoexcept([&]() -> int {
        auto engine = copyEngineAnyThread(handle);
        if (!engine) return 0;

        auto* vfs = engine->getVfs();
        nlohmann::json root;
        nlohmann::json mounts = nlohmann::json::array();

        if (vfs) {
            auto mountPoints = vfs->getMountPoints();
            for (const auto& [prefix, source] : mountPoints) {
                mounts.push_back({
                    {"prefix", prefix},
                    {"source", source ? source->getSourceName() : ""}
                });
            }
            root["mountCount"] = mountPoints.size();
            root["skippedPackages"] = vfs->skippedPackageCount();
            root["firstSkippedPackage"] = vfs->firstSkippedPackage();
        } else {
            root["mountCount"] = 0;
            root["skippedPackages"] = 0;
            root["firstSkippedPackage"] = "";
        }
        root["mounts"] = mounts;

        if (probePath && probePath[0] != '\0') {
            bool exists = vfs ? vfs->exists(probePath) : false;
            std::string diagnosis = exists ? "OK" : (mounts.empty() ? "UNMOUNTED" : "MOUNT_OK_BUT_MISSING");
            root["query"] = {
                {"path", probePath},
                {"exists", exists},
                {"diagnosis", diagnosis}
            };
        }

        std::string jsonStr = root.dump(2);
        int requiredSize = static_cast<int>(jsonStr.size() + 1);

        if (!outBuffer || bufferCap <= 0) {
            return requiredSize;
        }

        if (bufferCap < requiredSize) {
            std::memcpy(outBuffer, jsonStr.data(), bufferCap - 1);
            outBuffer[bufferCap - 1] = '\0';
            return -requiredSize;
        }

        std::memcpy(outBuffer, jsonStr.data(), jsonStr.size());
        outBuffer[jsonStr.size()] = '\0';
        return static_cast<int>(jsonStr.size());
    }, 0);
}

ROWL_API void RowlEngine_FreeObservabilityString(char* str) {
    // Gözlemlenebilirlik sorguları arayan-tahsisli arabelleğe yazar; native
    // tarafta ayrılmış dize yok. İmza P/Invoke sözleşmesi için tutulur.
    (void)str;
}

} // extern "C"
