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

**Faz 7 yapılan (2026-10-16):** D1 `b75f77d`, D4 `5c9b309`, D2 `b7d55d3`, D3+D5+entegrasyon `5972271`,
görsel düzeltmeler (Muse Wonder denetimi: kütüphane 8/10, Lua 7/10 → düğüm ikonu, ayrık "…" hücresi,
kutusuz bağlı obje seçici, düz "Lua" etiketi, Denetçi düğmelerinde normal ağırlık). Bilerek alınmayanlar:
başlıkta sürekli üç düğme (sessiz başlık kararı), Lua'da "Dosyalar" ve sekme kapatmanın kaldırılması
(gerçek projede dosya sayısı sınırsız). Yerel: editör 877/877, RowlEye golden + denetim 3/3.

## Faz 8 — Prototip kalanları + Aşama B (B04, B05, B06) (2026-10-16)

Karar (Chaple, 2026-10-16): kapsam **hepsi** — prototipte kalanlar ve yol haritası Aşama B'nin
B04/B05/B06 paketleri; tuval zemini prototip yüzeyi **#131315** (#000 değil).

**Ölçülen başlangıç (koddan):**
- `PanelIds.Console` ve `PanelIds.Store` tanımlı ama ayrı panel olarak barındırılmıyor; günlük alt
  şeritte (`OutputLogView`). Araçlar menüsünde Konsol / Assets Store kartı yok (prototipte var).
- Kurtarma: `CrashRecoveryService` teklif üretiyor, ama kullanıcı yüzeyi yok — yalnız günlüğe
  "RestoreFromRecovery ile geri yükleyin" yazılıyor; yoksay (dismiss) yolu yok.
- İçe aktarma: `ImportAssetAsync` async çekirdeği **senkron** çağırıyor (UI donar), ilerleme/iptal
  yok; aynı adlı varlık `File.Copy(overwrite: true)` ile **sessizce eziliyor** (B04 bitiş kriterini çiğner).
- Proje Merkezi: kart yolu silinmiş projeyi ayırt etmiyor; işlem sonuçları yalnız durum metni.
- Erişilebilirlik: editör tarafında yalnız "azaltılmış hareket" tercihi var; arayüz metin ölçeği,
  panel odak döngüsü, görünür odak halkası yok.

**Dilimler**
- **A1 Tuval zemini** — `CanvasBackgroundColor` #000000 → #131315 (ızgara/minimap kontrastı korunur);
  tema kilit testi ve RowlEye golden'ı bilerek yeni değere taşınır.
- **A2 Konsol paneli** — `PanelIds.Console` kendi yaprağı: prototip `.console-toolbar` (soluk
  "Oturum olayları" + Temizle) ve zaman + mesaj satırları (eş aralıklı, ayırıcı çizgi); mevcut
  günlük kaynağı (`LogOutput`) ve sayaçlar korunur. Araçlar menüsüne kart.
- **A3 Assets Store** — `PanelIds.Store` "Yakında gelecek" boş ekranı (ikon + başlık + alt satır),
  Araçlar kartı. Araçlar menüsü kilidi 9 → 11 kart, prototip sırasıyla.
- **B1 Kurtarma kartı** — açılışta teklif varsa ana pencerede kart: kaynak (günlük/son sağlam kopya),
  düğüm sayısı, bozulma nedeni; **Geri yükle** (geri alınamaz uyarısı, onay) / **İncele** (teklifin
  özeti: düğüm başlıkları) / **Yoksay** (kurtarma dosyaları silinmez, teklif bu oturumda kapanır).
- **B2 İçe aktarma ilerleme + iptal** — async çekirdek UI iş parçacığı dışında; ilerleme şeridi
  (n / toplam, dosya adı) + İptal; iptalde yarım dosya kalmaz; UI yanıt verir (test: uzun dönüşümde
  dispatcher kilitlenmez).
- **B3 Çakışma kararı** — hedefte aynı adlı varlık varsa: içerik aynıysa atla; farklıysa kullanıcıya
  **Üzerine yaz / İkisini de tut (`ad (2).ext`) / Atla** (+ "hepsine uygula"); karar verilmeden dosya
  değişmez. Servis karar geri çağrısı alır; saf testler.
- **C1 Proje Merkezi kart durumları** — yolu eksik proje "Bulunamadı" rozeti, Aç devre dışı, "Listeden
  kaldır"; boş / yükleniyor / hata durumları; açma hatası başarı görünümü bırakmaz.
- **C2 İşlem sonucu dili** — Kaydet / Farklı kaydet / Dışa aktar için ortak durum: çalışıyor →
  başarılı (çıktı konumu + Klasörü aç) / hatalı (neden + Günlük'e git); başarısızlık asla başarı
  bildirimi göstermez.
- **D1 Klavye** — F6 / Shift+F6 panel odak döngüsü, Esc ile tuvale dönüş, görünür odak halkası (tema
  token'ı), kısayol listesi Ayarlar'da; temel yazar yolu (düğüm ekle → düzenle → kaydet) yalnız klavyeyle.
- **D2 Metin ölçeği + kontrast** — Ayarlar → Erişilebilirlik: arayüz metin ölçeği %100/%115/%130
  (yazı token'ları ölçeklenir, canlı), yüksek kontrast teması (kenar/soluk metin tonları), mevcut
  azaltılmış hareket; tercihler profilde kalıcı (test).
- **D3 Ölçek denetimi** — RowlEye %130 ölçekte kesilme/taşma denetimi; kilit testleri.

Çalışma düzeni: kodlama Spark (`muse-spark-1.3-contributor`), küçük iş + görsel denetim Wonder;
şef derler, tam editör süitini koşar, ekran görüntüsüne kendisi bakar. Dilim başına yerel commit.

**Faz 8 yapılan (2026-10-16):**
- A `1176ffb`: zemin #131315 + `GraphDotColor` (#303034). Konsol = alt şeridin Günlük sekmesi
  (yeniden adlandırma + prototip görünümü). Ayrı yaprak yapılmadı, çünkü kayıtlı düzen sekme
  indekslerini bozardı. Store gerçek panel, Araçlar 10 kart.
- C+D `36a5c6b`: Proje Merkezi kart durumları + içe aktarma doğrulaması; işlem sonucu kartı.
  Sessiz arka plan kayıt hatası artık görünür. F6 / Shift+F6, tema odak halkası, Kısayollar sekmesi
  tek kaynaktan. Arayüz ölçeği %100/%115/%130 ve yüksek kontrast. %115/%130'da Denetçi başlığı
  kesiliyordu; düzeltildi, kilit testi var.
- Kök neden düzeltmeleri: kayıt servisinin ev dizininden örnek proje içe aktarması (test makineye
  bağlıydı); yüksek kontrastın her viewmodel kurulumunda kaynak yazması (UI dışı iş parçacığında 50 test).
- Yerel: editör 929/929, RowlEye golden + denetim 3/3, taşınabilirlik + launcher yeşil.
- B `d19583e`: kurtarma kartı (incele / iki adımlı geri yükle / yoksay — dosyalar silinmez);
  içe aktarma UI dışı + ilerleme + iptal + atomik `.part` kopya; aynı adlı varlık kararı
  (atla / ikisini de tut / üzerine yaz, kalanlara uygula). Sürükle-bırak yolları ezmez, yeni ad verir.
  Kök neden: içe aktarma viewmodel'i sonucu UI dışında yazıyordu, son ilerleme kuyrukta kalabiliyordu.
- Faz 8 sonu yerel: editör 951/951 (iki kez), RowlEye golden + denetim 3/3, taşınabilirlik + launcher
  yeşil; native değişmedi.

## Faz 9 — Aşama B kapanışı (B02 tuval, B03 hata dili, B kabulü) (2026-10-17)

Karar (Chaple, 2026-10-17): Aşama B'yi kapat — B02/B03 açıkları + gerçek akış kabul turu.

**Ölçülen başlangıç (koddan, 958/958 yeşil):**
- Delete tuşu ve araç çubuğu yalnız tek `SelectedNode`'u siliyor; atomik `DeleteSelectedNodes`
  (`BatchDeleteNodesUndoAction`) hiçbir yere bağlı değil; Delete dalında metin kutusu koruması yok.
- Geri alma dışında kalan mutasyonlar: grup oluştur/sil/taşı/boyutlandır, Denetçi alan düzenlemeleri
  (başlık, metin, konuşmacı, seçenek metni/hedefi), başlangıç düğümü değişikliği.
- Zoom yalnız Ctrl+tekerlek; düz tekerlek bir şey yapmıyor. F yalnız tek düğümü ortalıyor (zoom yok,
  çoklu seçim yok); "hepsini sığdır" kısayolu yok; +/- pan'ı korumuyor. Tuvalde Tab ile düğüm gezme yok.
- Bağlantı reddi (kendine, yinelenen, hedef yok) sessiz; boş tuvale bırakılan kablo yalnız günlük satırı.
- Sorunlar paneli: `AssetPath` hiç okunmuyor (varlığa gidilmiyor); hata/uyarı simgesi yok.
- Varlık ızgara kartlarında eksik dosya rozeti yok; Çeviri Masası satır durumu çıplak enum metni.
- Denetçi silinmiş düğüme işaret eden seçenek hedefini göstermiyor (yalnız toplu lint).

**Dilimler**
- **E1 Tuval klavye + zoom** — Delete/araç çubuğu çoklu seçimi tek adımda siler (metin girişinde
  çalışmaz); düz tekerlek kaydırır (Shift yatay), Ctrl+tekerlek zoom; F seçimi (çoklu dahil) sığdırır,
  Shift+F hepsini; +/- görünüm merkezine sabit; Tab / Shift+Tab düğümler arasında seçim gezdirir;
  boş tuvale tıklamak tuvale odak verir.
- **E2 Grup + başlangıç düğümü geri alma** — oluştur/sil/taşı/boyutlandır ve başlangıç düğümü değişimi
  geri alınabilir; grup sürükleme tek adım.
- **E3 Denetçi düzenleme geri alma** — Denetçi içindeki alan düzenlemeleri (odak oturumu başına tek
  adım) geri alınır/yinelenir; tuval taşıma gibi Denetçi dışı değişimler karışmaz.
- **E4 Bağlantı dili** — reddedilen bağlantı nedenini kullanıcıya gösterir (kendine, yinelenen);
  boş tuvale bırakılan yeni kablo bırakılan yerde varsayılan düğüm oluşturur ve bağlar (tek geri alma
  adımı). Not: projede tuvalde açılan bir "Oluştur" menüsü yok; menü yerine doğrudan varsayılan düğüm.
- **F1 Hata dili (B03)** — Sorunlar satırı varlık sorununda varlığı seçer; hata/uyarı simgesi ve
  hedefsiz sorun ayrımı; ızgara kartında eksik rozeti; Çeviri Masası durum rozeti (yerelleştirilmiş,
  renkli); Denetçi'de silinmiş düğüm hedefi uyarısı.
- **K1 B kabulü** — gerçek `MainWindow` üzerinde betikli yolculuk: oluştur → düzenle → içe aktar →
  önizle → kaydet → farklı kaydet → dışa aktar; vazgeç, kurtarma, iptal. RowlEye görüntüsü + Wonder
  görsel denetimi; gerçek pencere kanıtının sınırı açıkça yazılır.

Çalışma düzeni: kodlama Spark, küçük iş + görsel denetim Wonder; şef uygular, derler, tam editör
süitini koşar. Dilim başına yerel commit; push gün sonunda tek sefer.

**Faz 9 yapılan (2026-10-17):**
- E1 `5a1405a`: çoklu silme tek adım (metin girişinde Delete düğüm silmez); düz tekerlek kaydırır, Ctrl+tekerlek
  zoom; F seçimi / Shift+F hepsini sığdırır (48 px pay, en fazla %100); +/- merkez sabit; Tab / Shift+Tab düğüm gezer.
  `ZoomToFit` kilit testi bilerek yeni kurala taşındı.
- E3 `57afabc`: Denetçi alan düzenlemeleri geri alınır (giriş denetimi başına tek adım; düğmeler kendi eylemiyle
  kalır; tuval konumu karışmaz). Panel yeniden yerleşince kayıtçı yeniden bağlanır. Pencere testi kayıtçısız kırmızı.
- E2 `423b813`: grup oluştur/sil/taşı/boyutlandır geri alınır (sürükleme bırakışta tek adım).
- E4 `75aa423`: bağlantı reddi bildirimi; boş tuvale bırakınca düğüm + bağlantı (tek adım).
- F1b `fd132b9`: Çeviri Masası durum rozeti (canlı); Denetçi silinmiş seçenek hedefi hatası, düğüm silinince /
  geri gelince yeniden doğrulanır (önceden yalnız toplu lint).
- F1a `cae9c4e`: varlık sorunu tıklanınca Varlıklar sekmesi + varlık seçimi; hata/uyarı noktası; hedefsiz satır
  düz; ızgara kartında eksik rozeti. `40eee6e`/`4302ee8`: Analiz düğmesi sağa (önceden yapışıktı), tıklanabilir
  satırda ok işareti (Wonder denetimi), RowlEye `ROWL_EYE_ISSUES=1` kancası.
- K1 `a7ce70c`: gerçek `MainWindow` (başsız) kabul yolculuğu: oluştur → Ctrl+N → Denetçi → Ctrl+Z/Y → Ctrl+S →
  yeniden aç; içe aktarma iptal → içe aktar → önizleme; farklı kaydet → paketle; vazgeç; kurtarma kartı.
  **Sınır:** klasör seçici diyalogları başsız sürülemez (o adımlar koordinatör yoluyla); gerçek ekranlı (X11/Wayland)
  pencere kanıtı yok — Aşama C02 kapsamı.
- Kök neden `2e21b5b`: başsız UI iş parçacığı kurulmadan `Dispatcher.UIThread`'e dokunan test dispatcher'ı
  `NullDispatcherImpl` ile bağlıyordu → iki iş parçacığı aynı pencerede düzen koşuyor, `EditorHeadlessSuiteTests`
  sıraya bağlı düşüyordu (~%75). Test derlemesinde `[ModuleInitializer]` ile çalışma zamanı önce kurulur.
- Bilinen, bu fazda alınmayan (Wonder): Denetçi doku kimliği metni sağda kesiliyor; tuvaldeki "Görünümü Sıfırla"
  yazısı soluk. Grup sürüklemesinde yakalama kaybı (bırakma olayı gelmezse) kayıt üretmez.
- Faz sonu yerel: editör 995/995, RowlEye golden + denetim 3/3, RowlEye testleri 56/56, taşınabilirlik + launcher
  yeşil; native değişmedi.

## Faz 10 — Donma, yan yana merkez görünümler, Denetçi yeniden tasarımı (2026-10-17)

Karar (Chaple): (1) prototipteki gibi Node / Oyun / Sahne Düzenle aynı anda ayrı paneller; (2) panel
taşırken donma giderilsin; (3) Denetçi sıfırdan, "uçak kokpiti değil, sanat eseri". Denetçi taslağı
`prototypes/editor-workspace/inspector-redesign.html` onaylandı (kavram: `docs/design/inspector-redesign-concept.md`).

**Yapılan:**
- `aae20ca` donma kök nedeni: yerleşim animasyonu paneli ara boyutlarda yeniden ölçüyordu (Denetçi
  40–120 ms/kare); panel son yerinde bir kez yerleşir, geçiş dönüşümle çizilir. Kilit testi.
- `30a338d` merkez görünümler bağımsız aç-kapa, yan yana (`ToggleCenterView`, `EnsureCenterVisible`).

**Denetçi dilimleri:**
- **D1** `InspectorSceneViewModel`: mevcut bileşenlerin üstünde yazar dilinde uyarlayıcı (konuşan, söz,
  yazı hızı Yavaş/Normal/Hızlı/Anında, karakter konumu Sol/Orta/Sağ → X, arka plan Doldur/Sığdır,
  seçim satırları hedef sahne adıyla, ses, geçiş kartları + süre, kamera Sabit/Yaklaş/Uzaklaş).
  Veri modeli değişmez.
- **D2** yeni görünüm: hikâye akışıyla bölümler, "Ayrıntılar" katlanır; eski bileşen düzenleyicisi en altta
  "Tüm sahne nesneleri (gelişmiş)" altında kalır — özellik kaybı yok.
- **D3** seçim yok / çoklu seçim durumları (ortak: bölüm, etiket; çoğalt, sil).
- Modelde karşılığı olmadığı için taslaktan alınmayanlar: "Işık: Gündüz/Akşam/Gece", "Seslendirme ekle"
  (ses kaydı alanı yok); çoklu seçimde geçiş/müzik toplu atama sonraki dilim.

**Faz 10 Denetçi yapılan (2026-10-17):**
- D1 `59a6ee7` `InspectorSceneViewModel` (yalnız seçili düğümü ve seçenek hedeflerini dinler; ilk sürüm her düğüme
  abone olup 2000 düğümlük yükleme bütçesini aşıyordu). Test kurulumunun `ProjectRoot`'u erken geri yüklemesi,
  Dispose'daki kaydı deponun örnek projesine yazdırıyordu — düzeltildi.
- D2/D3 `7bc08f8` `SceneInspectorView`: hikâye akışıyla bölümler, parçalı seçimler, boş durumlar, seçim yok /
  çoklu seçim; eski düzenleyici "Gelişmiş ayarlar" altında katlı; başlıkta nesne kimliği gizli.
- `ee4560c` Wonder cilası: "Ayrıntılar" ok şablonu, ölü boşluk, bölüm adı alt satırı, "+ Ekle".
- Yerel kapılar: editör 1011/1011, RowlEye golden + denetim 3/3, taşınabilirlik + launcher yeşil; native değişmedi.
- Taslaktan alınmayan (modelde yok): Işık, Seslendirme ekle, "Ana karakter · sahnede"; çoklu seçimde bölüm/etiket/
  geçiş/müzik toplu alanları (VM'de `MultiChapter`/`MultiColorTag` hazır, görünüm sonraki dilim).
