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

    bool isTransitionActive() const { return m_type != TransitionType::None; }
    TransitionType getType() const { return m_type; }
    float getProgress() const { return m_progress; } // 0.0f to 1.0f
    float getDuration() const { return m_duration; }
    float getElapsed() const { return m_elapsed; }

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
};

} // namespace Rowl::Render
