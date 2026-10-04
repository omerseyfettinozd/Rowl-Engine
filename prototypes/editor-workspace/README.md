# Rowl çalışma alanı — tarayıcı prototipi

Motor ve Avalonia editöründen bağımsız, yerel örnek veri kullanan tasarım denemesi.
Paket kurulumu ve derleme gerektirmez:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory prototypes/editor-workspace
```

Tarayıcı: `http://127.0.0.1:4173`.

## Ekranlar ve araçlar

- **Node:** örnek hikâye akışı ve sahne seçimi.
- **Game:** oyun sahnesinin önizlemesi.
- **Edit Scene:** aynı sahnede Ay, Mira ve diyalog kutusunu sürükleyerek düzenle. Seçili obje ok tuşlarıyla da taşınır; Shift adımı büyütür.
- **Hiyerarşi:** seçili node'un objeleri; seçim Edit Scene ve Inspector ile ortak.
- **Inspector:** seçili objenin konum/boyut yüzdeleri, görünürlüğü ve diyalog metni. Değerler sahne sınırlarında tutulur.
- **Varlıklar:** örnek varlıkları isimle filtrele, seçerek Edit Scene'e geç.
- **Konsol:** gerçek prototip etkileşimlerinin oturum kayıtları; Temizle ile boşaltılır.

PC ve mobilde aynı üst kapsül var: **Node · Game · Edit Scene | Başlat · Duraklat | Araçlar · …**. Yedi düğmenin dokunma alanı 44×44 piksel; kapsül 314 piksel genişliğiyle 320 piksel ekrana sığar. Sağ alttaki düğme kaldırıldı. **Araçlar**, kapsülün altında her iki cihazda da aynı 2×2 isimli menüyü açar. Açık araçlar menüde işaretlenir; ikinci tıklama aynı pencereyi kapatır. Lua kod editörü ileride bu menüye ayrı araç olarak eklenebilir; henüz uygulanmadı.

## Pencere yerleşimi

- Açık pencere sınırı ve otomatik pencere değiştirme kaldırıldı. Yedi pencere birlikte açılabilir; ilk açılışta yalnız Node açık.
- Başlığı başka pencerenin sol/sağ/üst/alt tarafına sürükle: hedef alan bölünür. Pencereler kendi bölmelerinde kalır, birbirine binmez. Örneğin Node solda; Game ve Edit Scene sağda alt alta.
- Araçlar masaüstünde sağ sütunda, mobilde altta gruplanır. Mobil çalışma alanı gerektiğinde aşağı kayar; her araç kendi içinde de kaydırılabilir.
- Bölme çizgisini sürükle veya klavyede odaklayıp ok tuşlarını kullan.
- Başlıktaki yerleşim menüsü dokunmatik/klavye için hedef ve yön seçimi sunar.
- **Sabitleme**, başlıktan sürüklemeyi ve menüden taşımayı kilitler. Bölme boyutları ayarlanabilir; sabit pencere elle kapatılabilir.
- Görünür **×**, son pencere dahil herhangi bir pencereyi kapatır; kapanan pencerenin sabitlemesi temizlenir.
- Escape/pointer iptali sürüklemeyi geri alır; alan dışına bırakma eski konumu korur.

## Geçişler

Pencere açılırken bölme oranları 200 ms içinde yumuşakça değişir; yeni içerik hafifçe belirir. Kapanırken içerik solar, kapanan bölme 180 ms içinde daralır. Mobilde çalışma alanının yüksekliği de geçişe katılır. Animasyonlar pencereyi diğerinin üzerine taşımak yerine flex bölmelerini değiştirir. Hızlı aç/kapat önceki geçişi sonlandırıp güncel duruma geçer. `prefers-reduced-motion` açıkken geçişler anında gerçekleşir.

## Proje menüsü

Sağ üst **…** üzerinden:

- **Proje ayarları:** proje adı.
- **Oyun ayarları:** yatay/dikey çözünürlük ve başlangıç sahnesi.
- **Editör tercihleri:** Node ızgarası ve Edit Scene obje çerçeveleri.
- **Dışa aktar:** sahne verileri, ayarlar ve yerleşimin yerel JSON taslağı; gerçek motor derlemesi değildir.
- **Yerleşimi sıfırla:** açık pencereleri varsayılan yerleşime getirir; obje düzenlemeleri, ayarlar ve oynatma korunur.

Ayarlar bu tarayıcının localStorage alanında saklanır. Örnek sahne düzenlemeleri, yerleşim ve oynatma sayfa yenilendiğinde sıfırlanır.

## Oynatma

- **▶ Başlat:** ayarlardaki başlangıç sahnesinin bağımsız kopyasını oynatır, Game'i açar; düğme **■ Oyunu kapat** olur.
- **Ⅱ Duraklat:** Mira hareketi ve diyalog yazısını mevcut karede dondurur; tekrar tıklamak kaldığı yerden devam eder. Bekleme süresi oynatıma eklenmez.
- Node, Edit Scene ve Inspector kullanılabilir; düzenleme verileri çalışan/donmuş Game kopyasını değiştirmez. ■ ile kapatınca Game düzenleme önizlemesine döner.
- Ekran simgesi yalnız pencereyi açar/kapatır. Game'de Devam et duraklatılmışken; duraklat düğmesi oyun kapalıyken devre dışıdır.

## Doğrulama — 2026-10-05

22 durum testi: yedi pencere açma, sabit/son pencere kapatma, odak, dört yönlü yerleşim, pin kilidi, oran sınırları, 1000 karma işlemde alanların çakışmaması; ayar doğrulama ve Inspector sınırları; bağımsız oynatma kopyası, donma ve devam etme.

```sh
node --test prototypes/editor-workspace/*.test.mjs
```

Yeni kontrol kapsülü 1440×900 ve 320×740 görünümlerinde aynı sıra ve 44×44 düğmelerle doğrulandı. 2×2 araç menüsü 260 piksel genişlikte iki görünümde de aynı yerleşimi kullanır. Sekiz hızlı Game aç/kapat işleminden sonra pencere ve düğme durumları tutarlı kaldı; Inspector kapanıp kaldırıldı ve yeniden açıldı. Başlat/duraklat akışı korundu.

Tarayıcıda 1440×900'de yedi pencere birlikte açıldı; DOM alanları çakışmadı. Hiyerarşi seçiminden Inspector konum/görünürlük düzenleme, Edit Scene'e yansıma ve donmuş Game kopyasının değişmemesi doğrulandı. Varlık filtreleme/seçim, Konsol kayıtları, sabit pencerenin yön düğmelerinin kapanması kontrol edildi. Çözünürlük ve başlangıç sahnesi uygulandı; yenileme sonrası korundu. Izgara/çerçeve tercihleri uygulandı. İndirilen JSON açılarak üç sahne, yedi pencere ve ayarlar doğrulandı.

390×844'te beş pencere çakışmadan alt alta açıldı; Inspector'a kaydırarak ulaşıldı. 320×740'ta üst kontroller ve dört araç düğmesi ekrana sığdı. Gerçek mobil cihaz kabulü yapılmadı.

Siyah/kemik beyazı palet, yerel SVG sahne. Harici font, resim, API veya motor bağlantısı yok.

![Masaüstü araç pencereleri](previews/desktop.jpg)

[Mobil araç pencereleri](previews/mobile.jpg)
