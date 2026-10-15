# Rowl.Eye — Native Görsel Denetim, Regresyon ve Canlı Web Inspector Altyapısı

## 1. Genel Bakış
`Rowl.Eye`, Rowl Engine C# Avalonia 11 masaüstü editörü ve C++20 çekirdeği için tasarlanmış deterministik görsel denetim, snapshot alma, durum mühürleme, piksel ısı haritası (`diff`) karşılaştırma, CI regresyon kapısı, canlı kontrol ağacı denetçisi (`Visual Tree DevTools`) ve canlı yerel web denetçisi (`Local Web Inspector`) sistemidir.

Web prototiplerindeki DOM/CDP kısıtlarını aşarak:
1. Avalonia Headless platformunda Skia grafik motoru üzerinden doğrudan piksel render alır (Diyaloglar, izole paneller ve **1920x1080 tam ekran MainWindow** dahil).
2. Eşzamanlı olarak C# ViewModel durumunu ve kontrol hiyerarşisini (**Visual Tree Bounds**) JSON olarak mühürler.
3. İki görsel arasındaki piksel farklarını **mor/kırmızı ısı haritası (`diff.png`)** ile raporlar.
4. **Dinamik Maskeleme / Ignore Regions** desteği ile saat, animasyon ve gürültülü alanları diff testlerinden muaf tutar.
5. **Durum Varyantları (State Variants)** ile panellerin boş (`Empty`), dolu (`Populated`), seçili (`Selected`) veya arama aktif (`SearchActive`) hallerini deterministik doğrular.
6. **Golden Master** referansları ile otomatik tolerans/eşik regresyon testleri yürütür (`--audit`).
7. Kendi kendine yeten, modern koyu temalı **İnteraktif HTML Denetim Raporu** üretir (`--report`).
8. **Canlı Yerel Web Inspector** (`--serve` / `http://localhost:7070`) üzerinden herhangi bir modern tarayıcıdan motor panellerini anlık canlı izleme, fareyle kontrol sınır kutularını (**Inspect Element Highlight Box**) inceleme, split-slider ile altın referansla kıyaslama ve tek tıkla snapshot onaylama imkanı sunar.

---

## 2. 5-Fazlı Sistem Mimarisi

```
                                  [Rowl.Eye CLI & Runner]
                                             │
      ┌─────────────────┬────────────────────┼─────────────────┬─────────────────┐
      │                 │                    │                 │                 │
  [FAZ 1]            [FAZ 2]              [FAZ 3]           [FAZ 4]           [FAZ 5]
İzole Paneller   Golden Master &       İnteraktif HTML    Local Web        Visual Tree DevTools
 & Varyantlar    Regresyon Kapısı        Raporlayıcı      Inspector        & Maskeleme (Ignore)
      │                 │                    │                 │                 │
FixtureFactory    Auditor & Differ      ReportGenerator   WebServer & UI   VisualTreeService &
  (Variants)    (MaskedComparator)      (audit-report)   (Live Stream)      IgnoreRegion Masks
      │                 │                    │                 │                 │
Skia PNG + HiDPI  audit.json + Heatmap    Koyu Tema +     Split-Slider +   Hover Highlight Box
 + ViewModel      (Passed / Failed)     Filtre Butonları   Tek Tık Onay     + Ignored Overlays
```

### Dizin Yapısı (`editor/Tools/RowlEye/`)
* **`RowlEyeConfig.cs`**: CLI argüman ayrıştırıcısı (`--target`, `--theme`, `--variant`, `--scales`, `--audit`, `--update-goldens`, `--serve`, `--port`, `--diff`, `--ref`, `--actual`, vb.).
* **`RowlEyeRenderer.cs`**: Skia tabanlı Avalonia `RenderTargetBitmap` ve `HeadlessWindowExtensions` kullanarak fiziksel ekran açmadan 1x, 1.25x, 2x çözünürlüklerde PNG render eden çekirdek (`RenderWindow`, `RenderWindowToBytes`).
* **`RowlEyeSealer.cs`**: Görsel kareyle birlikte ViewModel durumunu ve koşu manifestini (`manifest.json`) döngüsel bağımlılıklara karşı korumalı biçimde atomik olarak mühürleyen serileştirici.
* **`RowlEyeDiffer.cs`**: SkiaSharp ile çalışan piksel hassasiyetli görsel karşılaştırma motoru. Eşleşen alanları karartılmış gri tonlamada (0.35x) tutarken, değişen pikselleri fark şiddetine göre Mor (`#FF00FF`) -> Kırmızı (`#FF0000`) ısı haritası olarak boyar. `IgnoreRegions` ile maskelenen bölgeleri nötr mavi olarak boyayıp fark sayacından düşer.
* **`RowlEyeHeadlessRuntime.cs`**: Linux, macOS ve Windows'ta Avalonia Headless platformunu adanmış STA/UI thread ve sürekli `MainLoop` döngüsü ile çalıştıran, `HttpListener` gibi asenkron ThreadPool isteklerinde deadlock oluşmasını engelleyen yaşam döngüsü yöneticisi.
* **`Inspection/`**:
  * `VisualNodeDto.cs`: Hiyerarşik kontrol tipi, adı, root'a göre koordinatları (`X, Y, Width, Height`) ve görünürlük DTO'su.
  * `VisualTreeService.cs`: Avalonia Visual Tree'sini gezerek mutlak koordinatları hesaplayan servis.
* **`Targets/`**:
  * `RowlEyeTarget.cs`: `MainWindow`, `SettingsDialog`, `NodeGraph`, `Hierarchy`, `Inspector`, `AssetBrowser`, `LivePreview`, `All`.
  * `RowlEyeIsolationHost.cs`: Panelleri bağımsız olarak barındıran ve temalandıran kapsayıcı.
  * `RowlEyeFixtureFactory.cs`: Deterministik ViewModel, mock servisler ve State Variants (`Empty`, `Populated`, `Selected`, `SearchActive`) üreten fabrika.
* **`Goldens/`**:
  * `RowlEyeGoldenManager.cs`: `__goldens__` klasör yönetimi, yedekleme (`.bak`) ve altın referans güncelleme.
  * `RowlEyeIgnoreRegion.cs`: Dinamik alanları (saat, imleç, animasyon) maskeleme sınır kutusu.
  * `RowlEyeThresholds.cs`: Hedef bazlı tolerans, hata eşikleri ve ignore regions kuralları.
  * `RowlEyeAudit.cs`: Canlı render ile golden master'ı kıyaslayıp `audit.json` üreten regresyon kapısı.
* **`Reporting/`**:
  * `RowlEyeReportGenerator.cs`: Bağımsız, zero-dependency, modern koyu temalı, filtreleme düğmeli (All/Failed/Theme), görsel ızgaralı ve collapsible C# ViewModel JSON detaylı HTML raporlayıcı.
  * `RowlEyeReportModel.cs`: Rapor veri modeli.
* **`Serve/`**:
  * `RowlEyeWebServer.cs`: Minimal `System.Net.HttpListener` tabanlı, Skia on-demand PNG, ViewModel JSON ve Visual Tree JSON akıtan REST/HTTP sunucusu.
  * `inspector.html`: Modern cyber/dark temalı, offline çalışan, split-slider karşılaştırma, interaktif fareyle kontrol inceleme (Inspect Element Highlight Box) ve tek tıkla snapshot onaylama arayüzü.

---

## 3. Komut Satırı Kullanımı

### A. Local Web Inspector Başlatma
Tarayıcıdan masaüstü motor panellerini canlı izlemek, kontrollerin sınır kutularını (Visual Tree) hover ile incelemek ve tek tıkla altın referans onaylamak için:
```bash
dotnet run --project editor/RowlEngine.Editor.csproj -- --rowl-eye --serve --port 7070
```
Tarayıcınızda açın: `http://localhost:7070`
* `GET /api/targets`: Kullanılabilir hedeflerin JSON listesi.
* `GET /api/snapshot?target=X&theme=Y&scale=1.0`: Canlı Skia PNG akışı.
* `GET /api/visual-tree?target=X&theme=Y`: Canlı Avalonia Visual Tree hiyerarşisi ve Bounds koordinatları.
* `GET /api/viewmodel?target=X&theme=Y`: Mühürlenmiş ViewModel durumu.
* `GET /api/golden-image?target=X&theme=Y`: Altın referans PNG'si.
* `GET /api/diff-image?target=X&theme=Y`: Maskelenmiş fark ısı haritası PNG'si.
* `GET /api/audit?theme=Y`: Hedeflerin regresyon durumu listesi.
* `POST /api/goldens/update?target=X&theme=Y`: Anlık görüntüyü golden master olarak mühürleme.

### B. CI Regresyon Audit & HTML Raporu
```bash
dotnet run --project editor/RowlEngine.Editor.csproj -- --rowl-eye \
  --audit --report --target All --theme All --scales 1,2 \
  --goldens "editor/__goldens__" --out "/tmp/audit-results"
```

### C. Golden Master Güncelleme
```bash
dotnet run --project editor/RowlEngine.Editor.csproj -- --rowl-eye \
  --update-goldens --target All --theme Dark --scales 1,2 --goldens "editor/__goldens__"
```

### D. İzole Snapshot Alma
```bash
# 1. SettingsDialog'u 1x, 1.25x ve 2x HiDPI ölçeklerinde render et:
dotnet run --project editor/RowlEngine.Editor.csproj -- --rowl-eye --target SettingsDialog --scales 1,1.25,2 --out "/tmp/snapshots"

# 2. Tam Ekran MainWindow 1920x1080 render et:
dotnet run --project editor/RowlEngine.Editor.csproj -- --rowl-eye --target MainWindow --scales 1 --width 1920 --height 1080 --out "/tmp/snapshots"
```

---

## 4. Test Paketi & Doğrulama (22/22 Test Yeşil)

Sistem `editor/Tests/RowlEye/` altında 5 test sınıfı ve 22 xUnit testi ile %100 kapsama sahiptir:
```bash
dotnet test editor/Tests/RowlEngine.Editor.Tests.csproj --filter "FullyQualifiedName~RowlEye"
```

| Test Sınıfı | Test Sayısı | Doğrulanan Kapsam |
| :--- | :---: | :--- |
| `RowlEyeSmokeTests` | 6 | Argüman ayrıştırma, mühürlü yapıtlar, özdeş fark (%0.000), sentetik ısı haritası, IgnoreRegion maskeleme baypası ve State Variants üretimi |
| `RowlEyeTargetTests` | 6 | 6 izole panelin (SettingsDialog, NodeGraph, Hierarchy, Inspector, AssetBrowser, LivePreview) render'ı |
| `RowlEyeGoldenTests` | 3 | Altın referans güncelleme döngüsü, kırık görsel tespiti ve eksik referans kapısı |
| `RowlEyeReportTests` | 1 | Zero-dependency interaktif HTML raporunun üretimi, filtreler ve DOM doğrulaması |
| `RowlEyeServeTests` | 6 | Dinamik port dinleme, offline HTML sunumu, Skia PNG sihirli baytları, ViewModel JSON, Audit roundtrip ve Visual Tree API doğrulaması |
| **Toplam** | **22** | **Tümü Başarılı (0 Hata, ~4 saniye)** |
