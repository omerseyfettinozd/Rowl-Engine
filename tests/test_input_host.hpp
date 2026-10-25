#pragma once
// D-K3: enjekte edilebilir giris kuyruklu PlatformHost (motorun zaten kullandigi
// hosta iletir; takeInputEvents ile klavye / fare / duraklatma olaylari gercek
// Engine::handleRuntimeInput yolundan gecer). Testler arasi ortak yardimci.
#include "rowl/platform/platform_host.hpp"

#include <deque>
#include <filesystem>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace RowlTest {

class InputHost final : public Rowl::Platform::PlatformHost {
public:
    using RuntimeInputEvent = Rowl::Platform::RuntimeInputEvent;

    explicit InputHost(std::shared_ptr<Rowl::Platform::PlatformHost> inner) : m_inner(std::move(inner)) {}
    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override {
        return m_inner ? m_inner->openAssetStream(path) : nullptr;
    }
    std::filesystem::path writableSavePath() const override {
        return m_inner ? m_inner->writableSavePath() : std::filesystem::path();
    }
    std::filesystem::path writableProfilePath() const override {
        return m_inner ? m_inner->writableProfilePath() : std::filesystem::path("profiles");
    }
    Rowl::Platform::LifecycleState lifecycleState() const override {
        return m_inner ? m_inner->lifecycleState() : Rowl::Platform::LifecycleState::Active;
    }
    std::vector<RuntimeInputEvent> takeInputEvents() override {
        std::vector<RuntimeInputEvent> result(input.begin(), input.end());
        input.clear();
        return result;
    }
    Rowl::Platform::RenderSurface renderSurface() const override {
        return m_inner ? m_inner->renderSurface() : Rowl::Platform::RenderSurface{};
    }
    Rowl::Platform::AudioFocus audioFocus() const override {
        return m_inner ? m_inner->audioFocus() : Rowl::Platform::AudioFocus::Granted;
    }
    std::deque<RuntimeInputEvent> input;

private:
    std::shared_ptr<Rowl::Platform::PlatformHost> m_inner;
};

}  // namespace RowlTest
