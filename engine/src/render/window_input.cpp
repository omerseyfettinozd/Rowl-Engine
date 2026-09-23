// window_input.cpp — W8-d(2): SDL-key haritasi + pollEvents yonlendirmesi.
//
// window.cpp'den birebir tasinmistir:
//   119-121    touchCoordinateToPhysical (TU-local yardimci; tek kullanicisi
//              pollEvents parmak-bacaklari oldugu icin birlikte tasindi)
//   892-942    Window::mapKeyToRuntimeInput (SDL-key -> RuntimeInput haritasi)
//   1024-1169  Window::pollEvents (SDL_EVENT_* -> m_inputHandler)
// Kosul, sira ve yorumlar birebirdir. Kilit almaz, yeni sira kurmaz.
// Yeni export YOK, `RowlEngine_` sembolu YOK.

#include "rowl/render/window.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/platform/mobile_input.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace Rowl::Render {

namespace {

float touchCoordinateToPhysical(float normalized, uint32_t extent) {
    return std::clamp(normalized, 0.0f, 1.0f) * static_cast<float>(extent);
}

}  // namespace

bool Window::mapKeyToRuntimeInput(uint32_t sdlKey, Rowl::Platform::RuntimeInputEvent& outEvent) {
    using Type = Rowl::Platform::RuntimeInputEvent::Type;
    outEvent.x = 0.0f;
    outEvent.y = 0.0f;
    outEvent.slot = 0;
    // MS-6 player shell: Escape/P pause (never quit), digits pick the
    // quick-save slot, arrows drive menu navigation, Space/Enter advance
    // (the engine reroutes them to menu-confirm while paused).
    if (sdlKey == SDLK_ESCAPE || sdlKey == SDLK_P) {
        outEvent.type = Type::PauseToggle;
        return true;
    }
    if (sdlKey == SDLK_SPACE || sdlKey == SDLK_RETURN || sdlKey == SDLK_KP_ENTER) {
        outEvent.type = Type::Advance;
        return true;
    }
    if (sdlKey == SDLK_UP) {
        outEvent.type = Type::MenuUp;
        return true;
    }
    if (sdlKey == SDLK_DOWN) {
        outEvent.type = Type::MenuDown;
        return true;
    }
    if (sdlKey == SDLK_LEFT) {
        outEvent.type = Type::MenuLeft;
        return true;
    }
    if (sdlKey == SDLK_RIGHT) {
        outEvent.type = Type::MenuRight;
        return true;
    }
    if (sdlKey == SDLK_F5) {
        outEvent.type = Type::QuickSave;
        return true;
    }
    if (sdlKey == SDLK_F9) {
        outEvent.type = Type::QuickLoad;
        return true;
    }
    if (sdlKey == SDLK_BACKSPACE || sdlKey == SDLK_Z) {
        outEvent.type = Type::Rewind;
        return true;
    }
    if (sdlKey >= SDLK_0 && sdlKey <= SDLK_9) {
        outEvent.type = Type::SelectSlot;
        outEvent.slot = static_cast<int32_t>(sdlKey - SDLK_0);
        return true;
    }
    return false;
}

void Window::pollEvents(bool& outShouldQuit) {
    if (!m_initialized) return;

    if (m_eventWindowId == 0) return;
    for (const SDL_Event& event : Rowl::Platform::SdlEventDispatcher::takeEvents(m_eventWindowId)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                outShouldQuit = true;
                m_isOpen = false;
                break;
            case SDL_EVENT_KEY_DOWN: {
                // MS-6: Escape/P toggle the pause menu (never quit outright);
                // quitting needs the menu's two-step confirmation. The OS
                // window-close event below still exits immediately.
                Rowl::Platform::RuntimeInputEvent input{
                    Rowl::Platform::RuntimeInputEvent::Type::Advance};
                if (mapKeyToRuntimeInput(static_cast<uint32_t>(event.key.key), input)) {
                    if (m_inputHandler) m_inputHandler(input);
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerDown,
                                                        event.button.x, event.button.y});
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                // #16: release was queued by the dispatcher but had no branch
                // here, so press/release pairing was unobservable. Left-button
                // only, mirroring BUTTON_DOWN above.
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerUp,
                                                        event.button.x, event.button.y});
                }
                break;
            case SDL_EVENT_KEY_UP: {
                // #16: key release was queued but branchless. Every release is
                // observable (press/release pairing needs unmapped keys too);
                // the SDL keycode rides along in `key`.
                Rowl::Platform::RuntimeInputEvent input{
                    Rowl::Platform::RuntimeInputEvent::Type::KeyUp};
                input.key = static_cast<uint32_t>(event.key.key);
                if (m_inputHandler) m_inputHandler(input);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                // #16: motion never routed before the dispatcher fix; now it
                // surfaces as hover position through the input channel.
                if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerMotion,
                                                    event.motion.x, event.motion.y});
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                // #16: wheel never routed before the dispatcher fix. x/y carry
                // scroll deltas (not cursor position); SDL reports flipped
                // (natural) deltas inverted, so normalize the sign back per
                // the SDL_MouseWheelEvent contract.
                float dx = event.wheel.x;
                float dy = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    dx = -dx;
                    dy = -dy;
                }
                if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::Scroll, dx, dy});
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                // #16: text never routed before the dispatcher fix. Null text
                // carries nothing observable and stays unconsumed.
                if (event.text.text == nullptr) break;
                Rowl::Platform::RuntimeInputEvent input{
                    Rowl::Platform::RuntimeInputEvent::Type::TextInput};
                input.text = event.text.text;
                if (m_inputHandler) m_inputHandler(input);
                break;
            }
            case SDL_EVENT_TEXT_EDITING: {
                // #60: composition never routed (dispatcher nullopt-drop).
                // Null text carries nothing observable and stays unconsumed.
                if (event.edit.text == nullptr) break;
                Rowl::Platform::RuntimeInputEvent input{
                    Rowl::Platform::RuntimeInputEvent::Type::TextEditing};
                input.text = event.edit.text;
                input.compositionStart = event.edit.start;
                input.compositionLength = event.edit.length;
                if (m_inputHandler) m_inputHandler(input);
                break;
            }
            case SDL_EVENT_TEXT_EDITING_CANDIDATES: {
                // #60: aday listesi bilinçli tüketilir (iz bırakır, olay
                // taşımaz) — composition metni TEXT_EDITING bacağından akar.
                ROWL_LOG_TRACE("Window consumed TEXT_EDITING_CANDIDATES without payload mapping");
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                // #16: finger motion was queued but branchless. Same
                // viewport-relative mapping as FINGER_DOWN/UP so drag
                // positions land in physical pixels.
                const float x = touchCoordinateToPhysical(event.tfinger.x, m_width);
                const float y = touchCoordinateToPhysical(event.tfinger.y, m_height);
                if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerMotion, x, y});
                break;
            }
            case SDL_EVENT_FINGER_DOWN: {
                const float x = touchCoordinateToPhysical(event.tfinger.x, m_width);
                const float y = touchCoordinateToPhysical(event.tfinger.y, m_height);
                m_touchStarts[event.tfinger.fingerID] = {x, y};
                break;
            }
            case SDL_EVENT_FINGER_UP: {
                const auto start = m_touchStarts.find(event.tfinger.fingerID);
                if (start == m_touchStarts.end()) break;

                const float x = touchCoordinateToPhysical(event.tfinger.x, m_width);
                const float y = touchCoordinateToPhysical(event.tfinger.y, m_height);
                const auto gesture = Rowl::Platform::MobileInput::classifyTouchGesture(
                    start->second.first, start->second.second, x, y,
                    static_cast<float>(m_width), static_cast<float>(m_height));
                m_touchStarts.erase(start);
                if (gesture == Rowl::Platform::InputEventType::SwipeForward) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::SwipeForward, x, y});
                } else if (gesture == Rowl::Platform::InputEventType::SwipeBack) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::SwipeBack, x, y});
                } else if (m_inputHandler) {
                    m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerDown, x, y});
                }
                break;
            }
            case SDL_EVENT_FINGER_CANCELED:
                m_touchStarts.erase(event.tfinger.fingerID);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                int rw = 0, rh = 0;
                if (SDL_GetRenderOutputSize(m_sdlRenderer, &rw, &rh) && rw > 0 && rh > 0) {
                    m_width = static_cast<uint32_t>(rw);
                    m_height = static_cast<uint32_t>(rh);
                } else {
                    m_width = static_cast<uint32_t>(event.window.data1);
                    m_height = static_cast<uint32_t>(event.window.data2);
                }
                ROWL_LOG_TRACE("Window resized to physical render output: " + std::to_string(m_width) + "x" + std::to_string(m_height));
                break;
        }
    }
}

}  // namespace Rowl::Render
