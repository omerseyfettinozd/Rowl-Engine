#include "c_api_internal.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/vfs/vfs.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

static std::atomic<uint64_t> s_worldQuerySeq{1};

namespace {

// Gözlem uçları da sahip-thread sözleşmesine tabidir: sahipsiz kopya
// yalnızca ömrü uzatır, ÇAĞRI HAKKI VERMEZ (c_api_internal.hpp). Engine
// okumaları kilitsizdir; InjectInput motoru değiştirir. Emsal:
// RowlEngine_IsCharacterTweenActive — ölü 0, yabancı damga + 0.
std::shared_ptr<Rowl::Core::Engine> ownerEngineOrReject(RowlEngineHandle handle,
                                                        const char* op) noexcept {
    switch (classifyHandle(handle)) {
        case HandleStanding::Dead:
            return nullptr;
        case HandleStanding::Foreign:
            stampWrongThread(handle, op);
            return nullptr;
        case HandleStanding::Mine:
            return toEngineChecked(handle);
    }
    return nullptr;
}

// InjectInput iki aşamalıdır: önce TÜM olaylar doğrulanır, sonra uygulanır.
// Tek geçişte bozuk bir olay öncekileri uygulanmış bırakıp 0 döndürüyordu;
// arayan "hiçbir şey olmadı" sanırken hikâye ilerlemiş oluyordu.
struct InputAction {
    enum class Kind { Tap, ChoiceId, ChoiceIndex, Advance, Rewind, Drag };
    Kind kind = Kind::Advance;
    float x = 0.0f, y = 0.0f, toX = 0.0f, toY = 0.0f;
    uint32_t count = 0;
    std::string id;
};

bool readFloat(const nlohmann::json& ev, const char* key, float& out) {
    const auto it = ev.find(key);
    if (it == ev.end()) return true;  // yoksa varsayılan
    if (!it->is_number()) return false;
    const double value = it->get<double>();
    if (!std::isfinite(value)) return false;
    out = static_cast<float>(value);
    return true;
}

bool readCount(const nlohmann::json& ev, const char* key, uint32_t& out) {
    const auto it = ev.find(key);
    if (it == ev.end()) return true;
    // Negatif tamsayı is_number_unsigned değildir: -1 → 4294967295 sarması reddedilir.
    if (!it->is_number_unsigned()) return false;
    const auto value = it->get<uint64_t>();
    if (value > UINT32_MAX) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

bool readId(const nlohmann::json& ev, const char* first, const char* second, std::string& out) {
    for (const char* key : {first, second}) {
        const auto it = ev.find(key);
        if (it == ev.end()) continue;
        if (!it->is_string()) return false;
        out = it->get<std::string>();
        if (!out.empty()) return true;
    }
    return true;
}

bool parseInputAction(const nlohmann::json& ev, InputAction& action) {
    if (!ev.is_object()) return false;
    std::string t;
    for (const char* key : {"t", "type"}) {
        const auto it = ev.find(key);
        if (it == ev.end()) continue;
        if (!it->is_string()) return false;
        t = it->get<std::string>();
        break;
    }
    using Kind = InputAction::Kind;
    if (t == "tap" || t == "pointer_down" || t == "click") {
        action.kind = Kind::Tap;
        return readFloat(ev, "x", action.x) && readFloat(ev, "y", action.y);
    }
    if (t == "choice") {
        if (!readId(ev, "optionId", "id", action.id)) return false;
        action.kind = action.id.empty() ? Kind::ChoiceIndex : Kind::ChoiceId;
        return readCount(ev, "index", action.count);
    }
    if (t == "choice_id") {
        action.kind = Kind::ChoiceId;
        return readId(ev, "id", "optionId", action.id);
    }
    if (t == "advance" || t == "next") {
        action.kind = Kind::Advance;
        return true;
    }
    if (t == "rewind" || t == "back") {
        action.kind = Kind::Rewind;
        action.count = 1;
        return readCount(ev, "steps", action.count);
    }
    if (t == "swipe_back") {
        action.kind = Kind::Rewind;
        action.count = 1;
        return true;
    }
    if (t == "drag" || t == "swipe") {
        action.kind = Kind::Drag;
        return readFloat(ev, "fromX", action.x) && readFloat(ev, "fromY", action.y) &&
               readFloat(ev, "toX", action.toX) && readFloat(ev, "toY", action.toY);
    }
    return false;
}

void applyInputAction(Rowl::Core::Engine& engine, const InputAction& action) {
    using Kind = InputAction::Kind;
    switch (action.kind) {
        case Kind::Tap:
            engine.handlePointerDown(action.x, action.y);
            break;
        case Kind::ChoiceId:
            if (!action.id.empty()) engine.advanceToChoice(action.id);
            break;
        case Kind::ChoiceIndex:
            engine.advanceToNextNode(action.count);
            break;
        case Kind::Advance:
            engine.advanceToNextNode(0);
            break;
        case Kind::Rewind:
            engine.rewind(action.count);
            break;
        case Kind::Drag: {
            const float dx = action.toX - action.x;
            const float dy = action.toY - action.y;
            if (dx < -50.0f && std::fabs(dx) >= std::fabs(dy)) {
                engine.advanceToNextNode(0);  // Swipe forward
            } else if (dx > 50.0f && std::fabs(dx) >= std::fabs(dy)) {
                engine.rewind(1);  // Swipe back
            } else if (!engine.handlePointerDown(action.x, action.y)) {
                engine.advanceToNextNode(0);
            }
            break;
        }
    }
}

}  // namespace

extern "C" {

ROWL_API void RowlEngine_SetLogCallback(RowlEngine_LogCallbackFn callback) {
    Rowl::Core::Logger::setLogCallback(callback);
}

ROWL_API int RowlEngine_GetRecentLogsJson(char* outBuffer, int bufferCap, int minLevel, int maxCount) {
    return invokeNoexcept([&]() -> int {
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
    }, 0);
}

ROWL_API int RowlEngine_QueryWorldJson(
    RowlEngineHandle handle,
    const char* filterJson,
    char* outBuffer,
    int bufferCap) {
    return invokeNoexcept([&]() -> int {
        auto engine = ownerEngineOrReject(handle, "query_world_json");
        if (!engine) return 0;

        bool compact = true;
        bool checkAnomalies = true;

        // Filtre katıdır: anlaşılmayan filtre varsayılan anlık görüntüye
        // DÜŞMEZ, reddedilir — arayan filtrelenmemiş tam görüntüyü filtreli
        // sanmasın. (Eski get<bool> istisnası catch(...) ile yutulunca
        // seq_since reddi de atlanıyordu.)
        if (filterJson && filterJson[0] != '\0') {
            const auto parsed = nlohmann::json::parse(filterJson, nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object()) {
                Rowl::Core::Logger::warn("QueryWorldJson: filter rejected; expected a JSON object.");
                return 0;
            }
            // seq_since / dirty_only desteklenmez.
            if (parsed.contains("seq_since") || parsed.contains("dirty_only")) {
                Rowl::Core::Logger::warn("QueryWorldJson: seq_since/dirty_only filter rejected; differential snapshot is not supported.");
                return 0;
            }
            for (auto [key, target] : {std::pair<const char*, bool*>{"compact", &compact},
                                       std::pair<const char*, bool*>{"anomalies", &checkAnomalies}}) {
                const auto it = parsed.find(key);
                if (it == parsed.end()) continue;
                if (!it->is_boolean()) {
                    Rowl::Core::Logger::warn(std::string("QueryWorldJson: filter rejected; '") + key +
                                             "' must be a boolean.");
                    return 0;
                }
                *target = it->get<bool>();
            }
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
        // D-K3: bounds are the drawn / hit-tested geometry (fitted to the text
        // scale); "focused" is additive: the option holding keyboard/hover focus.
        for (const auto& chBtn : engine->getLaidOutChoiceButtons()) {
            choicesArray.push_back({
                {"id", chBtn.optionId},
                {"text", chBtn.text},
                {"bounds", {
                    {"x", chBtn.x}, {"y", chBtn.y}, {"w", chBtn.width}, {"h", chBtn.height}
                }},
                {"focused", chBtn.focused}
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
        auto engine = ownerEngineOrReject(handle, "inject_input_json");
        if (!engine) return 0;

        const auto parsed = nlohmann::json::parse(inputJson, nullptr, false);
        if (parsed.is_discarded()) return 0;

        std::vector<InputAction> actions;
        if (parsed.is_array()) {
            actions.reserve(parsed.size());
            for (const auto& item : parsed) {
                InputAction action;
                if (!parseInputAction(item, action)) return 0;
                actions.push_back(std::move(action));
            }
        } else {
            InputAction action;
            if (!parseInputAction(parsed, action)) return 0;
            actions.push_back(std::move(action));
        }
        if (actions.empty()) return 0;

        for (const auto& action : actions) applyInputAction(*engine, action);
        return 1;
    }, 0);
}

ROWL_API int RowlEngine_GetAudioStatsJson(
    RowlEngineHandle handle,
    char* outBuffer,
    int bufferCap) {
    return invokeNoexcept([&]() -> int {
        auto engine = ownerEngineOrReject(handle, "get_audio_stats_json");
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
        auto engine = ownerEngineOrReject(handle, "query_vfs_status_json");
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
