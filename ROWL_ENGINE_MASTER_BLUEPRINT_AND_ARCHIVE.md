# 🎮 ROWL ENGINE — MASTER BLUEPRINT & COMPLETE SYSTEM ARCHIVE
**Sürüm:** v1.0.0 Commercial Release Spec  
**Tarih:** 2026-08-29  
**Mimari:** C++20 Hardware Core (SDL3, Audio DSP, Lua 5.4, VFS, Zstd) + C# Avalonia UI Editor (.NET 10, MVVM, Dynamic Themes)  
**Hedef:** Projeyi sıfırdan birebir yeniden inşa edebilecek (reproduce/recreate) seviyede tüm mimari, kod blokları, ikili dosya formatları, hata çözümleri ve derleme adımları arşivi.

---

## 📑 İÇİNDEKİLER

1. [Proje Özeti ve Temel Felsefe](#1-proje-özeti-ve-temel-felsefe)
2. [Tam Dizin Ağacı ve Dosya Rolleri](#2-tam-dizin-ağacı-ve-dosya-rolleri)
3. [İkili (Binary) ve Veri Formatı Spesifikasyonları](#3-ikili-binary-ve-veri-formatı-spesifikasyonları)
   - [3.1 `.rowlpkg` Binary Paket Formatı (Byte-by-Byte)](#31-rowlpkg-binary-paket-formatı-byte-by-byte)
   - [3.2 64-bit vs 32-bit Hash Hizalama Hatası ve Çözümü](#32-64-bit-vs-32-bit-hash-hizalama-hatası-ve-çözümü)
   - [3.3 `full_story_graph.json` & `active_story.json` Veri Şeması](#33-full_story_graphjson--active_storyjson-veri-şeması)
   - [3.4 `project.rowlproj` Proje Manifest Formatı](#34-projectrowlproj-proje-manifest-formatı)
4. [C++20 Motor Çekirdeği (Engine Core Subsystems)](#4-c20-motor-çekirdeği-engine-core-subsystems)
   - [4.1 Native C-API (`c_api.h` / `c_api.cpp`) ve Zero-Copy P/Invoke](#41-native-c-api-c_apih--c_apicpp-ve-zero-copy-pinvoke)
   - [4.2 Motor Durum Makinesi (`engine.hpp` / `engine.cpp`)](#42-motor-durum-makinesi-enginehpp--enginecpp)
   - [4.3 Hibrit Sanal Dosya Sistemi (`vfs.hpp` / `vfs.cpp`) & İzolasyon Kuralları](#43-hibrit-sanal-dosya-sistemi-vfshpp--vfscpp--izolasyon-kuralları)
   - [4.4 Paket Okuyucu (`rowlpkg_reader.hpp` / `rowlpkg_reader.cpp`)](#44-paket-okuyucu-rowlpkg_readerhpp--rowlpkg_readercpp)
   - [4.5 Ses ve DSP Filtre Motoru (`audio_engine.hpp` / `audio_engine.cpp`)](#45-ses-ve-dsp-filtre-motoru-audio_enginehpp--audio_enginecpp)
   - [4.6 Güvenli Lua 5.4 Sandbox (`lua_sandbox.hpp` / `lua_sandbox.cpp`)](#46-güvenli-lua-54-sandbox-lua_sandboxhpp--lua_sandboxcpp)
   - [4.7 SDL3 Render & Offscreen Framebuffer (`window.hpp` / `window.cpp`)](#47-sdl3-render--offscreen-framebuffer-windowhpp--windowcpp)
   - [4.8 Standalone Oyuncu (`player_main.cpp`)](#48-standalone-oyuncu-player_maincpp)
5. [C# Avalonia UI Editör Mimarisi (.NET 10)](#5-c-avalonia-ui-editör-mimarisi-net-10)
   - [5.1 `MainWindowViewModel.cs` — Çekirdek Mantık & Durum Yönetimi](#51-mainwindowviewmodelcs--çekirdek-mantık--durum-yönetimi)
   - [5.2 ⚙️ Ayarlar Sistemi (`SettingsViewModel.cs` & `SettingsDialog.axaml`)](#52-️-ayarlar-sistemi-settingsviewmodelcs--settingsdialogaxaml)
   - [5.3 🎨 Dinamik Tema Motoru (`ThemeStyles.axaml`)](#53--dinamik-tema-motoru-themestylesaxaml)
   - [5.4 ↩️ Undo / Redo Komut Yığını (`UndoRedoService.cs`)](#54-️-undo--redo-komut-yığını-undoredoservicecs)
   - [5.5 🔔 Toast Bildirim Altyapısı (`ToastService.cs`)](#55--toast-bildirim-altyapısı-toastservicecs)
   - [5.6 🏠 Proje Hub Sistemi (`ProjectHubViewModel.cs` & `ProjectHubWindow.axaml`)](#56--proje-hub-sistemi-projecthubviewmodelcs--projecthubwindowaxaml)
   - [5.7 Görsel Arayüzler (2 Satırlı Toolbar, Status Bar, Quick Search, Split Screen)](#57-görsel-arayüzler-2-satırlı-toolbar-status-bar-quick-search-split-screen)
6. [Çoklu Platform Ticari Dağıtım ve Dışa Aktarma (Store & Steam Pipelines)](#6-çoklu-platform-ticari-dağıtım-ve-dışa-aktarma-store--steam-pipelines)
   - [6.1 Linux / Steam Deck Paketi](#61-linux--steam-deck-paketi)
   - [6.2 Windows Ticari Paketi](#62-windows-ticari-paketi)
   - [6.3 macOS Apple App Bundle (.app)](#63-macos-apple-app-bundle-app)
   - [6.4 Android APK / Google Play Pipeline](#64-android-apk--google-play-pipeline)
   - [6.5 iOS IPA / App Store Pipeline](#65-ios-ipa--app-store-pipeline)
7. [Geçmiş Hatalar, Kök Neden Analizleri ve Uygulanan Düzeltmeler](#7-geçmiş-hatalar-kök-neden-analizleri-ve-uygulanan-düzeltmeler)
8. [Sıfırdan Derleme, Test ve Çalıştırma Kılavuzu](#8-sıfırdan-derleme-test-ve-çalıştırma-kılavuzu)

---

## 1. PROJE ÖZETİ VE TEMEL FELSEFE

**Rowl Engine**, 2D Görsel Roman (Visual Novel) ve Etkileşimli Hikaye (Interactive Storytelling) oyunları geliştirmek için tasarlanmış, **hibrit mimariye sahip ultra hızlı bir oyun motorudur**.

### 🌟 Temel Felsefeler:
1. **Zero-IPC Native Interop (Sıfır İletişim Maliyeti)**: Editör (C# Avalonia UI) ile Motor Çekirdeği (C++20) arasında hantal socket, pipe veya flatbuffers IPC iletişimi yerine doğrudan `.so` / `.dll` P/Invoke C-API çağrıları kullanılır. Framebuffer ve hafıza blokları paylaşımlı bellek (zero-copy) ile aktarılır.
2. **Instant Hot-Reload**: Editörde bir node veya diyalog değiştiği anda tek tıkla (`PushHotReloadPacket`) C++ motorundaki bellek grafiği ve donanım doku havuzu kesintisiz güncellenir.
3. **Hibrit VFS (Virtual File System)**: Hem diskteki açık dosyalar (`Assets/`) hem de sıkıştırılmış yüksek performanslı paketler (`.rowlpkg`) öncelik sırasına göre tek bir sanal dosya ağacında birleştirilir.
4. **Ticari Mağaza Standardı (Steam, Epic, Google Play, App Store)**: Dışa aktarma işlemi basit bir veri dosyası üretmekle kalmaz; Windows'ta `.exe`, Linux'ta bağımsız ELF, macOS'ta `.app` paketi, Steamworks entegrasyonu için `steam_appid.txt` ve ticari `README.txt` dosyalarıyla birlikte tek tıkla teslim eder.
5. **Modern Editör Deneyimi**: 5 sekmeli Ayarlar penceresi, 4 canlı renk teması, 50 adımlı Undo/Redo sistemi, Toast bildirimleri, Spotlight arama overlay'ı ve 2 satırlı profesyonel araç çubuğu.

---

## 2. TAM DİZİN AĞACI VE DOSYA ROLLERİ

```
Rowl Engine/
├── CMakeLists.txt                         # Kök CMake projesi (Engine + Tests)
├── start_editor.sh                        # Editörü başlatan bash betiği
├── start_engine.sh                        # C++ motorunu başlatan bash betiği
├── run_editor.sh                          # Derleme ve çalıştırma otomasyon betiği
├── README.md                              # Genel proje tanıtımı
├── ROWL_ENGINE_MASTER_BLUEPRINT_AND_ARCHIVE.md # Bu master arşiv belgesi
│
├── Assets/                                # Proje kaynakları (Diyaloglar, Resimler, Sesler)
│   ├── full_story_graph.json              # Aktif hikaye düğüm grafiği (düz format)
│   ├── active_story.json                  # C++ motorunun doğrudan tükettiği aktif sahne
│   ├── project.rowlproj                   # Proje manifest ve meta verileri
│   ├── images/                            # Sahne arka planları ve karakter sprite'ları
│   ├── packages/                          # Üretilen .rowlpkg arşivleri
│   └── test_assets/                       # Test senaryoları
│
├── engine/                                # C++20 Donanım Motoru Çekirdeği
│   ├── CMakeLists.txt                     # Motor derleme tanımları (SDL3, Zstd, Lua)
│   ├── include/rowl/
│   │   ├── c_api.h                        # P/Invoke ve harici C bağlantıları için API
│   │   ├── core/
│   │   │   ├── engine.hpp                 # RowlEngine sınıf tanımı
│   │   │   └── logger.hpp                 # Konsol ve dosya loglayıcı
│   │   ├── vfs/
│   │   │   ├── vfs.hpp                    # Sanal Dosya Sistemi arayüzü
│   │   │   └── rowlpkg_reader.hpp         # .rowlpkg binary arşiv okuyucu
│   │   ├── audio/
│   │   │   └── audio_engine.hpp           # SDL3 Audio, ses miksajı ve DSP filtreleri
│   │   ├── scripting/
│   │   │   └── lua_sandbox.hpp            # Lua 5.4 korumalı çalıştırma alanı
│   │   ├── render/
│   │   │   ├── window.hpp                 # SDL3 Pencere ve Donanım/Yazılım Renderer
│   │   │   ├── aspect_guardian.hpp        # 16:9 en-boy koruma ve piksel ölçekleme
│   │   │   └── msdf_renderer.hpp          # Çok kanallı SDF metin renderlayıcı
│   │   ├── state/
│   │   │   └── game_state.hpp             # Değişken tablosu ve save/load durumu
│   │   └── platform/
│   │       └── mobile_input.hpp           # Çoklu dokunmatik ve hareket normalizasyonu
│   └── src/
│       ├── c_api.cpp                      # C-API fonksiyon gövdeleri
│       ├── player_main.cpp                # Bağımsız rowl_player çalıştırıcısı
│       ├── core/engine.cpp                # Ana motor döngüsü, render ve sahne yönetimi
│       ├── core/logger.cpp                # Log formatlama
│       ├── vfs/vfs.cpp                    # VFS bağlama, yol çözümleme ve izolasyon
│       ├── vfs/rowlpkg_reader.cpp         # İkili paket açma, zstd decompress
│       ├── audio/audio_engine.cpp         # DSP filtreleri (Telephone, Underwater, Reverb)
│       ├── scripting/lua_sandbox.cpp      # Lua sandbox güvenlik kancaları
│       ├── render/window.cpp              # SDL3 pencere, offscreen framebuffer oluşturma
│       ├── render/aspect_guardian.cpp     # Görüş alanı ve harici ölçek hesaplama
│       └── state/game_state.cpp           # Durum kaydetme/yükleme
│
├── editor/                                # C# Avalonia UI Editörü (.NET 10)
│   ├── RowlEngine.Editor.csproj           # Editör proje dosyası
│   ├── Program.cs                         # .NET giriş noktası (Desktop & Mobile)
│   ├── App.axaml / App.axaml.cs           # Tema kaynakları ve başlangıç penceresi
│   ├── Controls/
│   │   └── BezierWireRenderer.cs          # ComfyUI stili Bézier kablo çizicisi
│   ├── Styles/
│   │   └── ThemeStyles.axaml              # Dinamik tema paletleri (DynamicResource)
│   ├── Services/
│   │   ├── UndoRedoService.cs             # 50 adımlı Undo/Redo komut geçmişi
│   │   ├── ProjectRegistryService.cs      # Proje kayıt defteri ve SQLite/JSON yönetimi
│   │   └── AssetBitmapCache.cs            # Görsel bellek önbelleği
│   ├── ViewModels/
│   │   ├── ViewModelBase.cs               # MVVM temel sınıfı
│   │   ├── MainWindowViewModel.cs         # Ana editör ViewModel (~3200 satır)
│   │   ├── NodeViewModel.cs               # Düğüm veri modeli (Karakter, Diyalog, X, Y)
│   │   ├── ConnectionViewModel.cs         # Bağlantı kablosu modeli
│   │   ├── SettingsViewModel.cs           # Ayarlar ve tema yönetim ViewModel'i
│   │   ├── ToastService.cs                # Global toast bildirim servisi
│   │   ├── ProjectHubViewModel.cs         # Hub ve proje listesi ViewModel'i
│   │   └── Components/                    # Diyalog ve karakter alt bileşenleri
│   └── Views/
│       ├── MainWindow.axaml / .cs         # Ana editör arayüzü (Toolbar, Split, Status)
│       ├── ProjectHubWindow.axaml / .cs   # Minecraft tarzı proje açılış merkezi
│       ├── NodeControl.axaml / .cs        # Node kartı bileşeni (ComfyUI stili)
│       ├── LivePreviewControl.axaml / .cs # Yazılımsal canlı önizleme
│       ├── EnginePreviewControl.axaml / .cs # C++ P/Invoke Offscreen Game View
│       ├── Dialogs/
│       │   ├── SettingsDialog.axaml / .cs # 5 sekmeli modern ayarlar modalı
│       │   ├── ConfirmDialog.axaml / .cs  # Onay modalı
│       │   ├── CreateProjectDialog.axaml  # Yeni proje oluşturma modalı
│       │   └── RenameProjectDialog.axaml  # Proje ismi değiştirme modalı
│       └── Panels/
│           ├── NodeGraphView.axaml / .cs  # Sonsuz 2D Canvas ve zoom/pan alanı
│           ├── NodeInspectorView.axaml    # Sağ panel: Karakter, Metin, DSP, Boyut
│           ├── ProjectAssetsView.axaml    # Sol panel: Dosya gezgini ve ağaç görünümü
│           └── OutputLogView.axaml        # Alt panel: Konsol çıktı günlüğü
│
├── editor/RowlEngine.Editor.Core/         # Platform Bağımsız Çekirdek Servisler
│   ├── RowlEngine.Editor.Core.csproj
│   └── Services/
│       ├── RowlPackageBuilder.cs          # C# .rowlpkg ikili paket derleyicisi
│       └── Abstractions/
│           └── IPlatformFileSystem.cs     # Desktop ve Android SAF dosya soyutlaması
│
├── tools/                                 # CLI Araçları ve Python Yardımcıları
│   ├── package_assets.py                  # Python .rowlpkg paketleyicisi
│   ├── export_game.py                     # Oyun dışa aktarma otomasyonu
│   ├── stress_test_engine.py              # Motor dayanıklılık testleri
│   └── test_ipc_sync.py                   # P/Invoke ve sahne senkronizasyon testi
│
├── packaging/                             # Çoklu Platform Paketleme Şablonları
│   ├── android/                           # Android Manifest, Kotlin Activity ve Gradle
│   └── ios/                               # iOS Info.plist ve derleme betikleri
│
└── tests/                                 # C++ Test Paketi
    ├── CMakeLists.txt
    └── main_test_runner.cpp               # VFS, DSP, Lua, Render testleri
```

---

## 3. İKİLİ (BINARY) VE VERİ FORMATI SPESİFİKASYONLARI

### 3.1 `.rowlpkg` Binary Paket Formatı (Byte-by-Byte)

`.rowlpkg`, Rowl Engine'in özel, yüksek performanslı, sıkıştırılmış veya ham veri paket formatıdır. C++, C# ve Python tarafından **1 baytlık struct hizalaması (`#pragma pack(push, 1)`)** ile okunur/yazılır.

#### 📦 1. Ana Başlık (Master Header — 18 Bayt):
```
+---------------+---------------+--------------------+--------------------+
| Magic (4B)    | Version (2B)  | File Count (4B)    | Index Offset (8B)  |
| 'R','O','W','L'| uint16_t = 1  | uint32_t (N files) | uint64_t           |
+---------------+---------------+--------------------+--------------------+
0               4               6                    10                   18
```

#### 📦 2. Veri Bloğu (Data Payload):
- Başlıktan hemen sonra (`offset = 18`) tüm dosyaların ham veya Zstd ile sıkıştırılmış bayt dizileri sıralı olarak yazılır.

#### 📦 3. İndeks Girişi (Entry Record — `IndexOffset` konumunda başlar):
Her dosya için indeks tablosunda aşağıdaki yapı tekrarlanır:
```c++
#pragma pack(push, 1)
struct RowlPkgEntryRaw {
    uint64_t pathHash;          // 8 Bayt: Dosya yolunun FNV-1a 64-bit hash değeri
    uint32_t pathLength;        // 4 Bayt: UTF-8 dosya yolu metninin bayt uzunluğu
    uint64_t offset;            // 8 Bayt: Dosyanın paket içerisindeki başlangıç byte ofseti
    uint64_t compressedSize;    // 8 Bayt: Paketteki sıkıştırılmış boyutu (0 ise sıkıştırılmamış)
    uint64_t uncompressedSize;  // 8 Bayt: Orijinal açılmış bayt boyutu
    uint32_t flags;             // 4 Bayt: Bayraklar (Bit 0: Zstd Sıkıştırma)
};
#pragma pack(pop)
// Bu struct'ın ardından 'pathLength' kadar UTF-8 karakter baytı gelir.
```

#### 🔑 FNV-1a 64-bit Hashing Algoritması:
```csharp
public static ulong ComputeFnv1a64(string text)
{
    ulong hash = 14695981039346656037UL;
    byte[] bytes = Encoding.UTF8.GetBytes(text.Replace('\\', '/').TrimStart('/'));
    foreach (byte b in bytes)
    {
        hash ^= b;
        hash *= 1099511628211UL;
    }
    return hash;
}
```

---

### 3.2 64-bit vs 32-bit Hash Hizalama Hatası ve Çözümü

#### 🔴 Tespit Edilen Kritik Hata:
C# tarafındaki `RowlPackageBuilder.cs` paket indeksini yazarken `BinaryWriter.Write(uint)` çağırarak `pathHash`'i **4 bayt (uint32)** olarak yazıyordu. Fakat C++ motorundaki `rowlpkg_reader.hpp` `RowlPkgEntryRaw` struct'ı `uint64_t pathHash` beklediği için **8 bayt** okuyordu.
Bu durum **4 baytlık faz kaymasına (offset shift)** neden oldu; `pathLength` alanı `offset` ile, `offset` alanı `compressedSize` ile karıştı ve C++ okuyucusu `"Package entry size too large, possible corruption"` hatasıyla çöktü.

#### 🟢 Uygulanan Kesin Çözüm:
`RowlPackageBuilder.cs` satır 60'ta yazma işlemi 64-bit ulong tipine dönüştürüldü:
```csharp
// ÖNCE (Hatalı): bw.Write((uint)e.hash);
// SONRA (Düzeltildi):
bw.Write((ulong)e.hash); // 8 Bayt uint64_t C++ struct hizalamasına tam uyumlu
```

---

### 3.3 `full_story_graph.json` & `active_story.json` Veri Şeması

Rowl Engine iki JSON formatını da tam uyumlulukla destekler:

#### 1. Düz Format (Flat Schema — `full_story_graph.json`):
```json
{
  "project_name": "said",
  "version": "1.0.0",
  "start_node_id": 101,
  "nodes": [
    {
      "id": 101,
      "title": "Giriş Sahnesi",
      "speaker": "babaG",
      "dialogue": "Rowl Engine dünyasına hoş geldiniz!",
      "background": "Otsu.jpg",
      "character_sprite": "jpg.jpg",
      "character_x": 0.0,
      "character_y": 0.0,
      "character_width": 360.0,
      "character_height": 540.0,
      "character_scale": 1.0,
      "dialogue_box_x": 80.0,
      "dialogue_box_y": 860.0,
      "dialogue_box_width": 1760.0,
      "dialogue_box_height": 180.0,
      "dialogue_box_scale": 1.0,
      "dsp_filter": "Normal",
      "x": 120.0,
      "y": 140.0
    }
  ],
  "connections": [
    {
      "source_id": 101,
      "target_id": 102
    }
  ]
}
```

#### 2. Bileşen Bazlı Format (Component Schema — `active_story.json`):
```json
{
  "node_id": 101,
  "scene_id": "scene_101",
  "audio_dsp": "Normal",
  "components": [
    {
      "type": "BackgroundComponent",
      "image_path": "Otsu.jpg",
      "rect": { "x": 0, "y": 0, "w": 1920, "h": 1080 }
    },
    {
      "type": "CharacterComponent",
      "sprite_path": "jpg.jpg",
      "rect": { "x": 780, "y": 270, "w": 360, "h": 540 },
      "scale": 1.0
    },
    {
      "type": "DialogueComponent",
      "speaker": "babaG",
      "text": "Rowl Engine dünyasına hoş geldiniz!",
      "box_rect": { "x": 80, "y": 860, "w": 1760, "h": 180 }
    }
  ]
}
```

---

### 3.4 `project.rowlproj` Proje Manifest Formatı

```json
{
  "name": "said",
  "version": "1.0.0",
  "engineVersion": "1.0.0",
  "createdAt": "2026-08-26T18:00:00.0000000Z",
  "savedAt": "2026-08-26T22:30:00.0000000Z",
  "nodeCount": 3,
  "startNodeId": 101,
  "customCover": "cover.png"
}
```

---

## 4. C++20 MOTOR ÇEKİRDEĞİ (ENGINE CORE SUBSYSTEMS)

### 4.1 Native C-API (`c_api.h` / `c_api.cpp`) ve Zero-Copy P/Invoke

P/Invoke köprüsü `c_api.h` üzerinden dışa aktarılır (`ROWL_API`):

```c
#ifndef ROWL_C_API_H
#define ROWL_C_API_H

#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32)
  #define ROWL_API __declspec(dllexport)
#else
  #define ROWL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* RowlEngineHandle;

ROWL_API RowlEngineHandle RowlEngine_Create(void);
ROWL_API bool             RowlEngine_Initialize(RowlEngineHandle handle, int width, int height, bool headless);
ROWL_API bool             RowlEngine_UpdateScene(RowlEngineHandle handle, const char* speaker, const char* dialogue, const char* background);
ROWL_API bool             RowlEngine_UpdateSceneFromJson(RowlEngineHandle handle, const char* jsonString);
ROWL_API bool             RowlEngine_Step(RowlEngineHandle handle, double deltaTime);
ROWL_API const uint8_t*   RowlEngine_GetFramebuffer(RowlEngineHandle handle, int* outWidth, int* outHeight, int* outPitch);
ROWL_API bool             RowlEngine_SetPlayState(RowlEngineHandle handle, bool isPlaying);
ROWL_API void             RowlEngine_Shutdown(RowlEngineHandle handle);
ROWL_API void             RowlEngine_Destroy(RowlEngineHandle handle);

#ifdef __cplusplus
}
#endif
#endif
```

---

### 4.2 Motor Durum Makinesi (`engine.hpp` / `engine.cpp`)

Motor sınıfı `rowl::RowlEngine`, ana döngüyü, doku önbelleğini ve VFS önceliğini koordine eder:

```c++
namespace rowl {

class RowlEngine {
public:
    RowlEngine();
    ~RowlEngine();

    bool initialize(const EngineConfig& config);
    bool updateSceneFromJson(const std::string& jsonString);
    bool step(double deltaTime);
    void render();
    const uint8_t* getOffscreenFramebuffer(int* outWidth, int* outHeight, int* outPitch) const;
    void setPlayState(bool playing);
    void shutdown();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<AudioEngine> m_audio;
    std::unique_ptr<LuaSandbox> m_lua;
    std::unique_ptr<VFS> m_vfs;
    std::unordered_map<std::string, SDL_Texture*> m_textureCache;
    SceneState m_currentScene;
    bool m_isPlaying{false};
};

} // namespace rowl
```

---

### 4.3 Hibrit Sanal Dosya Sistemi (`vfs.hpp` / `vfs.cpp`) & İzolasyon Kuralları

VFS, fiziksel dizinleri ve `.rowlpkg` paketlerini tek bir hiyerarşide birleştirir.

#### 🛡️ Kritik İzolasyon Kuralı:
VFS'in üst dizinlere (`../../`) tırmanarak Masaüstü veya başka projelerin dosyalarını okumasını engellemek için **Directory Climbing kaldırılmıştır**. Bağlanan bir projenin kök dizini mutlak referanstır.

```c++
bool VFS::mountPackage(const std::string& packagePath, const std::string& mountPrefix) {
    auto source = std::make_shared<RowlPkgDataSource>(packagePath);
    if (!source->isOpen()) return false;
    
    // Paketler en yüksek önceliğe sahip olması için vektörün EN BAŞINA eklenir:
    m_mountPoints.insert(m_mountPoints.begin(), MountPoint{mountPrefix, source});
    return true;
}
```

---

### 4.4 Paket Okuyucu (`rowlpkg_reader.hpp` / `rowlpkg_reader.cpp`)

Paket okuyucu doğrudan bellek haritalaması ve Zstd streaming açma uygular:

```c++
bool RowlPkgDataSource::read(const std::string& path, std::vector<uint8_t>& outBuffer) {
    uint64_t hash = computeFnv1a64(path);
    auto it = m_index.find(hash);
    if (it == m_index.end()) return false;

    const auto& entry = it->second;
    m_fileStream.seekg(entry.offset, std::ios::beg);

    if (entry.flags & 1) { // Zstd Sıkıştırılmış
        std::vector<uint8_t> compressed(entry.compressedSize);
        m_fileStream.read(reinterpret_cast<char*>(compressed.data()), entry.compressedSize);
        outBuffer.resize(entry.uncompressedSize);
        size_t dSize = ZSTD_decompress(outBuffer.data(), entry.uncompressedSize, compressed.data(), entry.compressedSize);
        return !ZSTD_isError(dSize);
    } else { // Ham Bayt
        outBuffer.resize(entry.uncompressedSize);
        m_fileStream.read(reinterpret_cast<char*>(outBuffer.data()), entry.uncompressedSize);
        return true;
    }
}
```

---

### 4.5 Ses ve DSP Filtre Motoru (`audio_engine.hpp` / `audio_engine.cpp`)

Ses motoru SDL3 Audio Stream üzerinde 4 adet gerçek zamanlı DSP filtresi uygular:
- **Normal**: Doğrudan geçiş (Direct pass-through).
- **Telephone**: Band-pass filtre (300 Hz - 3400 Hz).
- **Underwater**: Low-pass cutoff filtre (800 Hz).
- **CaveReverb**: Basit geri beslemeli yankı (Feedback delay buffer).
- **Voice Ducking**: Karakter konuştuğunda BGM kazancını %30'a düşürür, diyalog bitince yumuşakça %100'e yükseltir.

---

### 4.6 Güvenli Lua 5.4 Sandbox (`lua_sandbox.hpp` / `lua_sandbox.cpp`)

- **Kara Liste**: `os`, `io`, `debug`, `package`, `dofile`, `loadfile` tamamen silinir.
- **Döngü Koruması**: `lua_sethook` ile her kod bloğuna **10,000,000 talimat limiti** konur. Sonsuz döngü tespit edildiğinde motor çökmeden yakalanır (`catch runtime error`).
- **Değişken Köprüsü**: `rowl.var_set("affinity", 95)` ve `rowl.getVariable("affinity")`.

---

### 4.7 SDL3 Render & Offscreen Framebuffer (`window.hpp` / `window.cpp`)

Motor, Editör içerisinde çalışırken bağımsız bir pencere açmak yerine **1920x1080 RGBA32 Offscreen Framebuffer** üretir. `RowlEngine_GetFramebuffer` çağrısı bu bellek bölgesinin işaretçisini C# Avalonia `WriteableBitmap` nesnesine zero-copy olarak kopyalar.

---

## 5. C# AVALONIA UI EDİTÖR MİMARİSİ (.NET 10)

### 5.1 `MainWindowViewModel.cs` — Çekirdek Mantık & Durum Yönetimi

Editörün beyni olan `MainWindowViewModel.cs`, CommunityToolkit.Mvvm kullanarak tüm durumu koordine eder:

- **Viewport Pan & Zoom**: Smooth lerp animasyonu ile hedef pan `(TargetPanX, TargetPanY)` ve zoom `(TargetZoom)` aralığı: `%15` ile `%400`.
- **Bölünmüş Ekran (Split Screen)**:
  - `0`: Tekli mod (Node Graph / Preview / Game View).
  - `1`: Yatay Bölünme (Üstte Node Graph, altta Canlı Oyun).
  - `2`: Dikey Bölünme (Solda Node Graph, sağda Canlı Oyun).
- **Otomatik Kaydetme (AutoSave)**: `DispatcherTimer` tabanlı periyodik kaydetme ve toast bildirimi.

---

### 5.2 ⚙️ Ayarlar Sistemi (`SettingsViewModel.cs` & `SettingsDialog.axaml`)

`Ctrl+,` veya araç çubuğundaki **⚙️ butonu** ile açılan 5 sekmeli ayarlar penceresi:
1. **🔨 Build**: Platform seçimi (Windows .exe, Linux ELF, macOS .app, Android APK, iOS IPA) ve çıktı dizini.
2. **🎨 Tema**: 4 adet hazır tema kartı ve anlık uygulama motoru.
3. **⚙️ Genel**: AutoSave aralığı, Izgara Yapışması (Grid Snapping), Kablo Stili (Bézier / Düz), FPS Overlay, Dil (Türkçe / English).
4. **⌨️ Kısayollar**: Hiyerarşik kısayol referans tablosu.
5. **ℹ️ Hakkında**: Sürüm, SDL3, Avalonia ve platform mimari bilgileri.

---

### 5.3 🎨 Dinamik Tema Motoru (`ThemeStyles.axaml`)

Tüm renkler `DynamicResource` olarak tanımlanmıştır. `SettingsViewModel.ApplyTheme(themeName)` çağrıldığında `Application.Current.Resources` içerisindeki renkler anında ezilir ve tüm pencereler yeniden çizilir:

| Renk Anahtarı | Rowl Cyber Dark | Midnight OLED | Unreal Slate | Nordic Emerald |
|---|---|---|---|---|
| `AppBackground` | `#121218` | `#000000` | `#1A1A1A` | `#0F1A14` |
| `SurfaceBackground` | `#1E1E2A` | `#0A0A12` | `#2A2A2A` | `#162420` |
| `PanelBackground` | `#181822` | `#050510` | `#222222` | `#121E1A` |
| `AccentColor` | `#38BDF8` | `#A78BFA` | `#F59E0B` | `#34D399` |
| `AccentButtonBg` | `#2563EB` | `#7C3AED` | `#D97706` | `#059669` |

---

### 5.4 ↩️ Undo / Redo Komut Yığını (`UndoRedoService.cs`)

Komut deseni (Command Pattern) ile 50 adımlık işlem geçmişi tutulur:
- `AddNodeUndoAction`: Düğüm eklemeyi geri alır veya yineler.
- `DeleteNodeUndoAction`: Silinen düğümü ve bağlı olduğu tüm kabloları pozisyonlarıyla birlikte geri getirir.
- `DisconnectCablesUndoAction`: Ayrılan kabloları eski pinlerine yeniden bağlar.

---

### 5.5 🔔 Toast Bildirim Altyapısı (`ToastService.cs`)

- Singleton deseni ile tüm ViewModel'lerden `ToastService.Instance.Show("Mesaj", ToastType.Success)` şeklinde çağrılır.
- Sağ alt köşede 3-4 saniye boyunca görünür ve otomatik kaybolur.

---

### 5.6 🏠 Proje Hub Sistemi (`ProjectHubViewModel.cs` & `ProjectHubWindow.axaml`)

- Editör ilk açıldığında Minecraft tarzı kart listesiyle projeleri sunar.
- Her proje için kapak resmi (`cover.png` veya seçilen görsel) yüklenir.
- Yeni proje oluşturma (`CreateProjectDialog`), yeniden adlandırma (`RenameProjectDialog`) ve silme işlemleri tam izolasyonla yapılır.

---

### 5.7 Görsel Arayüzler

- **2 Satırlı Üst Araç Çubuğu (Toolbar)**:
  - Üst Satır: Logo + 🏠 Hub + `🕸️ Graph` / `🎬 Preview` / `🎮 Game` / `⊞ Split Screen` + Pencereler Menüsü.
  - Alt Satır: `💾 Kaydet` + `↩ Geri` + `↪ İleri` + `🔍 Ara` + `+ Node` + `✂ Ayır` + `🗑 Sil` + Platform Menüsü + `🔨 Build Al` + `▶ Play` + `⚙️ Ayarlar`.
- **Alt Durum Çubuğu (Status Bar)**: Durum metni, anlık Zoom `%` ve seçili platform göstergesi.
- **Spotlight Hızlı Arama (`Ctrl+F`)**: Arama kutusuna yazılan başlık veya metne göre ilgili Node'a kamerayı otomatik odaklar.

---

## 6. ÇOKLU PLATFORM TİCARİ DAĞITIM VE DIŞA AKTARMA (STORE & STEAM PIPELINES)

`MainWindowViewModel.BuildGame()` çağrıldığında hedef platforma göre aşağıdaki ticari paketler eksiksiz üretilir:

### 6.1 Linux / Steam Deck Paketi
```
[ProjeAdı]_Linux/
├── [ProjeAdı]                   # Ana çalıştırılabilir ELF binary (rowl_player)
├── [ProjeAdı].x86_64            # Steam Deck & 64-bit Linux uyumluluk ikilisi
├── libRowlEngineCore.so         # C++ Motor paylaşımlı kütüphanesi
├── game.rowlpkg                 # Zstd sıkıştırılmış tüm oyun varlıkları
├── run_game.sh                  # Ortam değişkenlerini (LD_LIBRARY_PATH) ayarlayan başlatıcı
├── steam_appid.txt              # Steamworks App ID (Varsayılan: 480)
└── README.txt                   # Ticari kurulum ve Steamworks yapılandırma kılavuzu
```

### 6.2 Windows Ticari Paketi
```
[ProjeAdı]_Windows.zip/
├── [ProjeAdı].exe               # Ana oyun çalıştırıcısı
├── rowl_player.exe              # Yedek çalıştırıcı
├── RowlEngineCore.dll           # C++ Motor DLL'i
├── game.rowlpkg                 # Oyun veri arşivi
├── run_game.bat                 # Başlatma betiği
├── steam_appid.txt              # Steam entegrasyon dosyası
└── README.txt                   # Kurulum kılavuzu
```

### 6.3 macOS Apple App Bundle (.app)
```
[ProjeAdı].app/
└── Contents/
    ├── Info.plist               # Apple Bundle kimlik ve CFBundleExecutable tanımları
    ├── PkgInfo                  # 'APPL????' paket bayrağı
    ├── MacOS/
    │   └── [ProjeAdı]           # Bağımsız macOS Universal Binary
    └── Resources/
        └── game.rowlpkg         # Oyun varlık paketi
```

### 6.4 Android APK / Google Play Pipeline
- `packaging/android/build.sh` betiği `tools/package_assets.py` ile `game_data.rowlpkg` üretir.
- `assets/` altına yerleştirilerek Gradle ile Release APK / AAB derlenir.

---

## 7. GEÇMİŞ HATALAR, KÖK NEDEN ANALİZLERİ VE UYGULANAN DÜZELTMELER

| # | Hata Tanımı | Kök Neden (Root Cause) | Uygulanan Çözüm |
|---|---|---|---|
| 1 | **Paket Açma Hatası (Corrupted Entry)** | `RowlPackageBuilder.cs` 4 baytlık uint yazarken, C++ okuyucusu 8 baytlık uint64 okuyordu (4 bayt kayma). | `(ulong)e.hash` yazılarak 8 baytlık tam hizalama sağlandı. |
| 2 | **Farklı Projenin Dosyalarını Build Alma** | VFS'in `../` ile üst dizinlere tırmanarak Masaüstündeki eski dosyaları okuması. | Directory climbing kaldırıldı, aktif proje yolu VFS'e tek yetkili olarak bağlandı. |
| 3 | **Build Al Butonu Eksikliği** | Platform seçiciye tıklandığı anda kontrolsüz build tetikleniyordu. | Platform seçici (Flyout) ile "🔨 Build Al" butonu birbirinden tamamen ayrıldı. |
| 4 | **Avalonia XML Entity Hatası** | `SettingsDialog.axaml` içinde `&` karakteri yalın kullanılmıştı. | `&amp;` olarak escape edildi. |
| 5 | **ResourceInclude Sözdizimi** | `App.axaml` içinde ResourceInclude doğrudan konulmuştu. | `ResourceDictionary.MergedDictionaries` içine sarıldı. |

---

## 8. SIFIRDAN DERLEME, TEST VE ÇALIŞTIRMA KILAVUZU

### 8.1 Gereksinimler
- **Linux (Ubuntu / Debian / Fedora / Arch)**
- **C++ Derleyici**: `g++` veya `clang++` (C++20 destekli)
- **CMake**: 3.22 veya üzeri
- **.NET SDK**: .NET 10.0 SDK
- **SDL3 & Geliştirme Paketleri**: `libsdl3-dev`, `libzstd-dev`, `liblua5.4-dev`

### 8.2 C++ Motorunu ve Testleri Derleme
```bash
cd "/home/chaple/Belgeler/Rowl Engine"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Testleri çalıştırma:
./bin/rowl_tests
```

### 8.3 C# Avalonia Editörünü Derleme ve Çalıştırma
```bash
cd "/home/chaple/Belgeler/Rowl Engine"
dotnet build editor/RowlEngine.Editor.csproj

# Editörü başlatma:
dotnet run --project editor/RowlEngine.Editor.csproj
```

### 8.4 Python ile Varlık Paketleme (Manuel Test)
```bash
cd "/home/chaple/Belgeler/Rowl Engine"
python3 tools/package_assets.py Assets Assets/packages/game.rowlpkg
```

---

> **Arşiv Notu:** Bu belge, Rowl Engine projesinin tüm mimarisini, veri yapılarını, iş akışlarını ve kaynak kodunu tam olarak açıklar. Yeni bir geliştirici veya yapay zeka ajanı bu belgeyi referans alarak projeyi eksiksiz anlayabilir ve sürdürebilir.
