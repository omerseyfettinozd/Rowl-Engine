// engine_render_facade.cpp — W8-d(3): texture-cache / last-frame Engine facade'u.
//
// c_api_render.cpp R:84-154'deki katman ihlalinin (checked->getWindow()->
// windowMetodu() zinciri, Engine'i baypas eder) davranissiz karsiligi.
// Her yordam pencere-bosken sifir/no-op doner; donusum ve null-kollari
// R'deki eski govdelerle birebirdir. W8-a hunku (R:60-82) etkilenmez.
// C ABI imzasi degismez (additive); yeni `RowlEngine_` sembolu YOK.

#include "rowl/core/engine.hpp"

#include "rowl/render/window.hpp"

#include <cstdint>

namespace Rowl::Core {

uint32_t Engine::getTextureCacheTextureCount() const {
    const auto* window = getWindow();
    return window ? static_cast<uint32_t>(window->getTextureCacheTextureCount()) : 0;
}

uint64_t Engine::getTextureCacheBytes() const {
    const auto* window = getWindow();
    return window ? window->getTextureCacheBytes() : 0;
}

uint64_t Engine::getTextureCacheBudgetBytes() const {
    const auto* window = getWindow();
    return window ? window->getTextureCacheBudgetBytes() : 0;
}

uint64_t Engine::getTextureCacheEvictionCount() const {
    const auto* window = getWindow();
    return window ? window->getTextureCacheEvictionCount() : 0;
}

double Engine::getLastFrameTextureLoadMilliseconds() const {
    const auto* window = getWindow();
    return window ? window->getLastFrameTextureLoadMilliseconds() : 0.0;
}

double Engine::getLastFrameNonTextureRenderMilliseconds() const {
    const auto* window = getWindow();
    return window ? window->getLastFrameNonTextureRenderMilliseconds() : 0.0;
}

double Engine::getLastFrameTextRasterizationMilliseconds() const {
    const auto* window = getWindow();
    return window ? window->getLastFrameTextRasterizationMilliseconds() : 0.0;
}

double Engine::getLastFrameRendererFlushMilliseconds() const {
    const auto* window = getWindow();
    return window ? window->getLastFrameRendererFlushMilliseconds() : 0.0;
}

void Engine::setTextureCacheBudgetBytes(uint64_t bytes) {
    if (auto* window = getWindow()) window->setTextureCacheBudgetBytes(bytes);
}

}  // namespace Rowl::Core
