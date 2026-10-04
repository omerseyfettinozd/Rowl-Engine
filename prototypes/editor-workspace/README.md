# Rowl çalışma alanı — tarayıcı prototipi

Motor ve Avalonia editöründen bağımsız, örnek veri kullanan tasarım denemesi.
Paket kurulumu ve derleme gerektirmez. Dosyalar yerel HTTP sunucusuyla açılır:

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory prototypes/editor-workspace
```

Tarayıcı: `http://127.0.0.1:4173`

## Davranış

- Başlangıçta Node açık; varsayılan sınır iki panel.
- Üst ortadaki simgeler gizli ekranı açar, açık ekranı odaklar.
- İki panel doluyken üçüncü simge, sabit olmayan ve en uzun süredir odaklanılmayan panelin yerini alır.
- İğne simgesi panelin yerini korur. Bütün açık paneller sabitse yeni ekran açılmaz; açıklama gösterilir.
- Üç panel seçimi bütün ekranları açar. İkiye dönünce sabit paneller korunur. Üçü de sabitse önce bir sabitleme kaldırılmalıdır.
- Panel başlığındaki çarpı kapatır. Son panel veya sabit panel kapatılamaz.
- Masaüstünde yan yana; 900 px ve altında alt alta. Boyut değişimi açık panel ve sabitleme durumunu değiştirmez.
- Node paneli daralınca kartlar küçültülmek yerine dikey bir akışa geçer; panel içinde kaydırılabilir.
- Node seçimi ve Game'deki devam düğmesi örnek sahneyi değiştirir. Düzenle alanındaki metin ve görünüm ayarları Game'e anında yansır.
- Sağ üstteki sıfırlama düğmesi ilk örnek veriye ve tek Node görünümüne döndürür.

Siyah ve kemik beyazı paleti mevcut `docs/C1_VISUAL_TOKENS_AND_MOCKS.md` içindeki güncel KARAR-0 kararından gelir.
Örnek sahne görseli yerel SVG'dir. Harici font, resim, API veya motor bağlantısı yoktur. Değişiklikler sayfa yenilenince sıfırlanır.

Panel durum doğrulaması: `node --test prototypes/editor-workspace/workspace-state.test.mjs`

## İlk doğrulama — 2026-10-04

Beş durum testi geçti. Tarayıcıda tek/iki/üç panel, sabit Game korunarak ekran değişimi ve canlı diyalog düzenleme kontrol edildi.
1440×900, 390×844 ve 320×740 görünüm boyutları incelendi; mobilde yatay sayfa taşması gözlenmedi.
Tarayıcı hata/uyarı kaydı boştu. Bu kontrol gerçek mobil cihaz kabulü değildir.

![Masaüstü üçlü görünüm](previews/desktop.jpg)

[Mobil ekran görüntüsü](previews/mobile.jpg)
