# Uygulama Durumu

Son doğrulama: Linux Debug derlemesi, CTest ve editör headless testi geçiyor.

## Hazır

- C++20 çekirdek, SDL3 framebuffer/player, P/Invoke editör köprüsü ve graph tabanlı hikâye oynatımı.
- Sürümlü save dosyaları; değişken, sahne arka planı, BGM yolu/volümü/aktiflik ve DSP bilgisi.
- Lua sandbox, `script` component, `on_enter`, `on_update`, `on_choice` ve `on_exit` callback'leri.
- WAV oynatma, PCM üzerinde Telephone/Underwater/Cave DSP, ducking durum sorguları ve ses hata sorgusu.
- MSDF atlas meta verisi, RGB median örnekleme ve glyph ölçüm çekirdeği; atlas yoksa TrueType fallback.
- VFS salt-okunur stream arayüzü; loose dosyalar doğrudan stream olarak açılır.

## Sonraki üretim işleri

- SDL GPU backend ile MSDF atlasının oyuncu render yolunda çizilmesi; şu an MSDF CPU çekirdeği hazır, görsel çizim fallback fontta.
- Birden fazla script component için callback isim alanı ve editor backlog paneli.
- Android/iOS host projeleri ve fiziksel cihaz doğrulaması.

Bu dosya, hedef mimari belgelerindeki gelecek vaatleri ile test edilmiş kodun durumunu ayırmak için tutulur.
