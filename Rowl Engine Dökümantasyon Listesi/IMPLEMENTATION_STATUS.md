# Uygulama Durumu

Son doğrulama: Linux Debug derlemesi, CTest, editör headless testi ve paket
içinden VFS graph yükleyen yerel standalone smoke testi geçiyor.

## Hazır

- C++20 çekirdek, SDL3 framebuffer/player, P/Invoke editör köprüsü ve graph tabanlı hikâye oynatımı.
- Sürümlü save dosyaları; değişken, sahne arka planı, BGM yolu/volümü/aktiflik ve DSP bilgisi.
- Lua sandbox ve birbirinden izole script component modülleri: her component kendi
  `on_enter`, `on_update`, `on_choice` ve `on_exit` callback'ini taşır; aynı
  callback isimleri başka component'leri ezmez.
- WAV oynatma, PCM üzerinde Telephone/Underwater/Cave DSP, ducking durum sorguları ve ses hata sorgusu.
- MSDF atlas meta verisi, RGB median örnekleme ve glyph ölçüm çekirdeği; atlas yoksa TrueType fallback.
- VFS salt-okunur stream arayüzü; loose dosyalar doğrudan stream olarak açılır.
- Script component çalışma/hata tanıları editör ve C API üzerinden görünür; Lua
  component modülleri birbirinden izole kalır.
- Node girişinde BGM instant/fade/crossfade geçişleri ve SFX'in preview refresh
  sırasında yeniden tetiklenmesini engelleyen olay semantiği uygulanır.
- `tools/package_assets.py` v1 `.rowlpkg` için tek kanonik üreticidir. Desktop
  release yalnızca `Assets/packages/game.rowlpkg` ve boş `mods/` override
  dizinini taşır; aynı göreli mod yolu paket içeriğini geçersiz kılar.
- C API ve player paket içindeki `json/full_story_graph.json` graph'ını VFS
  üzerinden yükler. `--package-smoke-test` tek offscreen frame render eder.
- Story graph dosya/VFS yüklemeleri artık açık başarı sonucu üretir. Geçersiz
  graph aktif hikâyeyi korur; C API/PInvoke son yükleme tanısını sorgulayabilir.
- SDL pencere input'u render katmanından global engine'e ulaşmaz; kendi runtime
  callback'i üzerinden advance/save/load/rewind ve pointer olaylarını taşır.
- Native benchmark JSON şeması ve aynı ortam/fixture için yüzde farkı raporlayan
  karşılaştırıcı hazırdır; ilk sonuçlar baseline olarak saklanır.
- Yaratıcı Hikâye Stüdyosu tasarım sistemi, Project Hub, boş durum, üst çalışma
  akışı ve ana panel yüzeylerinde uygulandı.

## Sonraki üretim işleri

- Linux ve Windows CI release package smoke sonuçlarını ilk uzak çalıştırmada
  doğrula; Windows sonucu gelene kadar platform desteği onaylanmış sayılmaz.
- Aynı fiziksel makinede Release benchmark baseline'ları biriktir; gerçek
  örnek sayısı yeterli olmadan fail eşiği koyma.
- Node kartları ve component inspector için ayrı Yaratıcı Hikâye Stüdyosu
  görsel dilimlerini uygula.
- macOS desktop package kapısı; Android/iOS host projeleri ve fiziksel cihaz
  doğrulaması daha sonraki platform kapsamındadır.

Bu dosya, hedef mimari belgelerindeki gelecek vaatleri ile test edilmiş kodun durumunu ayırmak için tutulur.
