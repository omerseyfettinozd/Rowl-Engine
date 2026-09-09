# Uygulama Durumu

Son doğrulama: Linux Debug derlemesi, CTest ve editör headless testi geçiyor.

## Hazır

- C++20 çekirdek, SDL3 framebuffer/player, P/Invoke editör köprüsü ve graph tabanlı hikâye oynatımı.
- Sürümlü save dosyaları; değişken, sahne arka planı, BGM yolu/volümü/aktiflik ve DSP bilgisi.
- Lua sandbox ve birbirinden izole script component modülleri: her component kendi
  `on_enter`, `on_update`, `on_choice` ve `on_exit` callback'ini taşır; aynı
  callback isimleri başka component'leri ezmez.
- WAV oynatma, PCM üzerinde Telephone/Underwater/Cave DSP, ducking durum sorguları ve ses hata sorgusu.
- MSDF atlas meta verisi, RGB median örnekleme ve glyph ölçüm çekirdeği; atlas yoksa TrueType fallback.
- VFS salt-okunur stream arayüzü; loose dosyalar doğrudan stream olarak açılır.

## Sonraki üretim işleri

- Script component'leri için editor tarafında hata/çalışma durumu görünürlüğü.
- Android/iOS host projeleri ve fiziksel cihaz doğrulaması.

Bu dosya, hedef mimari belgelerindeki gelecek vaatleri ile test edilmiş kodun durumunu ayırmak için tutulur.
