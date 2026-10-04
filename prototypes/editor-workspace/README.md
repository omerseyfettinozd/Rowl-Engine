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
- **Edit Scene:** sahnenin doğrudan düzenlenebilir görünümü. Ay, Mira ve diyalog kutusu tutulup sürüklenebilir; oyun çalışmıyorken konumları Game'e anında yansır. Seçili obje ok tuşlarıyla da taşınabilir; Shift adımı büyütür.

Edit Scene, inspector formu değildir. Inspector için ayrı bir kullanıcı tasarımı bekleniyor.
Üstte üç ekran simgesi, yanlarında başlat/kapat ve duraklat/devam et; sağda küçük seçenek düğmesi var.

## Oynatma

- **▶ Başlat:** seçili sahnenin kopyasını oynatır, Game penceresini açar. Aynı düğme **■ Oyunu kapat** olur. Mevcut pencere sınırı ve sabitleme kuralları korunur.
- **Ⅱ Duraklat:** Game'in o anki karesini dondurur. Tekrar tıklamak kaldığı yerden devam eder; bekleme süresi oynatıma eklenmez.
- Yerel örnekte Mira'nın hafif hareketi ve diyalog yazısının görünmesi oynatımı gösterir. Duraklatınca ikisi de donar; Game'deki Devam et devre dışı kalır.
- Edit Scene ve Node kullanılabilir. Sahne seçimleri/düzenlemeleri çalışan veya donmuş Game kopyasını değiştirmez. Oyunu kapatınca Game yeniden düzenlenen sahnenin önizlemesini gösterir.
- Ekran simgesi yalnız pencereyi açar/kapatır; oyunu kapatmak için ■ kullanılır. Oyun kapalıyken duraklat devre dışıdır.
- Bu, tarayıcı prototipinin örnek oynatımıdır; motor çalıştırılmaz.

## Pencere yerleşimi

- Başlığı sürükle: başka pencerenin sol/sağ/üst/alt tarafına bırakınca o alan bölünür. Ortasına bırakıldığında da en yakın tarafa göre bölünür. Bölmeler iç içe kurulabilir; örneğin Node solda, Game ve Edit Scene sağda alt alta.
- Bütün pencereler kendi bölmesinde kalır; birbirinin üstüne binmez. Serbest pencere modu kaldırıldı. Sürüklerken pencere yerinde kalır, yalnız hedef bölmenin önizlemesi gösterilir.
- Bölmeler arasındaki çizgiler sürüklenerek boyutlandırılır. Klavyede çizgiye odaklanıp ok tuşları kullanılabilir.
- Pencere başlığındaki yerleşim düğmesi aynı işlemler için dokunmatik/klavye alternatifi sunar: hedef pencere ve dört yön.
- Escape veya pointer iptali sürüklemeyi geri alır. Alan dışına bırakılan pencere önceki konumunda kalır.
- Mobilde yeni açılan pencereler varsayılan olarak alt alta yerleşir; kullanıcı yönlerini ve boyutlarını değiştirebilir. Üçlü başlangıçta alan dengeli paylaşılır.

## Açık pencere sınırı ve sabitleme

- Başlangıçta yalnız Node açık, sınır iki pencere. Üst simge gizli ekranı açar; aynı simgeye tekrar basmak açık ekranı kapatır. Sabit ve son pencere de böyle kapatılabilir.
- Sağ üst seçenek menüsünde üç pencere seçimi tüm ekranları açar. İkiye dönüş sabit olmayan en eski pencereyi kapatır.
- Her pencerenin başlığında yerleşim, sabitleme ve görünür **× kapatma** düğmeleri var. Çok dar pencerede sabitleme yerleşim menüsünden de kullanılabilir.
- **Pencereyi sabitle**, otomatik ekran değiştirmede o pencerenin korunmasını sağlar. Kullanıcı sabit pencereyi bizzat taşıyabilir veya × ile kapatabilir.
- İki pencere doluyken üçüncü simge, sabit olmayan en eski pencerenin aynı bölmesini devralır.
- Bütün açık pencereler sabitse yeni ekran açılmaz; açıklama gösterilir. Üçü sabitse ikiye geçmeden sabitleme kaldırılmalıdır.
- ×, son açık pencere dahil herhangi bir pencereyi kapatır. Boş çalışma alanında üst simgelerden yeniden ekran açılabilir. Kapatılan pencerenin sabitlemesi kaldırılır. Seçenek menüsündeki başlangıca dön eylemi örnek veriyi ve yerleşimi sıfırlar.

## Doğrulama — 2026-10-04

21 durum testi: pencere kuralları ve 1000 karma işlemde alanların çakışmamasına ek olarak üst simgeyle kapatma, oynatma kopyasının bağımsızlığı, hareket/yazının donması, devam etme ve yeniden başlatma.

```sh
node --test prototypes/editor-workspace/*.test.mjs
```

Son düzeltmede masaüstü ve 390×844 görünümde başlığı başka pencerenin ortasına bırakma kontrol edildi; sonuçta pencerelerin gerçek DOM alanları çakışmadı. Bölme boyutlandırma ve dört yönlü yerleşim menüsü çalışıyor. Kapatma, sabitleme ve üst simgeden yeniden açma korunuyor.
Yeni oynatma kontrolleri masaüstünde doğrulandı: başlat/kapat dönüşümü, duraklatılan karenin Node sahne seçimi değişse bile aynı kalması, kaldığı yerden devam ve kapatınca düzenleme önizlemesine dönüş. Üst simgeden son pencereyi ve Game'i kapatma da doğrulandı. 320×740 görünümde beş üst düğme ve seçenekler taşmadan sığıyor; 390×844 oynatma/duraklatma önizlemesi kaydedildi.
Bu kontrol gerçek mobil cihaz kabulü değildir.

Siyah/kemik beyazı paleti mevcut KARAR-0 kararından gelir. SVG sahne yereldir; harici font, resim, API veya motor bağlantısı yoktur. Örnek veri sayfa yenilenince sıfırlanır.

![Masaüstü karma pencere düzeni](previews/desktop.jpg)

[Mobil ekran görüntüsü](previews/mobile.jpg)
