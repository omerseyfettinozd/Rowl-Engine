# Rowl Engine — ajan yönlendiricisi

Ortak hafıza: `/home/chaple/second-brain`. Ortak kurallar `🔮 850-Companion/Kurallar.md`; proje kuralları `🔮 850-Companion/Scoped-Rules.md` içindeki **Rowl Engine** bölümüdür. Bağlamda zaten yüklüyse tekrar okuma.

Mimari: `300-Projects/Rowl-Engine.md`; görevle ilgili `Rowl-Engine-Core`, `Render`, `VFS`, `Bridge` veya `Editor` notunu seç. Güncel durum `Rowl Engine Dökümantasyon Listesi/IMPLEMENTATION_STATUS.md`; yalnız devam/planlama işinde başvur.

Proje sınırları: C++20/CMake/SDL3; .NET 10/Avalonia 11/CommunityToolkit.Mvvm; `c_api.h` ile P/Invoke köprüsü. Kaynak ömrünü RAII ile yönet; köprü değişikliklerinde native/managed sözleşme uyumunu doğrula.

Git işlemlerinde mevcut SSH ve doğrulanmış kullanıcı kimliğini koru. Görev kapsamıyla commit/push yap; mevcut dalı veya teslim düzenini izle, `main` varsayma.
