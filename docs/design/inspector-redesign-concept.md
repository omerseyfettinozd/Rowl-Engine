# Denetçi — Yazarın Stüdyosu

## 1. En Büyük 6 Sorun
1. **Öncelik ters:** Diyalog düğümü açılıyor ama yazarın ilk aradığı “Ne diyor?” ekranda yok; arka planın `X: 0` değeri ve hash’li dosya adı önde.
2. **Kokpit yoğunluğu:** Tek düğümde ~62, yalnızca Diyalog bileşeninde 27 kontrol var; hepsi aynı anda görünür olmaya çalışıyor.
3. **Programcı dili:** `ID: b176dacc`, ContentId, `3a72957d...jpg`, hex, easing, Paralaks, 1080p; ayrıca Background Layer / Dialogue Node ile dil karışık.
4. **Sonuç görünmeden sayı isteniyor:** X/Y, genişlik, ölçek, `0,0°`, `1,00x` yazarın zihninde sahneye dönüşmüyor; yanlış ancak oynatınca anlaşılıyor.
5. **Yazar “bileşen” düşünmeye zorlanıyor:** “Kim konuşuyor?”u bulmak için 8 bileşen ve her karttaki Etkin / Ayar / `...` ayrımını çözmesi gerekiyor.
6. **Hiyerarşisiz gürültü:** Kutu içinde kutu, her alana siyah kutu ve kenarlık; nadir kullanılan Kütüphane/Grup işlemleriyle sahnenin sözü aynı ağırlıkta.

## 2. Yeni Bilgi Mimarisi
*Akış, hikâyenin akışıdır: önce söz, sonra görünüm, seçim, ses ve geçiş.*

| Bölüm | Varsayılan görünür (2–4 öğe) | “Ayrıntılar”a katlanan | Sahne Düzenle’ye taşınan |
|---|---|---|---|
| **Sahnenin adı** | Büyük, düzenlenebilir ad; Bölüm seçimi | Gruplar, renk etiketi, Kütüphaneye kaydet, kimlik | — |
| **Kim konuşuyor?** | Karakter (portre + ad); Duygusu; Konumu: Sol / Orta / Sağ | Anlatıcı modu, ad rengi, ses tonu | Karakteri sürükleme; boyunu kenarından ayarlama |
| **Ne diyor?** | Büyük metin alanı; Seslendirme: Ekle / Dinle; Yazı belirişi | Otomatik devam, bekleme süresi | Yazı alanını taşıyıp boyutlandırma |
| **Sahne nasıl görünüyor?** | Arka plan önizlemesi + Değiştir; Işık: Gündüz / Akşam / Gece | Katmanlar, karartma, bulanıklık, renk havası | Görselleri sürükleme, büyütme, döndürme |
| **Oyuncunun seçimleri** | Seçim satırları (söz + gittiği sahne); `+ Seçim ekle` | Görünüm şablonu, süre sınırı, gösterim koşulu | Seçimlerin yeri, sırası ve aralığı |
| **Ses** | Müzik (ad + Dinle + Değiştir); Ses efekti; Ses düzeyi | Yavaşça başla/bitir, tekrar, seslendirme düzeyi | — |
| **Geçiş ve kamera** | Geçiş kartı; Süre; Kamera: Sabit / Yaklaş / Uzaklaş | Sarsıntı şiddeti, yumuşatma tarzı, odak | Kamera çerçevesini sürükleyip yakınlaştırma |

> “Etkin” kutuları kalkar: bölüm boşsa kapalıdır, içerik eklenince kullanılır; kapatma isteği **Ayrıntılar > Bu sahnede kullanma** altındadır.
> “Betik” diye ayrı kapı yok: koşullar ilgili bölümde cümleyle kurulur — “Bu seçim, yalnızca Elif’e güvenildiyse görünür.”

## 3. Sayı Yerine Seçim
- **Konum:** Sol / Orta / Sağ parçalı düğmesi; ince ayar yalnızca sahnede sürükleyerek.
- **Boyut:** Sığdır / Doldur / Tamamını göster; sayılar değil, sonuç önizlemesi konuşur.
- **Dönüş, derinlik, opaklık:** Dönüş: Düz / Hafif eğik; Derinlik: Yok / Hafif / Belirgin; Opaklık kaydırıcısında yüzde yerine Soluk — Tam.
- **Renk:** Adlı hazır palet: Gece, Şafak, Mum Işığı, Yağmur; hex yalnızca **Ayrıntılar > Özel renk** içinde.
- **Kamera:** Ham easing metni yerine Yavaşça yaklaş / Usulca uzaklaş / Birden kes; 8 sarsıntı sayısı yerine Yok / Hafif titreme / Deprem.
- **Geçiş:** Küçük hareketli önizleme kartları (Kararma, Perde, Yumuşak geçiş) + süre çipleri: 0,5 sn / 1 sn / 2 sn.
- **Seçim görünümü:** X/Y/renk/yazı tipi tek tek değil; şablon kartları: Altta liste / Ortalanmış büyük tercihler / İki sütun.
- **Dosya ve hız:** Hash ve yol hiç görünmez; görsel galeriden küçük resimle, ses dalga biçimi ve Dinle ile seçilir. Yazı hızı: Yavaş / Normal / Hızlı / Anında.

## 4. Görsel Dil — 8 Kural
1. **Tipografi:** Üç düzey yeterlidir: sahne adı, bölüm sorusu (“Kim konuşuyor?”), içerik. Büyük harfli minik teknik etiketler yoktur; diyalog metni panelin en rahat okunan yazısıdır.
2. **Boşluk:** Bölümler kartlarla değil, bol boşluk ve ince bir ayraçla ayrılır; her bölümde yalnızca bir ana eylem öne çıkar.
3. **Kenarlık:** Kenarlık yalnızca yazılabilir alanı ve seçili nesneyi gösterir; iç içe kartlar ve her satıra kutu yoktur.
4. **Tek vurgu rengi:** Sıcak kehribar yalnızca ana eylem, seçim ve odak için kullanılır; etiket renkleri küçük bir noktada kalır, arayüzle yarışmaz.
5. **İkonlar:** İkon daima sözcüğüyle gelir: Dinle, Müzik, `+ Seçim ekle`. Yalnız `...` ve ne yaptığı belirsiz “Ayar” yoktur.
6. **Boş durumlar:** Boş kontroller yerine tek cümle + eylem: “Henüz müzik yok. [Müzik seç]”; boş bölüm sessizce tek satıra küçülür.
7. **Hatalar:** Kod ya da yalnız kırmızı çerçeve değil, yerinde ve çözümüyle insan cümlesi: “Bu görsel bulunamıyor — [Başka görsel seç]”.
8. **Sakin his:** Yumuşak koyu zemin, düşük kontrastlı yardımcı metin, geniş nefes alanı; hareket eden tek şey, istendiğinde geçiş/kamera önizlemesidir.

## 5. Seçim Yokken ve Çoklu Seçimde
- **Seçim yokken:** Denetçi boş ve kilitli görünmez; hikâyenin adı, bölüm listesi ve sahne sayıları, “Son çalışılan sahneye devam et” ve `+ Yeni sahne` görünür: “Düzenlemek için bir sahne seçin.”
- **Birden çok düğüm seçiliyken:** Başlık “6 sahne seçili” der ve adları kısaca listeler. Yalnızca ortak kararlar gösterilir: Bölüm, Etiket, Geçiş, Müzik. Değerler farklıysa boş değil, **“Her biri farklı”** yazar; bir seçim yapılırsa hepsine uygulanacağı açıkça belirtilir. Metin alanları hiç açılmaz. Altta: Birlikte taşı / Çoğalt / Sil.