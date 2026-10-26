#pragma once

#include <string>
#include <vector>

#include "rowl/render/window.hpp"

namespace Rowl::Render {

/// A fully assembled visual-novel frame, packed by the scene owner and
/// consumed by the render boundary in a single call.
///
/// The struct carries scene content only — no SDL handles, no caches, no
/// profiling state. Field order mirrors Window::renderVisualNovelFrame so the
/// forwarding call stays a plain member-wise pass-through.
struct ComposedFrame {
    bool hasBackground = true;
    std::string background;
    float backgroundX = 0.0f;
    float backgroundY = 0.0f;
    float backgroundWidth = 1920.0f;
    float backgroundHeight = 1080.0f;
    std::vector<CharacterRenderData> characters;
    std::vector<DialogueRenderData> dialogues;
    std::vector<ChoiceButtonRenderData> choices;
    float backgroundRotation = 0.0f;
    float backgroundParallaxX = 1.0f;
    float backgroundParallaxY = 1.0f;
    float backgroundOpacity = 1.0f;
    /// D04-2: player Auto / Skip status capsules (top-right). Never part of
    /// the content hash; the window tracks what is baked into its cached frame.
    bool showAutoBadge = false;
    bool showSkipBadge = false;
    /// D07-2: achievement toast text ("Basarim: X"), empty = none. Like the
    /// badges it never enters the content hash; the window tracks what is baked.
    std::string toastText;
};

} // namespace Rowl::Render
