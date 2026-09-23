// window_font_resolve.cpp — W8-d(4): font-cozumleme uc kademesi.
//
// Window::initFontRenderer govdesinden saf tasima (mekanik yeniden-adlandirma
// disinda birebir: m_fontRenderer-> -> fontRenderer., vfs() -> vfs).
// Sira (VFS-once / sistem-sonra / /system/fonts taramasi) ve negatif-kume
// davranisi korunur. Yeni export YOK, `RowlEngine_` sembolu YOK.

#include "rowl/render/window_font_resolve.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Rowl::Render {

void resolveWindowFont(FontRenderer& fontRenderer, Rowl::VFS::VFSManager& vfs) {
    namespace fs = std::filesystem;

    // 1. Try VFS Manager memory buffer candidates
    std::vector<std::string> vfsFontCandidates = {
        "fonts/default.ttf",
        "fonts/default_bold.ttf",
        "Assets/fonts/default.ttf",
        "Assets/fonts/default_bold.ttf",
        "default.ttf",
        "default_bold.ttf"
    };

    for (const auto& vf : vfsFontCandidates) {
        auto bytes = vfs.readBytes(vf);
        if (!bytes.empty()) {
            if (fontRenderer.loadFontFromMemory(bytes.data(), bytes.size())) {
                ROWL_LOG_INFO("✅ Loaded Visual Novel Font from VFS [" + vf + "]");
                return;
            }
        }
    }

    // 2. System fonts are a non-project fallback for readable debug output.
    // Project fonts must come through the VFS above, whose mount set is
    // established from the selected project root.
    const std::vector<std::string> systemFontCandidates = {
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/cantarell/Cantarell-VF.otf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFPro.ttf"
    };

    for (const auto& path : systemFontCandidates) {
        // A3-tur4 (metin turu): error_code yoklamasi (throw yok) + yol
        // nesnesiyle yukleme (Windows-wide hazir; dar-string ANSI
        // tuzagindan kacinir).
        const fs::path candidate(path);
        std::error_code probeEc;
        if (!fs::is_regular_file(candidate, probeEc) || probeEc) continue;
        if (fontRenderer.loadFontFromPath(candidate)) {
            ROWL_LOG_INFO("✅ Loaded Visual Novel TTF Font from System: " + path);
            return;
        }
    }

    // Faz 4.5 Dilim 4: optional /system/fonts mount (Android-style). Resolves
    // strictly AFTER the project VFS and the desktop candidates above, and is
    // list-only plus exists()-guarded: an absent directory (every desktop/CI
    // machine) skips silently via error_code paths, never probing or throwing.
    {
        std::error_code dirError;
        const fs::path systemFontDir("/system/fonts");
        if (fs::exists(systemFontDir, dirError) && !dirError &&
            fs::is_directory(systemFontDir, dirError) && !dirError) {
            std::error_code iterError;
            fs::directory_iterator it(systemFontDir,
                                      fs::directory_options::skip_permission_denied,
                                      iterError);
            const fs::directory_iterator end;
            for (; it != end && !iterError; it.increment(iterError)) {
                std::error_code entryError;
                if (!it->is_regular_file(entryError) || entryError) continue;
                const std::string ext = it->path().extension().string();
                if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
                if (fontRenderer.loadFont(it->path().string())) {
                    ROWL_LOG_INFO("Loaded Visual Novel TTF Font from /system/fonts: " +
                                  it->path().string());
                    return;
                }
            }
        }
    }

    ROWL_LOG_WARN("⚠️ No TrueType Font could be loaded. Fallback debug text will be used.");
}

}  // namespace Rowl::Render
