# Rowl çalışma alanı — tarayıcı prototipi

Motor ve Avalonia editöründen bağımsız, örnek veri kullanan tasarım denemesi.
Paket kurulumu ve derleme gerektirmez:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory prototypes/editor-workspace
```

Tarayıcı: `http://127.0.0.1:4173`.

## Ekranlar

- **Node:** örnek hikâye akışı ve sahne seçimi.
- **Game:** oyun sahnesinin önizlemesi.
- **Edit Scene:** Game ile aynı sahnenin doğrudan düzenlenebilir görünümü. Ay, Mira ve diyalog kutusu tutulup sürüklenebilir; konumları Game'e anında yansır. Seçili obje ok tuşlarıyla da taşınabilir; Shift adımı büyütür.

Edit Scene, inspector formu değildir. Inspector için ayrı bir kullanıcı tasarımı bekleniyor.
Üst alan üç ekran simgesi ve küçük bir seçenek düğmesinden oluşur. Proje çubuğu, tasarım rozeti ve alt bilgi çubuğu kaldırıldı.

## Pencere yerleşimi

- Başlığı sürükle: başka pencerenin sol/sağ/üst/alt kenarına bırakınca o alan bölünür. Bölmeler iç içe kurulabilir; örneğin Node solda, Game ve Edit Scene sağda üst üste.
- Ortasına veya boş alana bırakınca serbest pencere olur. Serbest pencere başlıktan taşınır, sağ alt köşeden boyutlandırılır.
- Bölmeler arasındaki çizgiler sürüklenerek boyutlandırılır. Klavyede çizgiye odaklanıp ok tuşları kullanılabilir.
- Pencere başlığındaki yerleşim düğmesi aynı işlemler için dokunmatik/klavye alternatifi sunar: hedef pencere, dört yön, serbest pencere ve çalışma alanına geri yerleştirme.
- Bir serbest pencereyi diğerinin yanına dock etmek için önce hedefi çalışma alanına yerleştir.
- Escape veya pointer iptali sürüklemeyi geri alır. Alan dışına bırakılan pencere önceki konumunda kalır.
- Mobilde yeni açılan pencereler varsayılan olarak alt alta yerleşir; kullanıcı yönlerini ve boyutlarını değiştirebilir. Üçlü başlangıçta alan dengeli paylaşılır. Serbest pencere konumları çalışma alanına göre oranla tutulur.

## Açık pencere sınırı ve sabitleme

- Başlangıçta yalnız Node açık, sınır iki pencere. Üst simge gizli ekranı açar, açık ekranı odaklar.
- Sağ üst seçenek menüsünde üç pencere seçimi tüm ekranları açar. İkiye dönüş sabit olmayan en eski pencereyi kapatır.
- Pencere menüsünde **Pencereyi sabitle**, otomatik ekran değiştirmede o pencerenin korunmasını sağlar. Kullanıcı sabit pencereyi bizzat taşıyabilir.
- İki pencere doluyken üçüncü simge, sabit olmayan en eski pencerenin aynı dock alanını veya serbest dikdörtgenini devralır.
- Bütün açık pencereler sabitse yeni ekran açılmaz; açıklama gösterilir. Üçü sabitse ikiye geçmeden sabitleme kaldırılmalıdır.
- Son açık pencere veya sabit pencere kapatılamaz. Seçenek menüsündeki başlangıca dön eylemi örnek veriyi ve yerleşimi sıfırlar.

## Doğrulama — 2026-10-04

14 durum testi: kapasite/sabitleme, dört yönde iç içe yerleştirme, serbest/dock dönüşü, aynı konumu devralma, oran sınırları, mobil başlangıç ve 1000 karma işlemde pencere tekilliği.

```sh
node --test prototypes/editor-workspace/*.test.mjs
```

Tarayıcıda masaüstü başlık sürükleme, üst/alt yerleştirme, bölme ve serbest pencere boyutlandırma, tek serbest pencerenin dock'a dönüşü, klavyeyle obje taşıma ve Game/Edit Scene ortak obje konumları kontrol edildi.
390×844 görünümde de obje sürükleme ve pencere yerleşimi kontrol edildi; yatay sayfa taşması gözlenmedi.
Bu kontrol gerçek mobil cihaz kabulü değildir. Tarayıcı hata/uyarı kaydı boştu.

Siyah/kemik beyazı paleti mevcut KARAR-0 kararından gelir. SVG sahne yereldir; harici font, resim, API veya motor bağlantısı yoktur. Örnek veri sayfa yenilenince sıfırlanır.

![Masaüstü karma pencere düzeni](previews/desktop.jpg)

[Mobil ekran görüntüsü](previews/mobile.jpg)
