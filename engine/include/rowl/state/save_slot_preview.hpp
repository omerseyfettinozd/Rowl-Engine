#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Rowl::State {

/// F2 (B-2): what a slot list needs from a save file, read WITHOUT building the
/// GameState. The list used to decode every slot fully (the 300 KB thumbnail
/// base64, the whole undo chain and the dialogue history) on every menu open:
/// ~115 ms per slot in Debug, 12 s for 100 slots. The preview walks the JSON with
/// a SAX parser that keeps only the top-level scalars, so nothing nested is built
/// and the thumbnail is neither stored nor decoded (see readSaveSlotThumbnail).
enum class SlotPreviewStatus {
    Valid,         ///< the top-level fields are well formed
    Corrupt,       ///< unreadable, not JSON, or an out-of-range / malformed field
    Incompatible,  ///< a well-formed save of a format version this build does not read
};

struct SaveSlotPreview {
    SlotPreviewStatus status = SlotPreviewStatus::Corrupt;
    uint32_t version = 0;
    uint64_t activeNodeId = 0;
    double playtimeSeconds = 0.0;
    std::string savedAt;
    std::string chapterTitle;
    std::string summary;
    std::string graphIdentity;
    bool hasThumbnail = false;
};

/// Previews a save payload. Same top-level rules as GameState::decodeJson (version
/// set, step / node ids, volumes, playtime, metadata sizes, thumbnail base64) but
/// nothing nested is validated: a variable or history defect still surfaces when
/// the slot is actually loaded.
SaveSlotPreview parseSaveSlotPreview(std::string_view json);

/// Reads a slot file (size-capped like a load) and previews it.
SaveSlotPreview readSaveSlotPreview(const std::filesystem::path& file);

/// The slot's thumbnail PNG bytes (decoded), empty when the file has none or it is
/// malformed. Called lazily for the selected row only.
std::string readSaveSlotThumbnail(const std::filesystem::path& file);

}  // namespace Rowl::State
