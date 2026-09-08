# Rowl Engine — Ajan Kuralları ve Hafıza Entegrasyonu

## 🧠 İkinci Beyin (Obsidian) Bağlantısı
- Bu projenin resmi mimari dökümantasyonu, hafızası ve kuralları kullanıcının Obsidian kasasındadır:
  - **Kasa Yolu:** `/home/chaple/second-brain`
  - **Rowl Engine Mimari Notları:** `/home/chaple/second-brain/300-Projects/`
    - `Rowl-Engine.md` (Genel Mimari)
    - `Rowl-Engine-Core.md` (Çekirdek Motor & C API)
    - `Rowl-Engine-Render.md` (SDL3 Render Pipeline & Viewport)
    - `Rowl-Engine-VFS.md` (Sanal Dosya Sistemi & .rowlpkg)
    - `Rowl-Engine-Bridge.md` (C++ / C# P/Invoke Köprüsü)
    - `Rowl-Engine-Editor.md` (C# Avalonia 11 Editör Mimarisi)
  - **Genel Bilgi Bankası:** `/home/chaple/second-brain/500-Knowledge/`
  - **Günlük Notlar / Kararlar:** `/home/chaple/second-brain/daily/`

Kullanıcı "Obsidian", "İkinci Beyin", "hafıza", "dökümanlar" veya "notlar" dediğinde; doğrudan `/home/chaple/second-brain` altındaki bu dosyaları referans al, oku ve gerekirse güncellemeleri oraya da not düş.

## 🛠️ Proje Standartları
- **C++ Çekirdeği:** C++20 standardı, CMake derleme sistemi, SDL3 offscreen render hattı.
- **Editör Katmanı:** .NET 10, Avalonia 11 UI framework, CommunityToolkit.Mvvm.
- **Köprü (Bridge):** `RowlEngineCore` paylaşımlı kütüphanesi ile P/Invoke (`c_api.h`).
- **Bellek Güvenliği:** Ham işaretçiler yerine modern RAII ve akıllı işaretçiler (`std::unique_ptr`, `std::shared_ptr`).
- **Git & GitHub Standardı:** Tüm commit ve push işlemlerinde sistemin hazır SSH anahtarı (`git@github.com:...`) ve doğrulanmış `Ömer Seyfettin <omerseyfettin.ozd@gmail.com>` kimliği kullanılır. Asla yerel e-posta (`.local` vb.) tanımlanmaz veya HTTP token aranmaz; doğrudan `git commit` ve `git push origin main` yapılır.

