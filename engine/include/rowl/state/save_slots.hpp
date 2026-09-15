#pragma once

#include <cstdint>

namespace Rowl::State {

/// Canonical save-slot range (Faz 4.5 Dilim 2, locked decision).
///
/// Every slot guard in the engine (Engine, SessionPersistence, C API) points
/// at these constants. Slot 100 and above are out of range; the quick-save
/// path (kPauseMenuQuickSlotMin..Max, 0..9) is a subset of this range.
inline constexpr int32_t kMinSaveSlot = 0;
inline constexpr int32_t kMaxSaveSlot = 99;

inline constexpr bool isValidSlot(int32_t slotIndex) {
    return slotIndex >= kMinSaveSlot && slotIndex <= kMaxSaveSlot;
}

} // namespace Rowl::State
