#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <algorithm>
#include "rowl/render/aspect_guardian.hpp"

struct SDL_Surface;
struct SDL_Renderer;
struct SDL_Texture;

namespace Rowl::Render {

enum class TransitionType {
    None,
    CrossFade,
    FadeToBlack,
    FadeToWhite,
    FadeToColor,
    WipeLeft,
    WipeRight
};

class TransitionManager {
public:
    TransitionManager();
    ~TransitionManager();

    // Disable copy/move
    TransitionManager(const TransitionManager&) = delete;
    TransitionManager& operator=(const TransitionManager&) = delete;

    void startTransition(TransitionType type,
                         float durationSeconds,
                         uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255);

    void startTransitionFromKind(const std::string& kind,
                                 float durationSeconds,
                                 const std::string& colorHex = "");

    void stopTransition();

    // D6-#147: validate-before-snapshot + retrigger coalesce. Window,
    // pahalı snapshot (SDL readback) öncesi bu kapılardan geçer; yeni
    // mantık burada yaşar, window.cpp büyümez.
    static bool isKnownKind(const std::string& kind);
    static bool isUsableDuration(float durationSeconds);
    bool canStartTransition(const std::string& kind, float durationSeconds) const;
    bool shouldCoalesceRetrigger(const std::string& kind) const;

    // D6-#147 kanıt sayacı: captureSnapshot girişimi (readback) sayısı.
    // Geçersiz girdi spam'inde artmaması, doğrulamanın snapshot'tan önce
    // geldiğini kanıtlar (mutant: snapshot'ı öne taşıyan değişiklik bu
    // kilide takılır).
    uint64_t getSnapshotCaptureCount() const { return m_snapshotCaptureCount; }

    bool isTransitionActive() const { return m_type != TransitionType::None; }
    // #162: stage-then-commit sonrası gözlemlenebilirlik — başarısız capture
    // eski snapshot'a dokunmadığı için bu bayrak lock-testin pinidir.
    bool hasSnapshot() const { return m_snapshotTexture != nullptr; }
    TransitionType getType() const { return m_type; }
    float getProgress() const { return m_progress; } // 0.0f to 1.0f
    float getDuration() const { return m_duration; }
    float getElapsed() const { return m_elapsed; }

    // #163: başarısız sahne güncellemesi, update'le GELEN (önceden uçan)
    // geçişin snapshot'ını değiştirmişse descriptor'u geri yazar. Doku
    // Engine tarafında güncel kareden yeniden yakalanır (sahnenin geri
    // kalanı zaten restore edilmiştir); yakalama düşerse Engine abort eder.
    // Tür/doku tutarlılığı çağıranın sorumluluğundadır.
    void restoreTransitionState(TransitionType type, float durationSeconds, float elapsedSeconds) {
        m_type = type;
        m_duration = durationSeconds;
        m_elapsed = elapsedSeconds;
        m_progress = (durationSeconds > 0.0f)
                         ? std::clamp(elapsedSeconds / durationSeconds, 0.0f, 1.0f)
                         : 1.0f;
    }

    void update(float dt);

    /**
     * Captures a snapshot of the current surface/frame as an SDL_Texture on the given renderer.
     * This snapshot represents "Scene A" before transitioning to "Scene B".
     */
    bool captureSnapshot(SDL_Surface* surface, SDL_Renderer* renderer);

    /**
     * Renders the active transition over the new scene on the given renderer.
     */
    void renderTransition(SDL_Renderer* renderer, const ViewportMetrics& metrics);

    void reset();

private:
    void completeTransition();
    void cleanupSnapshot();

    TransitionType m_type = TransitionType::None;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    float m_progress = 0.0f;

    uint8_t m_colorR = 0;
    uint8_t m_colorG = 0;
    uint8_t m_colorB = 0;
    uint8_t m_colorA = 255;

    SDL_Texture* m_snapshotTexture = nullptr;
    int m_snapshotWidth = 0;
    int m_snapshotHeight = 0;
    uint64_t m_snapshotCaptureCount = 0;
};

} // namespace Rowl::Render
