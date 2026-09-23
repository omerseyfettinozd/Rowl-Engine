#pragma once

#include "rowl/render/font_renderer.hpp"

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Render {

// W8-d(4): font-cozumleme uc kademesi (window.cpp initFontRenderer'dan saf tasima).
//
// 1. VFS bellek-adaylari (fonts/default.ttf ve turevleri)
// 2. Sistem fontlari (debug-okunabilirlik yedegi; proje fontu degil)
// 3. /system/fonts taramasi (Android-tarzi; yoksa sessiz-gecis)
//
// Sira, aday listeleri ve negatif davranis (son WARN dahil) birebirdir.
// Kilit almaz; cagiran (Window::initFontRenderer) m_fontRenderer tesisatini
// kurar ve vfs()'i enjekte eder. SDL'ye dokunmaz.
void resolveWindowFont(FontRenderer& fontRenderer, Rowl::VFS::VFSManager& vfs);

}  // namespace Rowl::Render
