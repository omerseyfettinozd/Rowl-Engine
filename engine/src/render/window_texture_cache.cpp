/**
 * window_texture_cache.cpp — A3-tur5: texture-cache bölünmesi.
 *
 * window.cpp'den VERBATIM taşındı (davranış-sıfır): donanım doku önbelleği
 * (clear/budget/touch/missing/destroy/evict/load) + yalnız-bu-blokta
 * kullanılan dosya-yerel yardımcılar (boyut-guard + güvenli stb-decode) ve
 * 4 constexpr eşik. STB_IMAGE_IMPLEMENTATION window.cpp'de kalır (tek TU);
 * burası yalnız başlığı tüketir. K2/D2 kilitleri bölünmeyi denetler.
 */
#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Rowl::Render {

namespace {

constexpr int kMaxTextureDimension = 8'192;
constexpr uint64_t kMaxTexturePixels = 16ULL * 1024 * 1024;
constexpr uint64_t kMinimumTextureCacheBytes = 1ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxMissingTextureCacheEntries = 512;

bool hasSafeTextureDimensions(int width, int height) {
    return width > 0 && height > 0 &&
           width <= kMaxTextureDimension && height <= kMaxTextureDimension &&
           static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= kMaxTexturePixels;
}

unsigned char* loadTextureMemorySafely(const uint8_t* bytes, int byteCount,
                                       int* width, int* height, int* channels) {
    int infoWidth = 0, infoHeight = 0, infoChannels = 0;
    if (!stbi_info_from_memory(bytes, byteCount, &infoWidth, &infoHeight, &infoChannels)) return nullptr;
    if (!hasSafeTextureDimensions(infoWidth, infoHeight)) {
        ROWL_LOG_WARN("Rejected texture buffer with unsafe dimensions");
        return nullptr;
    }
    return stbi_load_from_memory(bytes, byteCount, width, height, channels, 4);
}

} // namespace

void Window::clearTextureCache() {
    invalidateFrameCache();
    const bool hadMsdfAtlas = m_msdfAtlasTexture != nullptr;
    std::unordered_set<SDL_Texture*> uniqueTextures;
    for (auto& [name, tex] : m_textureCache) {
        if (tex) {
            uniqueTextures.insert(tex);
        }
    }
    for (auto* tex : uniqueTextures) {
        SDL_DestroyTexture(tex);
    }
    m_textureCache.clear();
    m_textureMemoryBytes.clear();
    m_textureLastUsed.clear();
    m_missingTextureCache.clear();
    m_budgetRejectedTextureCache.clear();
    m_buttonFontCache.clear();
    m_textureCacheEvictionCount = 0;
    m_textureUseClock = 0;
    m_msdfAtlasTexture = nullptr;
    if (hadMsdfAtlas) {
        m_msdfRenderer.reset();
        shutdownGpuMsdfRenderer();
    }
    ROWL_LOG_INFO("Hardware Texture Cache Cleared (" + std::to_string(uniqueTextures.size()) + " unique textures freed).");
}

void Window::setTextureCacheBudgetBytes(uint64_t bytes) {
    const uint64_t previousBudget = m_textureCacheBudgetBytes;
    m_textureCacheBudgetBytes = std::max(bytes, kMinimumTextureCacheBytes);
    if (m_textureCacheBudgetBytes > previousBudget) {
        // A previously oversized asset may now fit; do not keep its fallback
        // state after the host moves to a larger device profile.
        m_budgetRejectedTextureCache.clear();
    }
    if (!evictTexturesToFit(0)) {
        ROWL_LOG_WARN("Texture cache budget cannot be met while the MSDF atlas is pinned");
    }
}

uint64_t Window::getTextureCacheBytes() const {
    uint64_t total = 0;
    for (const auto& [texture, bytes] : m_textureMemoryBytes) {
        (void)texture;
        total += bytes;
    }
    return total;
}

void Window::touchTexture(SDL_Texture* texture) {
    if (texture) m_textureLastUsed[texture] = ++m_textureUseClock;
}

void Window::rememberMissingTexture(std::string path) {
    if (m_missingTextureCache.contains(path)) return;
    if (m_missingTextureCache.size() >= kMaxMissingTextureCacheEntries) {
        m_missingTextureCache.erase(m_missingTextureCache.begin());
    }
    m_missingTextureCache.insert(std::move(path));
}

void Window::destroyCachedTexture(SDL_Texture* texture) {
    if (!texture) return;
    for (auto it = m_textureCache.begin(); it != m_textureCache.end();) {
        if (it->second == texture) {
            it = m_textureCache.erase(it);
        } else {
            ++it;
        }
    }
    m_textureMemoryBytes.erase(texture);
    m_textureLastUsed.erase(texture);
    SDL_DestroyTexture(texture);
}

bool Window::evictTexturesToFit(uint64_t incomingBytes) {
    if (incomingBytes > m_textureCacheBudgetBytes) return false;
    while (getTextureCacheBytes() > m_textureCacheBudgetBytes - incomingBytes) {
        SDL_Texture* leastRecentlyUsed = nullptr;
        uint64_t oldestUse = UINT64_MAX;
        for (const auto& [texture, lastUsed] : m_textureLastUsed) {
            if (texture != m_msdfAtlasTexture && lastUsed < oldestUse) {
                leastRecentlyUsed = texture;
                oldestUse = lastUsed;
            }
        }
        if (!leastRecentlyUsed) return false;
        ROWL_LOG_INFO("Evicting least-recently-used texture to respect cache budget");
        destroyCachedTexture(leastRecentlyUsed);
        ++m_textureCacheEvictionCount;
    }
    return true;
}

SDL_Texture* Window::loadTexture(const std::string& filename) {
    if (filename.empty() || !m_sdlRenderer) return nullptr;

    // Normalize slashes
    std::string normPath = filename;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    if (m_missingTextureCache.contains(normPath)) return nullptr;
    if (m_budgetRejectedTextureCache.contains(normPath)) return nullptr;

    // Cache hit: only return valid textures
    auto it = m_textureCache.find(normPath);
    if (it != m_textureCache.end() && it->second != nullptr) {
        touchTexture(it->second);
        return it->second;
    }

    const auto loadStarted = std::chrono::steady_clock::now();
    const auto recordLoadTime = [this, loadStarted] {
        if (!m_collectingFrameProfile) return;
        m_lastFrameTextureLoadMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - loadStarted).count();
    };

    int width = 0, height = 0, channels = 0;
    unsigned char* data = nullptr;
    std::string sourceInfo;

    // normPath is a UTF-8 virtual path, not a native filesystem path. Keep it
    // byte-preserving on Windows instead of round-tripping through its code page.
    const size_t lastSlash = normPath.find_last_of('/');
    std::string bareName = lastSlash == std::string::npos
        ? normPath
        : normPath.substr(lastSlash + 1);

    // Asset paths are resolved only through the selected project's VFS.
    // This prevents CWD, parent-directory, or arbitrary absolute paths from
    // silently becoming runtime assets after a project switch.
    std::vector<std::string> vfsCandidates = {
        normPath,
        bareName,
        "images/" + bareName,
        "images/" + normPath,
        "Assets/images/" + bareName,
        "Assets/images/" + normPath,
        "Assets/" + bareName,
        "Assets/" + normPath
    };

    for (const auto& candidate : vfsCandidates) {
        auto bytes = vfs().readBytes(candidate);
        if (!bytes.empty()) {
            data = loadTextureMemorySafely(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels);
            if (data) {
                sourceInfo = "VFS [" + candidate + "]";
                break;
            }
        }
    }

    if (!data) {
        rememberMissingTexture(std::move(normPath));
        recordLoadTime();
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        width, height, SDL_PIXELFORMAT_RGBA32, data, width * 4
    );

    if (!surface) {
        stbi_image_free(data);
        recordLoadTime();
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(data);

    if (texture) {
        const uint64_t textureBytes = static_cast<uint64_t>(width) *
                                      static_cast<uint64_t>(height) * 4ULL;
        if (!evictTexturesToFit(textureBytes)) {
            SDL_DestroyTexture(texture);
            if (m_budgetRejectedTextureCache.size() >= kMaxMissingTextureCacheEntries) {
                m_budgetRejectedTextureCache.erase(m_budgetRejectedTextureCache.begin());
            }
            m_budgetRejectedTextureCache.insert(normPath);
            ROWL_LOG_WARN("Texture exceeds the configured cache budget: " + filename);
            recordLoadTime();
            return nullptr;
        }
        m_missingTextureCache.erase(normPath);
        // A3-tur3 (lifecycle): tek anahtar (normPath). filename/bareName
        // insert'leri okunmuyordu (:1110 tek okuma) — olu agirlik + capraz-
        // dizin bareName golgeleme kaldirildi. Sayaclar isaretci-anahtarli,
        // eviction value-erase'li: davranis-notr.
        m_textureCache[normPath] = texture;
        m_textureMemoryBytes[texture] = textureBytes;
        touchTexture(texture);
        ROWL_LOG_INFO("✅ Loaded Hardware Texture: " + filename + " (" + std::to_string(width) + "x" + std::to_string(height) + ") from " + sourceInfo);
    }
    recordLoadTime();
    return texture;
}
} // namespace Rowl::Render
