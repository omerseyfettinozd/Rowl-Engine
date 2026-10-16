# Editör arayüzü — prototipten Avalonia'ya taşıma planı

**Durum:** Faz 1 ✅ · Faz 2 + 2b ✅ · Faz 3 ✅ (2026-10-16) · sıradaki Faz 4. Kod değişikliği yok. Kaynak tasarım:
`prototypes/editor-workspace/` (tasarım şartnamesi olarak dondurulur). Hedef:
`editor/` (Avalonia 11.3, Fluent tema).

## 0. Ölçülen başlangıç durumu

| | Prototip (CSS) | Avalonia editörü |
|---|---|---|
| Renk token'ı | 6 (`style.css :root`) + çok sayıda sabit hex | ~30 (`Styles/ThemeStyles.axaml`) |
| Token kullanımı | — | **931** `DynamicResource`, yalnız **12** sabit hex |
| Yazı boyutu | sabit; 11px (42), 10px (33), 9px (17), 12px (14) | **532** sabit `FontSize`: 10 (219), 12 (127), 11 (119) |
| Köşe yuvarlaklığı | sabit; 6/8/10px baskın, 10 farklı değer | **167** sabit `CornerRadius`: 4, 6, 2, 8 |
| Stil sınıfı | CSS sınıfları | **0** `Classes`, **0** `Style Selector` |
| Yazı tipi | Inter | Inter (`Avalonia.Fonts.Inter`, `WithInterFont`; RowlEye headless da Inter) |

**Sonuç:** renk tarafı hazır — değerleri değiştirmek bütün editörü bir anda boyar.
Asıl iş yazı/köşe/boşluk: bunlar için ne token ne stil sınıfı var.

Not: prototipin kendi değerleri dağınık (ör. 6 farklı neredeyse-aynı koyu gri:
`#0e0e10 #0f0f12 #111113 #111114 #131315 #151518`). Taşırken birebir kopyalanmaz,
aşağıdaki ölçeklere **kuantalanır**.

## Faz 1 — Renk paleti (yalnız `ThemeStyles.axaml` değerleri)

Anahtar adları değişmez → 931 kullanım otomatik güncellenir. Risk düşük, etki en büyük.

| Avalonia anahtarı | Şimdi | Öneri | Prototip kaynağı |
|---|---|---|---|
| AppBackgroundColor | #0A0A0B | #0A0A0B | `--bg` (aynı) |
| SurfaceBackgroundColor | #232327 | **#131315** | `--surface` |
| PanelBackgroundColor | #2A2A2E | **#19191C** | diyalog/kart arka planı |
| InputBackgroundColor | #1B1B1E | **#0F0F12** | input arka planı |
| BorderColorValue | #3A3A40 | **#29292D** | `--line` |
| BorderSubtleColorValue | #4A4A52 | #4A4A52 (değişmez, K1) | input kenarı; prototip #3B3B40 çok soluk |
| PrimaryTextColor | #F2EFE6 | #F2EFE6 | `--bone` (aynı) |
| SecondaryTextColor | #D8D4CC | **#DAD7CF** | `--text` |
| MutedTextColor | #A8A49C | **#85858B** | `--muted` |
| DimTextColor | #6E6C66 | **#66666D** | etiket ikincil |
| AccentButtonBgColor | #3B3B42 | **#19191D** (+kenar #36363A) | `.author-button` |
| AccentButtonHoverColor | #4C4C55 | **#26262B** | `.author-button:hover` |
| ToolbarButtonBgColor | #323237 | **#111113** | kapsül araç çubuğu |
| PlayButtonGreen / Red, DangerButtonBg | yeşil/kırmızı dolgu | **tek renk** (bone kenar/ikon) | prototipte renkli düğme yok |
| Success / Error durum | doygun | prototip tonları: başarı #2C332C/#697267/#D4DDCE, hata #3E2B26/#966A54/#E2BE9E | `.build-result` |

**Kontrast (WCAG, ölçüldü):** bone/surface 16.1:1 · text/surface 12.9:1 ·
muted/surface **5.06:1** (AA geçer) · dim/surface **3.26:1** (yalnız ≥14px kalın ya da
süs metni) · input kenarı: prototip #3B3B40 **1.72:1** → K1 ile #4A4A52 = 2.18:1 + odak vurgusu.

**Doğrulama:** RowlEye ile `MainWindow` + 6 panel önce/sonra; prototip
`previews/desktop.jpg` ile yan yana. Golden'lar bu fazda bilerek güncellenir.

## Faz 2 — Yazı, köşe, boşluk token'ları + stil sınıfları

`ThemeStyles.axaml`'a `x:Double` / `CornerRadius` / `Thickness` kaynakları ve
`Classes` tabanlı stiller eklenir:

- **Yazı ölçeği** (prototip dağılımından): `Caption 9` · `Small 10` · `Body 11` ·
  `BodyLarge 12` · `Title 15` · `Heading 21`. Ağırlık: 400 / 500 / 550→600.
- **Köşe ölçeği:** `Small 6` (düğme, input) · `Medium 8` (kart) · `Large 10`
  (panel/kart büyük) · `Dialog 15`.
- **Boşluk ölçeği:** 4 · 6 · 8 · 10 · 12 · 16 · 24.
- **Stil sınıfları:** `TextBlock.caption/.muted/.title`, `Button.ghost/.primary/.icon`,
  `Border.panel/.card`, `TextBox` varsayılanı.

532 + 167 sabit değer **toptan değil**, Faz 4'te panel panel sınıflara çevrilir
(her panel ayrı commit, RowlEye ile kontrol).

## Faz 3 — Kabuk: kapsül araç çubuğu + panel başlıkları

- İki sıralı üst araç çubuğu → prototipteki tek **kapsül**: Node · Game · Edit Scene |
  Başlat · Duraklat | Araçlar · … (`view-switcher`: 2px iç boşluk, 1px #29292D kenar,
  14px köşe, #111113 zemin, 44×44 düğme).
- Panel başlığı: ikon + başlık + yerleşim/sabitle/kapat düğmeleri.
- Düğme dağılımı K4'e göre; boyutlar K2 (32–36 px).

**Yapılan (2026-10-16):** tek satır üst çubuk; ortada kapsül (`TopCapsule`: Node · Oyun ·
Sahne Düzenle | Oynat · Duraklat · Adım | Araçlar · `…`); `…` menüsü (Projeler, Kaydet,
Geri Al/Yinele, Dışa Aktar/İptal, Düzen önayarları, Bölünmüş Ekran, Ayarlar); kanvasta Node
araç çubuğu; kaydedilmemiş değişiklik noktası (`IsProjectDirty`); tüm metinler katalogda.
Kısayollar değişmedi (pencere kodunda). **Faz 4'e devredildi:** panel başlıkları (sabitle/kapat
davranışı Faz 5 pencere sistemiyle birlikte anlamlı).

## Faz 4 — Paneller tek tek

Sıra (görünürlük × risk): Node grafiği → Inspector → Hiyerarşi → Varlıklar →
Günlük/Konsol → Ayarlar diyaloğu. Her panelde: sabit değerleri sınıflara çevir,
prototip düzenine yaklaştır, TR/EN karışık etiketleri birleştir, RowlEye önce/sonra.
Bilinen sorunlar: Inspector'da taşan hash metni ve kesilen bileşen adları
("Görsel Backgr…").

## Faz 5 — Bölünebilir pencere sistemi (en son, en riskli)

Prototipte: sürükle-bırak ile bölme, sabitleme, 180–200 ms geçiş. Önce küçük bir
**deneme (spike)**: Dock.Avalonia kütüphanesi mi, kendi `Grid`+`GridSplitter`
düzenimiz mi? Karar spike ölçümüyle verilir, sonra uygulanır.

**Karar (2026-10-16): kendi `Grid`+`GridSplitter` düzenimiz** — prototipteki ikili bölme
ağacının (`layout-state.mjs`) C# karşılığı. Spike ölçümleri (net10.0 + Avalonia 11.3.11):
- Model uyumu: prototipte sekme/yüzen pencere yok, "sabitle" = taşıma kilidi. Dock.Avalonia
  IDE tarzı (sekme, yüzen pencere, pusula; "pin" = kenara otomatik gizle); uydurmak için
  `CanDrag/CanFloat=false` + pusulanın yeniden stillenmesi gerekir. Kendi model ~100 satır.
- Kaydet/yükle: kendi model 454 bayt JSON, geri yükleme bayt bayt aynı. Dock: System.Text.Json
  serileştiricisi döngü hatası; Newtonsoft yolu 25 KB + `$type` sınıf adları (yeniden adlandırma
  kayıtlı düzeni bozar) + ek bağımlılık.
- Tema (K2): kendi yol `Border.panelHeader` ile doğrudan 40 px; Dock'ta ~21 token eşlemesi
  gerekti, başlık yüksekliği yine ~24 px'te kaldı (şablon yeniden yazımı gerekir).
- Avalonia 12: Dock 11.x serisi durdu; geçişte Dock da kilitli yükseltilmeli. Kendi yol yalnız
  çekirdek Grid/GridSplitter/işaretçi API'si.
- Test: kendi yolun headless fare testleri 9/9 (bölme, sabit kilidi, kapatma, JSON, RTL bırakma).
  Bağımlılık: Dock 11 paket ~1.7 MB; kendi yol 0.
- Dock'un artıları: MIT, aktif, RTL hazır — yüzen pencere/sekmeli grup istenirse yeniden düşünülür.

**Dilimler (her biri ayrı test edilir; push yalnız faz sonunda):**
- D1 — Saf model (`layout-state.mjs` karşılığı) + xUnit (`layout-state.test.mjs` senaryoları,
  1000 rastgele işlem değişmezliği dahil).
- D2 — ViewModel panel bayrakları ↔ ağaç eşitlemesi; düzen önayarları ve Bölünmüş Ekran ağaç
  üretir; önayar/tema testleri güncellenir.
- D3 — `WorkspaceHost` görünümü: `panelHeader` başlıklar, `{l:T}` metinler, tek `NodeGraphView`
  örneği; MainWindow geçişi; RowlEye altın görüntüsü yenilenir.
- D4 — Başlıktan sürükle-bırak: kenar vurgusu, Escape iptali, sabit kilidi, klavye/menü
  alternatifi; headless fare + RTL testleri.
- D5 — Düzenin `EditorSettingsProfile`'a kalıcı kaydı, sürüm alanı, bozuk dosyada varsayılan.
- D6 — 200/180 ms geçişler, azaltılmış hareket, bölücüyü klavyeyle taşıma.
- Alt şerit kararı (D3): tek yaprak, içi sekmeli (beş ince panel ayrı yaprak olarak sıkışık).

**Yapılan (2026-10-16):** D1–D6 tamam (9f01837, df3e174, ca88fa7, a881c21, bc2c8c3, b5548a2);
editör süiti 577 → 814 test. Bilinen/sonraya: kapanan panelin çıkış animasyonu yok; OS
"azaltılmış hareket" yalnız `ROWL_REDUCE_MOTION` ile (Tercihler'e bağlanmadı); kayıtlı düzende
bilinmeyen panel tüm düzeni varsayılana düşürür (kısmi kurtarma yok); `new MainWindowViewModel`
kuran eski test sınıfları UI iş parçacığı dışında kuruyor (D5'teki kirlilik deseni — denenen
sırada üretilmedi, ortak test temeliyle kapatılmalı).

## Kararlar (Chaple, 2026-10-15)

| # | Konu | Karar |
|---|---|---|
| K1 | Input kenarı | Prototipten bir ton açık (#4A4A52 = 2.18:1) **ve** fare üstü/odakta belirginleşir. |
| K2 | Boyut | Masaüstü yoğunluğu: düğme/satır **32–36 px** (prototipin 44 px dokunma boyutu değil). "Rahat/Sıkı" seçeneği ileriye ertelendi. |
| K3 | Renk | Düğmeler tek renk (Oynat yeşili, Sil kırmızısı kalkar). Renk yalnız **anlam** taşır: hata/uyarı/başarı (prototipin soluk tonları) + kullanıcının düğüm renk etiketleri. |
| K4 | Düğme yerleşimi | **Kapsül:** Node · Oyun · Sahne Düzenle \| Oynat · Duraklat · Adım \| Araçlar · `…` — **`…` menüsü:** Projeler, Kaydet, Geri Al/Yinele, Dışa Aktar (eski "Linux Build"), Düzen, Ayarlar — **Node paneli araç çubuğu:** +Node, Bağlantıyı Kes, Sil, +Grup, Üst — Ctrl+S/Z/Y korunur; kaydedilmemiş değişiklikte kapsülde nokta. |
| K5 | Dil kapsamı | **Editör arayüzü + motorla yapılan oyunlar** çok dilli. Geliştirmede Türkçe; çıkışta tam donanımlı çok dil. |
| K6 | Yazı sistemleri | Altyapı ve paneller baştan **sağdan-sola (RTL) uyumlu** ve CJK'ye hazır (yazı tipi geri dönüş zinciri). İlk çıkışta `tr` + `en` dolu; CJK yazı tipi (Noto) ve RTL diller çeviri geldikçe. |
| K7 | Dil değişimi | **Canlı**, yeniden başlatmasız. |
| K8 | Çeviri biçimi | Motorun mevcut hattı: JSON katalog + CSV alış-veriş + sahte dil `qps-ploc` (bkz. `LOCALIZATION_CONTRACT.md`, `LOCALIZATION_EDITOR_CONTRACT.md`). Tek çeviri masası oyun + editör. |
| K9 | Önizleme dili | Editör dili (kişisel tercih) ile oyun önizleme dili (araç çubuğundan) **ayrı**. |

## Faz 2b — Editör çok dil altyapısı (Faz 2 ile birlikte, Faz 4'ten önce)

Ölçülen durum: editör arayüzünde çeviri sistemi yok; metinler koda gömülü (üst krom
Türkçe birliği testle kilitli: `EditorChromeLanguageTests`). Oyun tarafında BCP 47
diller, geri dönüş zinciri, HarfBuzz/FriBidi şekillendirme, `qps-ploc` ve CSV hazır.
Oyuncu profili şu an yalnız `en`, `tr` listeliyor.

1. **Servis:** `EditorStrings` — aktif dil, geri dönüş (etkin → `en` → `tr` → anahtar), canlı
   değişimde `PropertyChanged` ile tüm bağlı metinleri yeniler (K7).
2. **XAML kullanımı:** işaretleme uzantısı, ör. `Text="{l:T Inspector.Title}"`;
   C# tarafı `EditorStrings.Get("…")`. Komut kimlikleri (CommandParameter) çevrilmez.
3. **Dosyalar:** `editor/Assets/locales/editor.<dil>.json` → çıktıda `locales/` (gömülü kaynak
   değil: yeni dil yeniden derlemeden eklenir) — K8 katalog şeması; `tr` kaynak dil, `en` ikinci dil. Eksik anahtar sessiz değil: geliştirmede uyarı logu.
4. **RTL:** `FlowDirection` dil özelliğinden; paneller sola/sağa sabit koordinat yerine
   `Start/End` mantığıyla kurulur; ikon yönleri (oklar) aynalanabilir.
5. **Yazı tipi zinciri:** Inter → Noto Sans (Arapça/İbranice) → Noto Sans CJK; CJK
   paketi ayrı/isteğe bağlı (boyut).
6. **Kültür güvenliği:** büyük/küçük harf ve sayı biçimi `CultureInfo` ile; Türkçe
   İ/ı tuzağı için anahtar karşılaştırmaları sıralı-değişmez (ordinal).
7. **Kapılar:** (a) `qps-ploc` ile RowlEye denetimi — taşan/kesilen metin görünür,
   (b) koda gömülü metin taraması (XAML'de `Text="…"` sabit Türkçe/İngilizce),
   (c) `tr` ile `en` anahtar kümeleri eşit.
8. **Önizleme dili (K9):** kapsülde/araç çubuğunda dil seçici → native
   `LocalizationManager` aktif dili; editör dilinden bağımsız.

Faz 4'te her panel taşınırken metinleri de anahtarlara çıkarılır (ayrı bir "çeviri
seferi" yok).

**Yapılan (2026-10-16):** 1, 2, 3, 4 (FlowDirection bağlı), 6 (ordinal anahtar), 7(b) mandal
(taban 651 metin / 35 dosya, `editor/Tests/editor-hardcoded-text-baseline.json`), 7(c);
ayarlarda dil seçici; ilk dilim ana pencere menü/sekmeleri. **Kalan:** 5 (CJK/RTL yazı tipi
paketleme — çeviri gelince), 7(a) `qps-ploc` RowlEye denetimi, 8 önizleme dili seçici (Faz 3
kapsülüyle). Not: Avalonia indeksleyici bildirimi `"Item"` adını dinler (WPF'teki `"Item[]"` değil).

**Faz 2 yapılan:** yazı/köşe/boşluk/yükseklik token'ları, `Styles/ControlStyles.axaml`
sınıfları (caption, muted, body, title, heading; ghost, primary, icon; panel, card;
ToggleButton.header), Fluent vurgusu kemik tonuna (vurgu üstü ön planlar koyu), K1 input
kenar/odak, liste/ağaç seçimi prototip grisi.

## Her fazda kapı

- `dotnet test` (RowlEye + editör testleri) yeşil; RowlEye golden'ları yalnız o fazın
  hedef panelleri için ve bilerek güncellenir.
- Önce/sonra RowlEye görüntüsü + prototip görseli yan yana (PR açıklamasına eklenir).

## Faz 7 — Düğüm kütüphanesi ve Lua editörü panelleri (2026-10-16)

Kaynak: prototip `node-library.js` + `node-library-state.mjs` (+ testleri), `lua-editor.js`,
görseller `library-categories.jpg`, `inspector-library.jpg`, `desktop.jpg` (Lua editörü).
Kapsam prototipteki davranıştır; yeni ürün özelliği eklenmez.

**Kararlar**
- *Benim düğümlerim* kullanıcı düzeyinde saklanır (prototipte tarayıcı başına `localStorage`):
  editör ayar profiliyle aynı dizinde `node-library.json`, sürümlü, atomik yazım; bozuk dosya
  sessizce sıfırlanmaz (yedeklenir + uyarı). *Hazır paketler* salt okunur: prototipteki gibi
  `Rowl` paketi → `Standart düğüm` (bugünkü varsayılan yeni düğüm). Uydurma içerik yok.
- Kütüphane kaydı bağımsız şablondur: düğümün tüm objeleri, konum/görünürlük ve bileşen
  değerleri; bağlantı hedefleri (seçenek/koşul hedefleri) boşaltılır; sahneye eklenen kopya yeni
  kimlik ve yeni diyalog `content_id` alır (çoğaltma kuralıyla aynı); ekleme geri alınabilir.
- Klasör kuralları prototiple aynı: iç içe, aynı konumda aynı ad yok (TR büyük/küçük harf),
  kendi içine taşınamaz, silinen klasörün alt klasör/öğeleri üst konuma taşınır (çakışan ad
  `Ad (2)` alır), kaynak kayıtlar korunur.
- Lua editörü projedeki `.lua` dosyalarını sekme olarak açar; kayıt diske (atomik) yazar;
  kirli nokta, Ctrl+S, `+` → `script-N.lua`, arama, satır/sütun, Tab = 2 boşluk. *Bağlı obje*
  seçimi o objenin Betik bileşeninin `ScriptPath`'ini etkin dosyaya bağlar (geri alınabilir).
  Kod alanı: `Avalonia.AvaloniaEdit` 11.3.0 (MIT); Lua renklendirmesi tema token'larıyla
  (prototip `.lua-comment/keyword/string/number`).

**Dilimler**
- D1 Kütüphane modeli (saf C#) + JSON kalıcılık + prototip testlerinin taşınması.
- D2 Düğüm ↔ şablon dönüşümü (yakala / sahneye ekle, geri al) + testler.
- D3 Kütüphane paneli (`PanelIds.Library`: Hazır paketler / Benim düğümlerim, klasör ağacı,
  yol, arama, Sahneye ekle, klasör yönet) + Denetçi'de *Düğüm kütüphanesi* bölümü
  (yıldızla kaydet / kaldır, Kaydı güncelle) + Araçlar menüsünde kart.
- D4 Lua çalışma alanı modeli (dosya listesi, aç/kirli/kaydet/yeni, obje bağlama) + testler.
- D5 Lua editörü paneli (`PanelIds.Lua`, AvaloniaEdit) + Araçlar kartı.
- D6 Yerleşim/kısayol, RowlEye görüntüsü + prototip karşılaştırması, kilit testleri.

Test kilitleri bilinçli güncellenir: Araçlar menüsü 7 → 9 kart (Ctrl+8 / Ctrl+9 kısayolları).
