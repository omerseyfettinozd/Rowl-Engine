# KNOWN ISSUES — Rowl Engine (Faz 7 Dilim 1, IS 2/2)

Açık P0/P1 YOK (2026-09-16, tüm CTest yeşil).

| ID | Başlık | Şiddet (P0-P3) | Durum (açık/kabul-edildi) | Etkilenen alan | Kaynak (dosya:satır) | Hedef dilim |
|---|---|---|---|---|---|---|
| KI-01 | Save slot bütünlük checksum'u yok (parse+validasyon yeterli kararı, hash/MAC follow-up adayı) | P3 | kabul-edildi | save slot bütünlüğü | docs/DATA_FORMATS_AND_MIGRATION.md:110-112 | takip (v4 adayı) |
| KI-02 | errno-önek konum tutarsızlığı (enjeksiyon yollarında sonda, open/rename-fail yollarında başta) | P3 | açık | save hata mesajları | engine/src/state/save_durability.cpp:61-63,140-148,162-164,181-184 | takip |
| KI-03 | Windows rename-hata metni yaklaşıklığı (GetLastError strerror'dan çözülüyor, birebir değil) | P3 | kabul-edildi | save hata mesajları (Windows) | docs/DATA_FORMATS_AND_MIGRATION.md:69-73; engine/src/state/save_durability.cpp:89-96 | — |
| KI-04 | uninstall iç-içe boş dizin bırakabilir (tek `rmdir`, üst ebeveynler kalır) | P3 | açık | self-extracting uninstall | tools/export_game.py:499-523 (özellikle :521) | takip |
| KI-05 | receipt CRLF'ye dayanıksız (LF yazılıyor, okumada CR-strip yok; CR'li satırda `rm -f` ıskalar) | P3 | açık | self-extracting uninstall | tools/export_game.py:474-480 (yazım),508,510-519 (okuma) | takip |
| KI-06 | xUnit çevresi `sudo dotnet workload repair` istiyor (makine-çevre, kod dışı) | P2 | açık | editör test çevresi | second-brain daily 2026-09-16.md:392 | takip |
| KI-07 | Rewind geçmişi sınırsız/budamasız (ölçüm D8 stres testinde). Dosya ~60B/adım yavaş büyür (ölçüldü), RAM `previousState` zinciri sınırsız büyür (budama yok). | P3 | kabul-edildi | GameState rewind zinciri | engine/src/state/game_state.cpp:189-202 | — (ölçüm D8 stres testinde) |
| KI-08 | stres-suit ~156sn/1000-iter (thumbnail PNG-encode+base64 her save'de, ~310KB/slot) | P3 | kabul-edildi | test süresi | tests/test_rc_soak_and_data_safety.cpp yeni stres bölümü + eşik <240sn | Faz 7 |
| KI-09 | macOS derleme-kapısı CI-only (`macos-arm64-compile`), cihaz/imza-kanıtı yok; Darwin `#else` HOME-fallback ve GPU-smoke-dışı D2'ye | P2 | açık | macOS derleme kapısı | .github/workflows/ci.yml (`macos-arm64-compile`); docs/PLATFORM_SUPPORT.md (Faz 7 D1 bölümü); engine/src/platform/user_data_directories.cpp:80-83 | Faz 7 |

Kapsam-dışı (bilinçli yazılmadı): flag-drift `--help` (kodda dosya-yazma yok, şüphe düştü), `tail -n +N` kullanımı (tutarlı, şüphe yok).
