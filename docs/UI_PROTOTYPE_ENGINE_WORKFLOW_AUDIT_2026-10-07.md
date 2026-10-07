# Rowl prototipi: oyun motoru editörü iş akışı incelemesi

Tarih: 7 Ekim 2026. Kapsam: `prototypes/editor-workspace`; native editör yalnız önceki aktarım araştırmasının mimari bağlamıdır. Bu turda Avalonia UI değişmedi ve üretim UI aktarımı başlamadı.

## Sonuç

Prototipin görünümü ve pencere sistemi güçlü bir temel oluşturuyor; ancak bir hikâyeyi baştan sona güvenle üretmeye ilişkin bazı etkileşimler henüz tasarlanmamış. Önceki **92/100 görsel değerlendirme**, oyun motoru iş akışlarının yüzde 92 tamamlandığı anlamına gelmez. Bu inceleme daha geniş kapsamlıdır; yeni bir toplam puan için henüz bütün görev senaryoları doğrulanmadı.

Öncelik daha fazla süsleme değil: geri alınabilir düzenleme, anlaşılır kayıt durumu, gerçek dallanma temsili, büyüyebilen obje hiyerarşisi ve varlık seçiminin doğru bağlamı. Bunların arayüzünü örnek veriyle tasarlamak mümkündür; Lua çalıştırma veya gerçek paket üretme zorunluluğu yoktur.

Native görünümü gerekirse yeniden kurabiliriz. Bunun şartı mevcut işlevlerin envanterini ve hizmet bağlantılarını korumaktır. Eski XAML görünümünü değiştirmek ile kayıt, komut ve motor köprüsü hizmetlerini kaldırmak ayrı kararlardır. Önce prototip güçlendirilecek; native aktarım için kullanıcı onayı sınırı devam ediyor.

## İnceleme yöntemi ve kanıt sınırı

- Sahne, Inspector, bileşen düzenleyici, graph, varlıklar, kayıt/geri yükleme ve çalışma alanı kaynakları incelendi. Bu bir tüm motor kaynak kodu denetimi değildir.
- Canlı tarayıcı kontrolleri: 844×390, 320×740, 1366 piksel genişlik ve 1440 piksel genişlik. Bunlar masaüstü tarayıcı viewport benzetimleridir; fiziksel telefon/dokunma kabulü değildir.
- `review.html` örnekleri normal proje kayıtlarını okumadan/yazmadan kullanıldı. Gerçek kullanıcı kaydında bozulma veya kota hatası oluşturulmadı.
- 43 Node durum testi geçti; JS/MJS sözdizimi ve diff kontrolleri geçti. Bunlar Avalonia GUI, GPU, ses veya native oyun çalıştırma kanıtı değildir.
- Aşağıdaki “kaynak” bulguları kod akışına dayanır; canlı olarak yeniden üretilmeyen riskler ayrıca belirtilmiştir.

[Kanıt klasörü](verification-evidence/2026-10-07-engine-ui-audit/README.md). [Önceki aktarım araştırması](UI_PROTOTYPE_REFINEMENT_AND_AVALONIA_TRANSFER_2026-10-07.md).

## Bu turda uygulanan düzeltmeler

| Bulgu | Değişiklik | Doğrulama |
| --- | --- | --- |
| UI01: Kütüphane açıklamaları ortak 12 px ölçeğine rağmen eski özel CSS yüzünden 10 px kalıyordu | Daha özel seçiciler ortak metin değişkenine bağlandı | Tarayıcı hesaplanmış değer: 12 px |
| UI02: Kısa yatay ekranda sahne paneli kontrolleri ekranın altına taşıyordu | Varsayılan sahne yüksekliği kullanılabilir çalışma alanına göre sınırlandı | 844×390'da panel yaklaşık 323 px; alt kontroller ekran içinde, dokunma yüksekliği 44 px |
| UI03: Oku dar panelde sahneyi okunamayacak kadar sıkıştırıyordu | Geniş panelde yan yana, uzun dar panelde alt alta, küçük panelde geçici okuyucu görünümü | 1440'da yan yana; 320 ve 844×390'da overlay; Kapat sahneyi geri getiriyor |
| UI04: On bileşen tek düz listede yalnız “Düzenle” yazısıyla görünüyordu | Kapatılabilir Bileşenler grubu ve türe uygun özellik özetleri | Grup kapalıyken obje özelliği değişince kapalı kaldı; diyalog konuşmacı/yazı boyutu özeti görüntülendi |
| UI05: Bileşen uygulaması satırı yeniden oluşturunca modal sonrası odak kayboluyordu | Odak yeniden oluşturulan bileşen düğmesine döndürülüyor | Yazı boyutu 18→20 uygulandı; aktif odak güncel Diyalog satırında |

Okuma alanı Game'in o anki oynatım metnini izlemeye devam eder. Başlat/Duraklat kontrolünde sahne ve okuyucudaki metin aynı noktada dondu. Bu turdaki tarayıcı hata/uyarı kaydı boştu.

## Eksikler, hatalar ve öncelikler

P0: örnek kullanıcı verisini koruma; P1: temel üretim akışı; P2: akışın olgunlaşması. “Eksik” ifadeleri görsel/etkileşim prototipine ilişkindir; gerçek motor arka ucunun yokluğuna puan cezası değildir.

| ID / öncelik | Bulgular ve dayanak | Prototipte önerilen uygulama / kabul koşulu |
| --- | --- | --- |
| UI06 / P1 | Genel editor undo/redo yok. Canlı kontrol: Mira X=25→26, obje odaktayken Ctrl+Z sonrası 26 kaldı. Metin alanının tarayıcı undo'su proje undo'sunu karşılamaz | Obje hareketi, Inspector değişimi, bileşen ve düğüm işlemleri için işlem geçmişi. Bir sürükleme tek işlem; Escape iptal; undo/redo tüm bağlı panelleri günceller |
| UI07 / P1 | Global proje “değişti/kaydedildi/kayıt başarısız” durumu yok; Lua ve ayar taslak durumları ayrı. Kayıt yalnız bildirim üretiyor | Üst kapsülü kalabalıklaştırmadan proje adı/durum işareti; Kaydet, Vazgeç, İptal akışları; hata sonrası değişiklik işareti kalır |
| UI08 / P1 | `app.js::rebuildGraph` ardışık bağlantılar çiziyor. Seçim/koşul hedef alanları gerçek port/bağlantı düzenlemesiyle temsil edilmiyor | Etiketli çıkışlar, bağlantı ekle/sil, hedef seçimi, geçersiz hedef uyarısı, seçileni çerçevele ve arama. Döngüler hikâye kurallarına göre değerlendirilir. Bileşenleri yeni düğüm türleri gibi sunmayız |
| UI09 / P1 | `baseObjects` üç sabit obje: Ay/Mira/Diyalog. Çok objeli/nested sahnenin üretim akışı görünmüyor | Örnek veride farklı sayıda obje; yeniden adlandırma, çoğaltma, silme, kilit/görünürlük, sıralama; çoklu seçim ve parent ilişkilerini ürün kapsamıyla netleştir |
| UI10 / P1 | Yoğun varlık örneğinde dosyaların tamamı Ay objesini seçiyor; ipucu bunu açıklasa da kartlar bağımsız dosya gibi görünüyor. Normal araç da varlık seçimini sahne objesine yönlendiriyor | Varlık seçimi ayrı Inspector bağlamı: yol/tür/boyut/önizleme; klasör/breadcrumb/tür filtreleri, eksik dosya ve import durumu. Örnek varlığı sahneye atama eylemi açık olsun |
| UI11 / P0 | Kaynak riski: geri yüklemede 50.000 karakter ve üzeri Lua reddediliyor, editörde aynı üst sınır yok. Başlangıç restore hatası sessiz `catch {}` ile varsayılan projeye düşüyor. Sonraki kaydetme eski kaydı ezebilir | Bozuk/uyumsuz kaydı koru; açma hatası ve kurtarma/JSON indirme seçeneği göster; açık seçim olmadan varsayılanı aynı kayda yazma. Sınır yazarken görünür olsun. Gerçek kullanıcı kaydıyla yeniden üretilmedi |
| UI12 / P0 | Kaynak: `authoring-ui.js::hub` save sonucunu kontrol etmeden Hub'ı açıyor. save hata yakalayıp yalnız bildirim veriyor. Ayar kaydı da hatayı yutabiliyor | Hata enjekte edilen örnekte ekranda kal, tekrar dene/indir/iptal seçenekleri; “kaydedildi” durumu yalnız başarıdan sonra. Gerçek depolama kotası doldurulmadı |
| UI13 / P2 | Lua dosya kapatma/yeniden adlandırma ve kirli dosya kapatma kararı tasarımı eksik; tanılama gezinmesi sınırlı | Önce kapat ve Kaydet/Vazgeç/İptal, dosya yolu ve satıra git. Gerçek Lua execution/autocomplete bu dilimin şartı değil |
| UI14 / P1 | Konsol düz metin ve zaman damgası ağırlıklı; önem seviyesi/filtre/soruna git akışı yok | Hata/uyarı/bilgi ayrımı, arama, tekrar sayısı, ilgili düğüm/obje/script satırına git; mevcut native sorun paneli aktarımda korunmalı |
| UI15 / P2 | Oynatma kopyası ve duraklatma mevcut. Aktif düğüm, değişken izleme, adım/yeniden başlat ve seçim dallarını test etme tasarımı eksik | VN üretimine uygun örnek debug akışı, düzenleme/çalışan kopya ayrımı, aktif dal işareti; export ilerleme/iptal/hata senaryosu. Tam 3D profiler şartı değil |
| UI16 / P2 | Yerleşim dışa aktarılıyor ama yenilemede sıfırlanıyor; isimli çalışma alanı ve güvenli geri yükleme yok | Hikâye/Sahne/Script gibi kullanıcı tarafından seçilen presetler; yerel düzeni kaydet, bilinmeyen paneli güvenli ele al, sıfırla |
| UI17 / P2 | Ayarlar editör/proje/oyuncu tercihlerini bir arada tasarlıyor; birçok seçenek yalnız taslak | Kapsam ve uygulama zamanı açık olsun: editör tercihi mi, proje varsayılanı mı, oyuncu ayarı mı? İşlemeyen örnek kontrolü “tasarım önizlemesi” diye belirt |
| UI18 / P2 | Uzun isim, dokunmada ayrıntı gösterme, seçim/odak/sabitleme farkı için bütünlük kontrolü gerekli | Hover'a bağımlı olmayan ayrıntı, tutarlı odak, keşfedilebilir kısayollar, hatada ekran okuyucu bildirimi. Tam kontrast/ekran okuyucu denetimi bu turda yapılmadı |
| UI19 / P1 | Galeride örnek hata durumları var; kayıp medya, salt okunur proje, recovery, çatışma ve bağlama özel hata sonrası kararlar bütün iş akışına bağlanmıyor | Her kritik üretim görevine normal/boş/hata/iptal varyantı; hatanın ardından çalışmaya dönüş gösterilsin |
| UI20 / P2 | 30 düğüm/50 varlık görsel yoğunluk örneği; büyük proje performans kabulü yok | 500/1000 öğe, çok panel, uzun metin için ölçüm; gerekirse görünür alan render/virtualization. Şimdiden performans hatası var denmiyor |

## Uygulama sırası

1. **Veri koruma + işlem geçmişi:** UI11/UI12, ardından UI06/UI07. Başarısız kayıt/kurtarma taslağı ve undo sözleşmesi oluşturulsun. Geri alınan değişiklik kaydedilmiş duruma doğru dönebilsin; yeni işlem redo dalını temizlesin. Metin düzenlemesi ile proje kısayolları çakışmasın.
2. **Gerçek hikâye düzenleme görünümü:** UI08. Tek standart düğüm + bileşen modelini koruyarak iki/üç kollu hikâye, hedef kaybı, dal etiketleri, seçileni çerçeveleme tasarlansın.
3. **Sahne ve varlık üretimi:** UI09/UI10. Çok objeli sahne, bağımsız varlık Inspector'u, atama ve eksik varlık örnekleri. Obje seçim bağlamı ile varlık seçim bağlamı açık ayrılsın.
4. **Sorunu bulup düzeltme:** UI14/UI15/UI19. Graph hatasından ilgili alana, Lua tanılamasından satıra, Game'den aktif düğüme geçiş tasarlansın. Export başarısızlığının geri dönüşü gösterilsin.
5. **Günlük kullanım ve aktarım hazırlığı:** UI13/UI16/UI17/UI18/UI20. Kalıcı düzen, dosya kapatma, kapsamı açık ayarlar ve büyük örnekler. Boyut/typography/animasyon tokenları, panel sözleşmeleri ve kabul görüntüleri dondurulsun.

Her dilim bağımsız ve küçük tutulmalı. Yeni düğmelerle üst kapsülü büyütmek yerine bağlam menüsü, Inspector ve mevcut araçlar kullanılmalı. Süre tahmini ancak bağlantı/çoklu seçim kapsamı netleşince anlamlı olur.

## Prototip kabul görevleri

| Görev | Beklenen davranış |
| --- | --- |
| Sahne kur → varlık ata → diyalog yaz | Seçim bağlamı anlaşılır; değişiklik tüm ilgili panellere yansır; geri alınır |
| İki seçimli dal oluştur → hedefi sil → düzelt | Bağlantılar gerçeği gösterir; hedef kaybı tanılanır; sorundan ilgili yere gidilir |
| Oynat → duraklat → aktif düğüme git | Game çalışma kopyası ile Edit Scene ayrımı görünür; önizleme veriyi değiştirmez |
| Düzenle → Hub'a dön → kayıt hatası | Kaydet/Vazgeç/İptal açık; başarısız kayıtta değişiklik ve kurtarılabilir kayıt korunur |
| Script değiştir → dosyayı kapat | Kirli dosya kararı görünür; seçilene göre odak doğru yere döner |
| Düzeni değiştir → yenile → geri dön | Kullanıcı tercihiyle düzen geri gelir; geçersiz panel verisi güvenli ele alınır |
| Uzun içerik + dar yatay ekran | Kontroller ulaşılabilir; metin okunur; elle ayarlanan panel yüksekliği kaybolmaz |

Bu görevlerin tamamı örnek veri ve kontrollü hata enjeksiyonuyla gösterilebilir. Native aktarım kabulü ayrıca gerçek Avalonia penceresi, DPI, klavye/dokunma ve motor bağlantılarıyla yapılır.

## Referans araştırması

[Godot editör arayüzü](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html), sahne/dosya/Inspector panellerinin üretim bağlamındaki ilişkisini anlatır. [Godot Inspector](https://docs.godotengine.org/en/stable/tutorials/editor/inspector_dock.html) seçili node ve kaynak özelliklerinin düzenlenmesini gösterir. [Unity Inspector](https://docs.unity.com/en-us/engine/6000.7/manual/unity-editor/editor-windows-views-reference/using-the-inspector) obje, bileşen ve varlık bağlamını karşılaştırmak için kullanıldı.

Bu kaynaklardan çıkarım: Rowl için panel sayısını artırmaktan önce seçim bağlamı, bağlı özellikler ve tanılamaya dönüş yolu güçlendirilmeli. Rowl bir VN/hikâye motoru olduğu için 3D fizik, materyal pipeline veya genel amaçlı motorların bütün araçlarını kopyalamak bu raporun önerisi değildir.
