# Rowl Engine — bulgu doğrulaması ve uygulama yol haritası

Tarih: 6 Ekim 2026. İncelenen kaynak: `2a1648a9ee7dbd56e4505464ff4b3a7076b434d8`.
Durum: denetim ve uygulama önerisi; bu çalışmada üretim kodu değiştirilmedi.

**Sonraki kullanıcı talimatı:** İlk uygulama paketi A02/A03 ile başlanır.
UI tasarım aktarımı aşamasına gelindiğinde çalışma durur; kullanıcı tasarım
çalışmalarını tamamlayıp açık onay vermeden aktarım başlamaz. Aşama A'nın
tamamlanması tek başına UI aktarım yetkisi değildir. Bu metnin aşağıdaki
denetim sonuçları uygulama öncesi kaynak sürümüne aittir; uygulama sonuçları
ayrı teslim kaydında tutulur.

## Sonuç

4 Ekim raporunun temel teknik teşhisi geçerli. Güncel sürümde kayıt kararları, Lua çağrısının sonlandırılması ve export edilen oyuncunun metin/input paritesi açık. UI web prototipi hazır bir tasarım referansı; motorla bağlanmış editör değildir. Entegrasyonun ana yeri **Aşama B: güvenilirlik kapısından sonra, genel stabilizasyondan önce**. Tasarım envanteri ve komut eşlemesi şimdi hazırlanabilir; üretim arayüzünün değişimi kapıya bağlı olmalı.

Raporun 67/100 puanı bir mühendislik değerlendirmesidir; testle doğrulanmış tamamlanma oranı değildir. Yeni bir puan üretmedim. Yeşil süitler yeni karşı örneklerle aynı anda var olabilir.

## İncelemenin kapsamı ve kanıtı

- Masaüstündeki birleşik raporun motor incelemesi ve yol haritası okundu; depo karşılıkları ve İkinci Beyin'deki 30 Eylül master planı, 4 Ekim prototip notu ve 5 Ekim authoring notuyla karşılaştırıldı.
- `git diff 37a5a49 HEAD -- engine editor tests tools .github` boş. Motor, gerçek editör, test, araç ve CI kaynakları önceki incelemeden beri aynı; yeni commit'ler prototip/belge değişiklikleri. Bu, eski test çıktılarının bugünkü koşulda çalıştığı anlamına gelmez.
- 32 etiketli bulgu K1–K3, O1–O9, E1–E7, R1–R7, T1–T6 için ilgili çağrı/kod/sözleşme yolu kontrol edildi. Tüm repo ve tüm kasa satır satır okunmadı; 32 bulgu 32 bağımsız kök neden veya 32 yeniden üretilmiş hata değildir.
- Temiz Linux Release build, 218 adım tamamlandı. Bu worktree'nin yeni kütüphanesiyle Lua, condition ve tanı probe'ları; yeni nesneleriyle state probe'u çalıştırıldı. Başka checkout'un eski binary'si kullanılmadı.
- Gerçek editör kaynaklarıyla discard ve SaveAs probe'ları yeniden çalıştı. Verifier karşı örneği geçici paket fixture'ında tekrarlandı.
- Prototip `node --test prototypes/editor-workspace/*.test.mjs`: **27/27 geçti**. Kayıtlı masaüstü ekran görüntüsü incelendi; bu tur canlı tarayıcı veya Avalonia görsel kabulü yapılmadı.
- Gerçek GPU ekranı, ses çıkışı, Windows/macOS/Steam Deck ve mobil cihaz kabulü yapılmadı. Uzaktan CI canlı sorgulanmadı; 4 Ekim koşu kimlikleri tarihsel kanıttır.

Genel yerel kapıların nihai sonucu yanındaki `verification-evidence/2026-10-06/README.md` dosyasındadır. İlk managed koşu native build bitmeden başladı: 509/510, eksik player/runtime nedeniyle export testi düştü. Native build tamamlandıktan sonraki koşu **510/510 geçti**. İlk sonuç kapatılmış çevresel/build sırası eksikliğidir; yeni bir ürün regresyonu diye sayılmaz.

## Kritik ve yeniden üretilen bulgular

| ID | Güncel sonuç | Yeniden çalıştırılan kanıt / sınır |
| --- | --- | --- |
| K1 | Doğru, açık | `while true do pcall(function() while true do end end) end` 9 saniyede dönmedi; ayrı child öldürülüp toplandı. Poison sonraki çağrıyı reddetse de mevcut çağrıyı sonlandırmıyor. |
| K2 | Doğru, açık | Discard'dan sonra VM dirty=false; recovery marker=true; Dispose sonrası disk değişti ve vazgeçilen metin dosyada kaldı. Headless gerçek VM; pencere tıklaması yapılmadı. |
| K3 | Doğru, açık | Başarısız save callback'ine rağmen SaveAs success=true ve `old-disk-document` kopyalandı. Build'in aynı `Action` sözleşmesi kaynakla doğrulandı; build için disk dolu deneyi yapılmadı. |
| E2 | Doğru, açık | SaveAs probe'unda `SOURCEASSETS_COPIED=False`; converted dosya bulunması özgün medya ve yeniden dönüşüm kaynağını korumuyor. |
| R1 | Doğru, açık | 500×2000 karakter + dört önceki state: 5.361.414 bayt; `save=0`, `decode=0`. İyi eski slotun bozulduğu gösterilmedi; sorun runtime/save bütçe uyumsuzluğu. |
| R3 | Doğru, kayıtlı sınır | Condition sonrası `x==20` ve `math.abs==nil` true. Eski `_G` metatable kaçışı yeniden açılmış değildir; mevcut değerlerin mutasyonu ayrı saflık açığıdır. |
| R4 | Doğru, açık | badFade return=2/last=2; goodFade return=0/last=2. Doğrudan dönüş doğru; last-result önceki hatayı taşıyor. |
| T1 | Doğru, açık | Kontrol fixture'ı kabul edildi; player sıfır bayt + runtime isimli klasör de exit=0 ile kabul edildi. Fixture başlangıç binary'leri de gerçek executable değildir; test executable doğrulama eksikliğini gösterir, launch kanıtı vermez. |

## Oyuncu / render bulgularının kontrolü

| ID | Hüküm | Güncel kaynak dayanağı ve sınır |
| --- | --- | --- |
| O1 | Ürün seviyesinde eksik | `engine/src/player/main.cpp`: title-state CLI iskeleti; normal yol doğrudan `RowlEngine_Run`. Native title/new/continue ekranı bağlanmamış. |
| O2 | Kısmi | `parity_presenter.hpp`, `engine_pause_menu.cpp`: history/helper ve slot listesi var; native backlog ekranı ve zengin slot sunumu eksik. |
| O3 | Kısmi | `engine_parity_presenter.cpp`, pause menu: native global auto/read-aware skip sürücüsü ve kalıcı profil yolu tamamlanmamış. PlayerWindow yeteneği RowlGame kanıtı değildir. |
| O4 | Kod zinciri doğru | `engine.cpp` input ve `story_runtime.cpp` advance: oklar pause'a bağlı, ordinary advance ilk rota; choice dışı pointer advance'a düşebilir. GUI/condition bypass yeniden üretimi yok. |
| O5 | Temsil kaybı doğrulandı | `window_msdf_shaped.cpp`: `glyph_index` yerine logical scalar/codepoint atlas sorgusu ve tek renk. Fast-path de legacy raw-text çizimine dönüyor. Gerçek piksel farkı ölçülmedi. |
| O6 | Kod zinciri doğru | `window_text_fallback.hpp` ve `window.cpp`: kaliteli CPU raster offscreen koşullu; görünür MSDF başarısızlığında SDL debug text yolu var. Gerçek shaderless ekran kabulü bekliyor. |
| O7 | Kısmi | `engine_pause_menu.cpp` sabit Türkçe/ASCII menü dizeleri; story catalog oyuncu kabuğunun çevirisi değildir. |
| O8 | Kısmi, canlı font kanıtı | `window_font_resolve.cpp` ilk yüklenen fontu seçiyor. Bundled `default.ttf` ile Arapça `لا` glyph `[0,0]`; birden çok fonttan karakter bazlı fallback yok. |
| O9 | Kısmi | `audio_engine.cpp:748` gerçek uzun OGG BGM akışı; WAV aynı RAM yoluna düşüyor. Voice/ambience için SDL audio stream nesnesi bulunması bounded dosya decoding kanıtı değildir. |

**O5/O8 için gerekli açıklık:** 4 Ekim'in `ffi → 5044`, `لا → 5365` örnekleri fonta bağlı. Bu tur DejaVu Sans ile aynı değerler tekrarlandı. Bundled fontla `ffi → [73,73,76]`, Arapça `[0,0]` çıktı. Dolayısıyla “paket fontu fi ligatürü üretiyor” sonucu çıkarılamaz. Ana bulgu, üretildiğinde shaped glyph kimliğinin MSDF hattında korunmaması; ayrıca paket fontunun Arapça kapsamının eksikliğidir. Sistem DejaVu fontu paket kapsamı kanıtı sayılmaz.

## Editör, runtime ve yayın bulgularının kontrolü

| ID | Hüküm | Dayanak / yapılmayan doğrulama |
| --- | --- | --- |
| E1 | Kod zinciri doğru | `EditorBuildCoordinator` yalnız `ProjectValidationService.Validate` çağırıyor; `ProjectLintService` ek choice/condition/script/translation/structure kurallarının tümü export kapısında aynı snapshot'tan tüketilmiyor. |
| E3 | Kod zinciri doğru | `ImportAssetAsync` picker sonrası senkron `ImportAssetFiles` çağırıyor. Async isim responsive conversion garantisi değil; bu tur GUI gecikmesi ölçülmedi. |
| E4 | Ürün yüzeyi eksik | `RestoreFromRecovery` ve offer var; `editor/Views` içinde bunlara binding bulunmadı. Depo prototipi bu native açığı kapatmıyor. |
| E5 | Kod zinciri doğru | `EditorAssetImportService` birden çok kolda `File.Copy(... overwrite:true)` kullanıyor; collision kararı kullanıcıya bağlı değil. |
| E6 | Statik risk geçerli | `ConfigureAwait(false)` sonrası canlı node/connection okumaları ve `reportIssues`; GUI race yeniden üretilmedi. Immutable input/UI dispatch işi olarak kalmalı. |
| E7 | Kod zinciri doğru | `CrashRecoveryService`: toplam byte temizliği `journals.Count > 1` koşullu; tek journal 10 MiB sınırının dışında büyüyebilir. Yeni uzun journal deneyi yok. |
| R2 | Tasarım sınırı doğru | `GameState` previousState zinciri sürüyor; history vektör kopyaları ve shrink_to_fit toplam RAM tavanı sağlamıyor. Yeni RSS soak yok. |
| R5 | Kayıtlı sınır / kapsam kararı | `save_durability.cpp` açıkça fsync/FlushFileBuffers kapsam dışı. Atomik replace gerçektir; güç kaybı garantisi veya veri bozulması deneyi yapılmadı. |
| R6 | Kod zinciri doğru | `rowlpkg_reader.cpp` raw readEntry → string → istringstream; zstd bounded stream ayrı. Yeni peak allocation ölçümü yok. |
| R7 | Statik, düşük olasılık | `c_api_lifecycle.cpp`: token düşük 32-bit generation, kayıt/sayaç 64-bit. 2^32 sonrası uyuşmazlık; milyarlarca handle testi yapılmadı. |
| T2 | Doğru, politika eksik | CI editor benchmark üretip upload ediyor; `compare_editor_benchmarks.py` fail-percent kapısı yok. Native benchmark karşılaştırma kapısının varlığı editör için aynı garantiyi sağlamaz. |
| T3 | Doğru, test kapsamı sınırlı | `EditorEndToEndFlowTests` connectEngine=false ve dört byte PNG/WAV. Test yararlı, gerçek medya/GUI yolculuğu kanıtı değil. Discard/SaveAs probe'ları halen yeşil süitin dışında kusur buluyor. |
| T4 | Doğru, kanıt okuma sınırı | Workflow seçili test/hedefler, nightly koşulu ve detect_leaks=0 içeriyor. Bu tur sanitizer/remote CI çalıştırılmadı. |
| T5 | Doğru, belgeler eski | IMPLEMENTATION_STATUS başlığı 12 Eylül/11 CTest; güncel envanter 71. PRODUCTIZATION_BASELINE'ın rich text, graph araçları/layers/audio envanteri kaynakla hizalı değil. |
| T6 | Doğru, yayın boşluğu | CI SDL tarball sürümü pinli, ayrıca beklenen SHA256 doğrulaması yok. Platform compile/native host, signed installer/APK/IPA ve temiz hedef kabulüyle eşdeğer değil. |

Bu incelemede raporun temel kusur iddialarını çürüten bir düzeltme bulunmadı. Statik riskler, ürün eksikleri ve kayıtlı sınırlar dinamik arıza diye yükseltilmedi. Gallery/music room/achievement için tüm repo negatif-varlık ispatı yapılmadı; önce capability envanteriyle kapsam doğrulanmalı.

## UI web sitesinin entegrasyon kararı

Kaynak: `prototypes/editor-workspace/`, son commit `2a1648a`. Siyah/kemik palet, tek sade üst kapsül, çakışmayan dock bölmeleri, görünür ×, taşıma kilidi olarak pin, Node/Game/Edit Scene ayrımı ve Inspector merkezli düzenleme korunacak tasarım referanslarıdır. Son kullanıcı kararları gereksiz ek işlem barlarının kaldırılmasını, bileşenlerin yeni node türleri gibi gösterilmemesini ve Varlıklar ile Assets Store'un ayrılmasını içeriyor.

**Önerilen teknik yön:** HTML/CSS/JS tasarımını Avalonia AXAML/styles/controls ve mevcut MVVM servislerine taşı. Çalışma alanının tamamını WebView'e gömmek yeni JS↔C# persistence/input/render köprüsü açar; mevcut native/managed sınırları korunarak bu ek maliyet üstlenilmemeli. Bu bir uygulama önerisidir; yeni ürün mimarisi uygulanmış değildir.

| Prototip alanı | Native entegrasyon | Ek iş / kabul |
| --- | --- | --- |
| Kapsül, araç listesi, dock, pin/×, tema | B1: view/komut yerleşimi, layout model ve sürümlü kullanıcı tercihi | Başlık taşıma, splitter, odak, klavye, son panel kapatma; eski projeler etkilenmez. 27 JS testi davranış referansı, Avalonia kabulü ayrı. |
| Node, bağlantılar, minimap, arama | B2: mevcut canvas/culling/search kontrollerini yeni kabuğa yerleştir | Prototip sıralı örnek graph; gerçek dallanma/kablolama/condition/undo mevcut native graph üzerinden kalır. |
| Inspector/Hiyerarşi/Edit Scene | B3: ortak selection ve gerçek component komutları | Scene edit undo+dirty/save zincirine bağlanır; yüzde/prototip koordinatları gerçek model koordinatlarına açık dönüşür. |
| Game başlat/duraklat/kapat | B3: EngineHost/preview lifecycle | Runtime snapshot ile authoring ayrı; düzenleme duraklatılmış oyunu değiştirmez, kaynak ve host ömrü RAII/dispose ile kapanır. |
| Lua paneli | B4: gerçek ScriptComponent/file/provenance ve tanılar | Örnek `on_start`/obje fonksiyonları API değil; desteklenen on_enter/on_update/on_choice/on_exit sözleşmesine eşle. Çalıştırma A06'ya bağlı. |
| Ayarlar, Kaydet, Hub'a dön, Export | B4: mevcut Settings/Save/Hub/Build servisleri | Proje, editör ve oyuncu tercihlerinin sahipliği ayrılır. localStorage/JSON taslağı native save/rowlpkg değildir. Desteksiz ayarlar etkin ürün özelliği gibi gösterilmez. |
| Varlıklar, Konsol, Recovery, Localization/Analiz | B4: gerçek import/progress/cancel ve diagnostic panel | Recovery restore/incele/vazgeç; rename/replace/cancel; mevcut çeviri/analiz araçları sade kabukta erişilebilir kalır. |
| Klasörlü düğüm şablonları, Inspector yıldızı | B5: ayrı yeni ürün işi | Sürümlü kalıcı şablon modeli, yeni ID, dış hedef temizliği, derin kopya, undo, asset referansı/proje izolasyonu. Görsel aktarım diye gizlenmez. |
| Assets Store | Ertele | Yakında gelecek placeholder; backend/account/store kapsamı bu entegrasyonla açılmaz. |
| Mobil görünüm | Masaüstünde responsive tasarım referansı | Aynı kontrol dili korunur; Android/iOS paket, IME/touch/lifecycle kabulü E aşamasına ait. |

Tasarım aktarımı sırasında import/recovery gibi açık hata yüzeyleri görünür hale getirilmeli. Yeni timeline, yeni genel shader sistemi, mağaza veya mobil yayın B aşamasının kendiliğinden kapsamı değildir.

## Uygulayacağım sıra ve tamamlanma kapıları

Eski A01–A14 ID'leri korunur; yinelenen yeni bir backlog açılmaz. Rapordaki paralel ajan önerisi bu görevde ajan başlatma talimatı sayılmadı; çalışma tek ajanla yürütüldü. Aşağıdaki sıra sonraki uygulama görevleri için öneridir.

| Dalga | İşler ve yaklaşım | Çıkış kapısı |
| --- | --- | --- |
| 0 — kanıt ve sözleşme | A01/A14: güncel capability matrisi, karşı örnekler, commit/fixture/makine kimlikleri; UI alanı→VM/servis haritası | Durum raporu ve uygulama kuyruğu aynı kaynak sürümünü gösterir; eski 11 CTest/67 puan tamamlanma kanıtı olmaz. |
| 1 — yazar verisi | A02 → A03 → A04 → A05: açık save/discard/cancel; typed save sonucu; tam staged SaveAs; tek detached build/lint snapshot ve UI dispatch | Discard disk baytlarını değiştirmez; save hatasında close/SaveAs/export durur; SourceAssets/provenance korunur; build sürerken edit tutarlı tek sürümden export edilir. |
| 2 — yürütme ve paket | A06 → A07; A13 artifact alt işi: abort/rollback tasarımı, condition saflığı, gerçek binary/library doğrulama | Sonsuz catch bütçede gerçekten sonlanır; sonra geçerli iş çalışır; x/math.abs korunur; boş/klasör/yanlış mimari artifact reddedilir. |
| 3 — state ve player temeli | A08/A09 ortak state tasarımı; A10/A11 choice ve görünür metin; A12 tanı | Büyük history save/load/migration; ölçülmüş RAM ve köke rewind; klavyeyle ikinci choice, misclick güvenliği; gerçek shaderless Unicode ve CPU/GPU paritesi. |
| B — UI entegrasyonu | B1 tema/kabuk → B2 canvas → B3 selection/Inspector/scene/preview → B4 proje araçları/Lua/import/recovery → B5 yeni şablon kütüphanesi | Gerçek Avalonia create/open/edit/import/preview/save/SaveAs/export; discard/recovery/cancel; odak/kısayol/undo; kayıtlı tasarımla ekran karşılaştırması. |
| C — stabilizasyon | Decodable medya Golden Project; 2000 node/6000 edge; uzun oturum; disk/izin/iptal/crash; Linux ve Windows gerçek hedef | Aynı artifact ile GUI/input/audio/Unicode-save/locale/rewind kanıtı; yeni UI regresyonu yok; ölçüm bütçeleri fixture/makine bağlı. |
| D — VN ürün bağlantısı | D01 oyun kimliği/profile → D02 title/continue/slot → D03 backlog/replay → D04 auto/read-aware skip → D05 locale/font/a11y; D06 mevcut layer/audio/chapter yüzeyleri | Paket aç → yeni oyun → ikinci choice → save → çık/restart → continue → backlog → tercih → auto/skip → rewind, editör/CLI yardımı olmadan. |
| Masaüstü 1.0 | Aynı RC artifact, ilan edilen platformlarda kabul; installer/update/rollback, lisans/kılavuz ve bağımsız yazar denemesi | Kritik açık yok; platform/cihaz kanıtları artifact hash'iyle bağlı; desteklenmeyen hedefler açıkça ayrı. |
| E — mobil | PlatformHost/lifecycle ardından Android APK/AAB ve iOS signed app; oyuncu/editör UX ayrı | Fiziksel cihazda install/launch/touch/audio/save/restart/background kabulü. Masaüstü veya responsive web başarısı mobil kabulü olmaz. |

**UI'ye giriş kapısı:** K1/K2/K3 üretim yollarında kapanmış; A05 snapshot/UI-thread ve A08/A09 veri bütçesi sözleşmeleri oturmuş; A10/A11 temel input/metin karşı örnekleri kapanmış; artifact doğrulaması güvenilir; aynı kaynak sürümünün ilgili native/managed/package testleri geçer. Bu aşamaya kadar tasarım tokenları/komut haritası hazırlanabilir, üretim kabuğunun bütünü değiştirilmez.

R7 generation rollover ve R6 bounded raw stream, ölçülen etki ve kabul kararıyla sonraki optimizasyon dilimine taşınabilir; K1/K2/K3 ertelenmez. A06 için salt host timeout/poison/tekrar Lua error çözüm sayılmaz; coroutine tasarımında yield edemeyen C çağrıları ve rollback sınanır, güvence sağlanamazsa süreç izolasyonu değerlendirilir. Thread zorla öldürülmez. A08/A09 yalnız 4 MiB'yi büyüterek veya root history'yi sessiz budayarak kapanmaz.

İlk uygulanacak paket **A02 + A03**: yazarın kaydet/vazgeç kararını düzeltmek, sonra SourceAssets/staging ve build snapshot. Lua A06 bir sonraki bağımsız kritik paket; büyük UI değişimi öncesinde bitmeli. Her paket önce güncel kırmızı davranış örneği, ardından küçük diff, sonra ilgili ABI/native/managed/package kapısı ve görev kapsamlı kayıt ile teslim edilir. Zorunlu gerçek GUI kabulü ayrıca kaydedilir.

## Kapsam, maliyet ve karar noktaları

- A02/A03 ve verifier alt işi sınırlı düzeltmeler; A04/A05 orta boy workflow işleri; A06/A08/A09 yüksek belirsizlikli mimari işler. Dock/state ve gerçek scene edit B'nin en büyük entegrasyon riskleridir. Takvim tahmini bu mimari dilimlerin ilk teslim hızından sonra yapılmalı; 32 bulgu sayısından hafta hesabı çıkarılamaz.
- 1.0 platform listesi için öneri: önce Linux/Windows aynı artifact kabulü; macOS/Steam Deck ayrı platform kapısı. Master vizyonu daraltılmış/onaylanmış sayılmaz.
- D07 gallery/music room/achievement için öneri: temel VN yolculuğundan sonra, 1.x kapsamı; master plan/baseline çelişkisi kapsam dondurmasında çözülür.
- Power-loss durability iddiası için file+directory sync/Windows flush ve maliyet kabulü gerekir. Mevcut process-crash atomicity ayrı açık sözleşme olarak korunabilir; bu rapor bu ürün kararını verilmiş saymaz.
- Rewind backing store kullanılırsa disk bütçesi, corruption/IO error, temizlik ve köke dönüş birlikte tasarlanır. Hash kontrolü imzalı yayın/authenticity iddiası değildir.

## Kaynaklar ve devam kaydı

- [4 Ekim incelemesi](REVIEW_2026-10-04.md), [orijinal yol haritası](DEVELOPMENT_ROADMAP_2026-10-04.md), [ilk karşı örnekler](review-evidence/2026-10-04/README.md).
- [Bu turun test/karşı örnek kaydı](verification-evidence/2026-10-06/README.md).
- [UI prototipi ve son davranış kararları](../prototypes/editor-workspace/README.md).
- İkinci Beyin: `Rowl-Engine-Master-Yol-Haritasi-2026-09-30`, `Rowl-Engine-Arayuz-Prototipi-2026-10-04`, `Rowl-Engine-Prototype-Authoring-2026-10-05`; yeni doğrulama notu `Rowl-Engine-Dogrulanmis-Yol-Haritasi-2026-10-06`.

Uygulama henüz başlamadı. Tarihsel raporlar değiştirilmedi; sonraki uygulama bu doğrulanmış sıra ve mevcut ID'lerle yürütülmeli.
