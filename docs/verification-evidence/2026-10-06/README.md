# Güncel doğrulama — 6 Ekim 2026

Kaynak: `2a1648a9ee7dbd56e4505464ff4b3a7076b434d8`, cc79 worktree.
[Değerlendirme ve uygulama sırası](../../VERIFIED_ROADMAP_2026-10-06.md).

| Kapı | Sonuç | Kanıt |
| --- | --- | --- |
| Temiz Linux x86_64 Release | 218 adım geçti | configure.log, build.log |
| Native/tool CTest | 69/69, 100,75 sn | ctest.log |
| Editor scale fixture | 1/1 ayrıca geçti | scale.log |
| Managed/headless | 510/510 | editor-tests.log |
| İlk managed koşu | 509/510, native artifact henüz yoktu | editor-tests-before-native.log |
| Web prototip durum testleri | 27/27 | prototype.log |
| K2 discard | Vazgeçilen metin diske yazıldı | discard.log |
| K3/E2 SaveAs | Başarısız kayda rağmen başarı; SourceAssets yok | saveas.log |
| K1 Lua | 9,00 sn dönmedi; subprocess timeout child'ı öldürüp topladı | lua.log |
| R3/R4 condition/last-result | Mutasyon ve sticky hata tekrarlandı | capi.log |
| R1 state bütçesi | 5.361.414 byte, save=0/decode=0 | state.log |
| O5/O8 shape/font | DejaVu ligatürleri; bundled Arabic glyph 0 | shape.log |
| T1 verifier | Boş player/runtime isimli klasör kabul edildi | verifier.log, verifier-probe.py |

Envanter 71 CTest. `-E rowl_editor` managed kapısının yanında scale fixture'ı da
dışladı; scale ayrıca çalıştırıldı. Böylece 70 native/araç kapısı ve ayrı
510 managed testin kanıtı var. Native performans floor'ları **report**;
mutlak performans kabulü değil. GPU smoke host envanterinde yok.
CTest içindeki packaged demo kapıları geçti; offscreen paket/frame sonucu
gerçek görünür GPU, input veya ses cihazı kabulü değildir. Remote CI,
sanitizer, Windows/macOS/Steam Deck ve mobil cihaz bu tur çalıştırılmadı.

Repo kökünden ana tekrar komutları:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  '-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/home/chaple/Belgeler/Rowl Engine/build/_deps/nlohmann_json-src'
cmake --build build -j 6
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ROWL_PERF_FLOOR=report ROWL_CAMERA_FPS_FLOOR=report \
  ctest --test-dir build -E '^rowl_editor_headless_tests$' --output-on-failure
MSBuildEnableWorkloadResolver=false SDL_AUDIODRIVER=dummy \
  dotnet test editor/Tests/RowlEngine.Editor.Tests.csproj --configuration Debug
MSBuildEnableWorkloadResolver=false dotnet run --project docs/review-evidence/2026-10-04/discard-probe/probe.csproj
MSBuildEnableWorkloadResolver=false dotnet run --project docs/review-evidence/2026-10-04/saveas-probe/probe.csproj
python3 docs/review-evidence/2026-10-04/capi-diagnostics-probe.py
python3 docs/verification-evidence/2026-10-06/verifier-probe.py
node --test prototypes/editor-workspace/*.test.mjs
```

Lua freeze probe doğrudan sınırsız çalıştırılmaz. Bu tur
`subprocess.run([...], timeout=9, stdout=DEVNULL, stderr=DEVNULL)` kullanıldı;
TimeoutExpired kaydedildi. Log çıktısı bilerek kapatıldı; log hacmi ölçülmedi.

State probe orijinal `state-budget-probe.cpp` ile yeni
`build/engine/CMakeFiles/rowl_engine_objects.dir/**/*.o` nesnelerine ve
zstd/vorbisfile/SDL3/lua5.5/harfbuzz/freetype/fribidi/unibreak bağımlılıklarına
bağlandı. Aynı 500×2000 + dört halka fixture'ı kullanıldı.
Shape probe public `RowlEngine_ShapeMarkup` caller-buffer size-query/fill
sözleşmesini kullandı; font yolları shape.log'da. DejaVu sistem fontudur,
pakette dağıtılan kapsam olarak sayılmadı.

Bu loglar tarihli kanıttır; sonraki commit'in test edilmiş olduğu anlamına gelmez.
Karşı örnek script'lerinin exit=0 vermesi kusurun kapandığı anlamına gelmez;
probe sonuç alanları kusurun halen bulunduğunu gösterir.
