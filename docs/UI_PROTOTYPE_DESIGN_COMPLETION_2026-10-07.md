# Web prototipi — tasarım planı uygulama ve değerlendirme

Tarih: 7 Ekim 2026. Kapsam: `prototypes/editor-workspace`. Native/Avalonia UI aktarımı başlamadı; bunun için kullanıcının ayrı onayı bekleniyor.

Önceki tasarım değerlendirmesi 86/100 idi. Planın web kapsamı uygulandı; aşağıdaki örnekler ve tarayıcı kontrollerinden sonra güncel öznel tasarım puanı **92/100**. Bu puan motorun işlevselliği, ürünün tamamlanma oranı veya kullanıcı araştırması sonucu değildir.

## Uygulanan plan

| Dilim | Uygulama | Gözlenen sonuç |
| --- | --- | --- |
| Sahne okunabilirliği | Game ve Edit Scene içinde %100–300 yakınlaştırma, sığdır, kaydırılabilir sahne ve açılabilir 16 px okuma alanı | %200 yakınlaştırmada sürükleme yüzdeleri doğru; Inspector metni okuyucuya anında yansıyor; Game okuyucusu oynatılan metni izliyor |
| Karmaşık yerleşimler | Dock yüksekliği panel minimumlarından hesaplanıyor; dikey oranlar da sınırlandırılıyor; komşu sütun seçimi yükseklikle dengeleniyor | 1366 px dizüstünde yedi panel korunuyor; Inspector 360, kütüphane 320, Varlıklar 240 px yüksekliğin altında ezilmiyor |
| Uzun ve yoğun içerik | 2.000 karakter diyalog, uzun adlar, 30 düğüm, 50 varlık, 20 script, 10 bileşen ve 30 kullanıcı kütüphanesi kaydı | Başlıklar kontrollü satırlanıyor; dosya sekmeleri kısaltılıyor; varlık araması ve panel içi kaydırma çalışıyor |
| Büyük hikâye akışı | Ölçeklenmiş graph için gerçek kaydırma alanı; varsayılan sığdırmada %75 ölçek tabanı | Kompakt ve geniş graph görünümünde 30. düğüm erişilebilir; tüm düğümleri minik metne indirerek sığdırma önlendi |
| Hub | Boş, tek ve 12 kartlık örnekler; arama ve eşleşmeme; kart önizlemesi | 320 px boş ekran ve örnek kartlar sığıyor; 1440 px görünümünde 12 kart dört sütunda; arama sonucu ve geri dönüş çalışıyor |
| Durum tasarımları | Boş/eşleşmeme, yükleniyor, hata/yeniden dene, başarı, kaydedilmemiş, devre dışı ve alan hatası | Hata → yükleniyor → başarı akışı; alan yanında hata, ilk hataya odak, düzeltme sonrası başarı; renk yanında açıklama/simge |
| Görsel son düzenlemeler | Mevcut kapsül ve palet korundu; yeni sahne kontrolleri mevcut panel altında; 44 px hedefler, tutarlı yardımcı metin ve durum kartları | İkinci uygulama barı eklenmedi; 320 px görünümünde sahne kontrolleri sığıyor; hareket azaltmada yükleme animasyonu da duruyor |

## Tasarım örneklerini açma

Yerel sunucu:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory prototypes/editor-workspace
```

Galeri: <http://127.0.0.1:4173/review.html>. Standart kendi çalışma alanı: <http://127.0.0.1:4173/>.

Galeri 12 senaryoyu içerir. Yalnız izin verilen `?review=` adları tasarım modunu açar. Bu modda proje, ayar, Lua ve kütüphane kayıtları oturum içindeki ayrı belleğe gider; gerçek localStorage okunmaz/yazılmaz. Sayfa yenilenince örnek başlangıcına dönülür. Depolama sınırı, gerçek arka uç yerine hata atan bir depolama nesnesiyle test edildi; kaynakta gerçek localStorage erişimi yalnız adaptörün normal-mod arka ucunda bulunuyor. Normal modun anahtarları ve mevcut JSON formatları korundu.

Hub kartları gerçek dosya açmaz; yoğun varlık kataloğu gerçek dosya içe aktarması değildir. Durum galerisi gerçek motor işlemi çalıştırmaz. Bu sınırlar ekran metinlerinde belirtilir.

## Doğrulama

- `node --test prototypes/editor-workspace/*.test.mjs`: **41/41 geçti**. Yeni kontroller: yakınlaştırma sınırları, gerçek çerçeveye göre yüzdelik hareket, dikey sahne oranı/merkez korunması, geniş graph erişimi, normal dock minimum yüksekliği, farklı ağaç/ekran genişlikleri, değişmez örnek veri ve depolama izolasyonu. Önceki çalışma alanı, oynatma, Inspector, kütüphane ve ayar regresyonları da geçti.
- Tüm JS/MJS dosyaları `node --check` ile geçti; `git diff --check` temiz.
- Canlı tarayıcı: 1440×900, 1366×900, 768×900, 390×844, 320×740. Ek geniş graph kontrolü: 2400×900. Yerleşim eşiği için 1039/1040/1080 px tarayıcı genişlikleri incelendi. **1040 px eşiği tarayıcı değil çalışma alanı genişliğidir**; 1080 px tarayıcıda 1054 px çalışma alanı komşu sütun sunumunu kullanıyor.
- %200 Edit Scene sürükleme: 518,4×324 px çerçevede 20×10 px hareket, X=26 → 29,86 ve Y=35 → 38,09 sonucunu verdi. Klavye sağ oku önce X=25 → 26 yaptı. Okuma alanındaki metin Inspector değişikliğini izledi.
- 2.000 karakter okuyucu metni 390 px ekranda 16 px olarak kaldı. Game duraklatıldığında hem sahne hem okuyucuda aynı gösterilen metin bulundu.
- Tekrar aç/kapat sonrası Game çoğalmadı; hareket azaltma açıkken spinner `animation: none`, düğüm geçişi `0s` oldu. 1080×1920 dikey sahne seçimi yaklaşık 9/16 çerçeve oranını korudu.
- Hub araması, örnek kart önizlemesi/geri dönüş, boş Hub → üç örnek kart, varlık araması, alan hatası/düzeltme ve durum akışları canlı UI üzerinden incelendi. Gözlenen tarayıcı hata/uyarı kayıtları boştu.

[Ekran kayıtları ve test çıktısı](verification-evidence/2026-10-07-ui-completion/README.md).

## Puanlama

Aynı 100 puanlık kategori yapısı kullanıldı; güncel değerler yeni gözlemlerin değerlendirmesidir.

| Boyut | Puan | Gerekçe ve kalan pay |
| --- | ---: | --- |
| Görsel tutarlılık | 19/20 | Palet/kapsül korunuyor; durum yüzeyleri ve kontrol boyutları tutarlı. Son kişisel estetik kararlar açık. |
| Bilgi hiyerarşisi | 18/20 | Uzun başlıklar, yoğun Inspector ve Hub daha düzenli. Çok karmaşık gerçek proje davranışı henüz bu örnekte yok. |
| Ekrana uyum | 18/20 | Yoğun dizüstü, tablet, dar telefon ve dikey sahne incelendi. Gerçek dokunmatik cihaz ve farklı tarayıcı kabulü eksik. |
| Metin okunabilirliği | 14/15 | 16 px canlı okuyucu ve yakınlaştırma var. Sahne geometrisinin içindeki metin sahneyle ölçeklenmeye devam ediyor. |
| Etkileşim ve geri bildirim | 14/15 | Hatalar ve durum akışları görünür; zoom altında taşıma doğru. Gerçek Hub/işlem servisleri bu prototipe bağlı değil. |
| Hareket ve geçiş | 9/10 | Hareket azaltma yeni spinner için de çalışıyor; açık/kapat davranışı korundu. Fiziksel cihazda kare süresi ölçülmedi. |
| **Toplam** | **92/100** | Tasarım prototipinin gözlenen mevcut durumu |

## Kalanlar ve aktarım sınırı

Web planındaki örnek tasarım işleri tamamlandı. Kullanıcının görsel incelemesi ve kendi tasarım düzenlemeleri sıradaki adım. Gerçek telefon/dokunma, farklı tarayıcılar, tüm olası dock ağaçları ve düşük donanımda animasyon performansı ayrıca değerlendirilmeli; tarayıcı ekran boyutu testi bunların kabulü sayılmaz. Yeni yakınlaştırma ve okuyucu durumları oturumluk; kalıcı çalışma alanı özelliği olarak sunulmadı.

Native/Avalonia aktarımı yalnız kullanıcı onayından sonra ele alınacak. Motor, C API, P/Invoke, gerçek proje açma/kaydetme ve paket üretimi bu dilimde değiştirilmedi. Prototipteki yeni kütüphane/Hub görselleri mevcut motor özelliği gibi değerlendirilmemeli.
