# Yaratıcı Hikâye Stüdyosu Tasarım Sistemi

## Amaç

Editör, ilk kez hikâye kuran kişiyi proje açma, sahne kurma, graph düzenleme,
önizleme ve export sırasıyla yönlendirir. Deneyimli kullanıcı aynı anda graph,
inspector ve canlı önizlemeye erişmeye devam eder; çalışma alanı davranışı
görsel yenileme nedeniyle değişmez.

## Görsel temel

- **Renk:** Uzun yazım oturumları için sıcak, koyu mürdüm yüzeyler; mercan
  vurgu (`#F09A78`) birincil eylemi ve seçili odağı taşır. Başarı yeşili,
  uyarı altını ve hata kırmızısı yalnızca durum anlamı için kullanılır.
- **Tipografi:** Arayüz başlıkları kısa ve belirgin, açıklamalar sade ve
  ikincildir. Proje ve sahne adları bilgi hiyerarşisinde dosya yollarından önce
  gelir.
- **Yüzey:** 8 px düğme, 12 px kart yarıçapı; ince sıcak sınırlar ve yumuşak
  gölge katmanları ekranın bölümlerini ayırır.
- **Dil:** Birincil eylemler fiille başlar: “Yeni hikâye”, “Aç”, “Kaydet”.
  Boş durum kullanıcıya sıradaki somut adımı söyler; hata metni sorunu ve
  yapılabilecek düzeltmeyi belirtir.

## Akış ve hiyerarşi

1. **Projeler:** Hub, boş durumda “İlk hikâyeni kur” eylemini verir; mevcut
   projelerde ad, son çalışma zamanı ve Aç eylemi ilk bakışta görünür.
2. **Sahneler:** Üst araç çubuğundaki Sahneler graph canvas’ını açar.
3. **Önizleme:** Önizleme ve Oyuncu, graph düzeni korunurken hikâyenin iki
   çalışma görünümünü açar.
4. **Düzenleme:** Sol panel hikâye yapısını, merkez alan graph/önizlemeyi,
   sağ panel seçili node’un ayrıntılarını taşır. Alt panel günlük, asset ve
   doğrulama sonuçlarına ayrılmıştır.
5. **Export:** Masaüstü build seçicisi Linux, Windows, macOS ve yalnız paket
   seçeneklerini gösterir. Android/iOS host işleri bu UI geçişinden önce
   kapsamda değildir.

## İlk uygulanan dilim

Tema tokenları, Project Hub, boş proje yönlendirmesi, üst çalışma araç çubuğu
ve ana panel yüzeyleri bu belgede tanımlanan dile taşındı. Node kartı ve
component inspector ayrıntı görünümleri sonraki görsel dilimlerdir; mevcut
graph canvas, inspector ve canlı preview davranışları korunur.
