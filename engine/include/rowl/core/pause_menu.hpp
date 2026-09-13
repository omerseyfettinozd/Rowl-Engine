#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Rowl::Core {

/// MS-6 pause-menu contract shared by the engine state machine (hit-testing),
/// the SDL window (overlay rendering), and the C API (tests/hosts).
/// All geometry is in the 1920x1080 virtual canvas; hosts map physical input
/// to virtual coordinates before hit-testing.

enum class PauseMenuCommand : int32_t {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    Back = 4,
    Confirm = 5
};

enum class PauseMenuMode : int32_t {
    Main = 0,
    SaveSlots = 1,
    LoadSlots = 2
};

/// Player quick-save slots selectable at runtime (digits 0-9 / --slot N).
inline constexpr int32_t kPauseMenuQuickSlotMin = 0;
inline constexpr int32_t kPauseMenuQuickSlotMax = 9;

struct PauseMenuRow {
    std::string label;
    std::string value;
    bool isValue = false; // value rows adjust with Left/Right
};

struct PauseMenuView {
    bool open = false;
    std::string title;
    std::vector<PauseMenuRow> rows;
    int selected = 0;
    std::string hint;
};

/// Single source of truth for overlay layout. Row i occupies
/// [kRowY0 + i*(kRowH+kRowGap), +kRowH) at full kRowX..kRowX+kRowW width.
/// Value rows split into a minus zone (x < kMinusX1) and a plus zone
/// (x > kPlusX0); the middle band only selects.
struct PauseMenuLayout {
    static constexpr float kPanelX = 520.0f;
    static constexpr float kPanelY = 110.0f;
    static constexpr float kPanelW = 880.0f;
    static constexpr float kPanelH = 860.0f;
    static constexpr float kRowX = 580.0f;
    static constexpr float kRowW = 760.0f;
    static constexpr float kRowY0 = 290.0f;
    static constexpr float kRowH = 56.0f;
    static constexpr float kRowGap = 8.0f;
    static constexpr float kMinusX1 = 880.0f;
    static constexpr float kPlusX0 = 1040.0f;
    static constexpr int kMainRows = 9;
    static constexpr int kSlotRows = 10;

    /// Row index under a virtual point, or -1 for gaps/outside.
    static inline int rowAt(float vx, float vy, int rowCount) {
        if (vx < kRowX || vx > kRowX + kRowW || vy < kRowY0) return -1;
        const float stride = kRowH + kRowGap;
        const int index = static_cast<int>((vy - kRowY0) / stride);
        if (index < 0 || index >= rowCount) return -1;
        return (vy <= kRowY0 + index * stride + kRowH) ? index : -1;
    }

    /// -1 = minus zone, +1 = plus zone, 0 = middle (select only).
    static inline int adjustDirection(float vx) {
        if (vx < kMinusX1) return -1;
        if (vx > kPlusX0) return +1;
        return 0;
    }
};

} // namespace Rowl::Core
