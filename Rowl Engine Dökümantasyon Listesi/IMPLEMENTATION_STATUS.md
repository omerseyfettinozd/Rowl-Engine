# Uygulama Durumu

Son doğrulama (2026-09-12): Linux Debug derlemesi, 10/10 CTest, editör
headless testi ve paket içinden VFS graph yükleyen yerel standalone smoke
testi geçiyor.

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
- Story graph veri modeli ile JSON parse/semantic doğrulaması `Engine` sınıfından
  ayrılmış saf `StoryGraphParser` modülündedir. Parser canlı runtime state'ine
  dokunmaz; yalnız tam doğrulanmış belge `Engine` tarafından transactional olarak
  uygulanır ve syntax/validation hataları ayrı test edilir.
- SDL pencere input'u render katmanından global engine'e ulaşmaz; kendi runtime
  callback'i üzerinden advance/save/load/rewind ve pointer olaylarını taşır.
- C API artık aynı süreçte birden çok canlı handle destekler. Her runtime kendi
  VFS örneğini taşır; Window/Audio varsayılanları da global VFS yerine bağlı
  runtime VFS'sini kullanır. SDL video ve audio alt sistemleri process-safe
  lease yönetimiyle son kullanıcı kapanana kadar açık tutulur.
- Görünür/embedded SDL pencereleri process-genel event kuyruğundan pencere
  kimliğiyle ayrıştırılır; process quit tüm kayıtlı runtime'lara yayılır. Bu
  pencereler aynı host UI/event thread üzerinde başlatılıp step edilmelidir.
- Native benchmark JSON şeması ve aynı ortam/fixture için yüzde farkı raporlayan
  karşılaştırıcı hazırdır; ilk sonuçlar baseline olarak saklanır.
- Editor headless benchmark JSON'u graph drag, seçim, component değişimi,
  preview teslimi ve Save As maliyetlerini kimlikli fixture ile raporlar;
  scriptsiz değişmeyen preview payload'ları native render'a yeniden gönderilmez.
- Yaratıcı Hikâye Stüdyosu tasarım sistemi, Project Hub, boş durum, üst çalışma
  akışı ve ana panel yüzeylerinde uygulandı.
- Milestone 20--26 editör ve runtime zinciri yeşil: çoklu seçim/batch işlemler,
  diagnostic toast'lar, transform gizmo, DSP/VU telemetry, parallax, typewriter
  voice blip'leri ve sinematik kamera shake/screen FX, editor serileştirme,
  P/Invoke ve native offscreen testleriyle kapsanıyor.
- Sinematik C API sınırı non-finite shake yönü, flash intensity, tint opacity ve
  vignette radius girdilerini no-op olarak reddeder; aktif geçerli efekt/profil
  korunur ve `std::clamp` üzerinden NaN'ın render state'e ulaşması engellenir.
- Mobil export yardımcısı native runtime derlemesi ile gerçek APK/IPA üretimini
  ayırır. Alt süreç veya beklenen native artefakt başarısızsa hata kodunu taşır;
  henüz var olmayan APK/AAB/IPA çıktısını başarılı göstermez.
- GitHub Actions `34689084748` üzerinde Linux ve Windows build/test kapıları ile
  iki platformun standalone package/VFS smoke adımları `0f0b092` için geçti.
- `samples/second_signal`, checksum'lı `rowl-golden-project-v1` fixture'ıdır;
  ayrılan/birleşen graph, görsel/ses/sinematik/script component'leri ile
  save/load/restart/rewind sözleşmesini gerçek C API ve paketli VFS yolunda sınar.
- Save/load ve rewind sunumu yeniden kurarken node-entry yan etkilerini tekrar
  çalıştırmaz. Variable `add`, script `on_enter`, transition/shake ve dialogue
  history kaydı restore sırasında ikinci kez uygulanmaz; kaydedilmiş state korunur.
- Duvar-saati transition ölçümü varsayılan olarak raporlanır. Yalnız aynı
  makine/build/fixture için kontrol edilen performans hostu açıkça
  `ROWL_PERF_FLOOR=enforced` verdiğinde 30 FPS eşiği test kapısı olur.

## Sonraki üretim işleri

- Windows CI build/test/package kapısı yeşildir; gerçek Windows GUI, input,
  audio, Unicode yol ve save dizini doğrulanmadan tam platform desteği ilan etme.
- Editor/native benchmark setlerini aynı fixture ile biriktir; yeterli örnek
  oluşmadan otomatik performans-fail eşiği koyma. Native preview render ana
  editor maliyetidir; geniş render değişikliği önce ayrı profil kanıtı ister.
- Golden Project'i GUI erişimli oturumda Project Hub'dan paketlemeye kadar üret
  ve node/inspector görsel smoke yap. macOS/Xcode host'unda desktop package,
  Android'de gerçek APK ve iOS'ta development-signed app yürüyen iskeletlerini
  büyük mimari refactorlardan önce doğrula.

Bu dosya, hedef mimari belgelerindeki gelecek vaatleri ile test edilmiş kodun durumunu ayırmak için tutulur.
