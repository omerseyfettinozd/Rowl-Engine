# İnceleme kanıtları — 4 Ekim 2026

Kaynak commit: `37a5a49119a00f9c52fefdf6962809202ae12968`.
Ana rapor: `../../REVIEW_2026-10-04.md`.

- `summary.json`: sonuç/kapsam özeti.
- `reviewed-files.txt`: seçici içerik incelemesi veya kaynak taraması yapılan 124 dosya; tamamının baştan sona okunduğu anlamına gelmez.
- `ctest.log`, `editor-tests-summary.log`, `package-summary.log`: genel yerel kapılar.
- `ci-current.json`, `ci-nightly.json`: GitHub API'den canlı alınan iş sonuçları; nightly farklı commit üzerinde.
- `native-benchmark.json`: tek makine gözlemi; build/machine `unknown`, floor `report`; sürümler arası baseline değildir.
- `discard-reproduced.log`, `saveas-reproduced.log`: ana ajanın yeniden çalıştırdığı editör karşı örnekleri.
- `lua-root-reproduced.log`: aynı build'in C API çağrısını 9 saniye bekleyip kill eden ana ajan gözlemi.
- `capi-diagnostics.log`: aynı build ile condition mutasyonu ve last-result karşı örnekleri.
- `*-agent-report.md`: alt alan incelemeleri; kapsam ve kanıt sınıfları ana raporda birleştirildi.

`discard-probe` gerçek editor projesini, `saveas-probe` gerçek SaveAs/FileSystem kaynaklarını referans alır. Bu probe'lar geçici proje dizini üretip temizler. Kaynak commit'te repo kökünden:

```bash
MSBuildEnableWorkloadResolver=false dotnet run --project docs/review-evidence/2026-10-04/discard-probe/probe.csproj
MSBuildEnableWorkloadResolver=false dotnet run --project docs/review-evidence/2026-10-04/saveas-probe/probe.csproj
```

Lua freeze probe bilerek dönmeyen script çalıştırır; harici süreç bütçesi ile yürütülür. Doğrudan sınırsız çalıştırılmamalıdır. `lua-freeze-probe.py` aynı commit'in `build/lib/libRowlEngineCore.so` dosyasını kullanır. Ana ajan ölçümünde child 9 saniyede dönmedi ve kill edildi. Log hacmini sınırlamak için child stdout/stderr ana tekrarında `/dev/null`a yönlendirildi.

`state-budget-probe.cpp`, Release engine object'leri üzerinden büyük yasal history'nin 4 MiB save kapısı tarafından reddedildiğini ölçer; eski slotun bozulduğu iddiası değildir. GUI, gerçek GPU görüntüsü ve fiziksel ses cihazı bu kanıtlarda bulunmaz.
