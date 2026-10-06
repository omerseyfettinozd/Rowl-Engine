# A02/A03 — kayıt kararları ve hata taşıma

6 Ekim 2026. Başlangıç kaynak sürümü: `2a1648a`.

## Değişiklik

“Kaydetmeden çık” yalnız VM dirty flag'ini temizliyordu. Recovery marker ve
pending save kaldığı için Dispose vazgeçilen düzenlemeyi yazıyordu. Discard
artık debounce'ı durdurur, kayıt commit kilidinde bekleyen sequence'ları
geçersiz kılar ve recovery marker'ı temizler. Marker temizlenemezse kapanış
kabul edilmez. Dispose queued background işi geçersiz kılar; background
completion kapatılmış VM'nin durumunu güncellemez.

Başlamış commit, discard kabulünden önce aynı kilitte tamamlanır. Daha önce
tamamlanan autosave geri alınmaz; discard henüz kaydedilmemiş değişiklikleri
bırakır. Kilit disk commit/recovery işiyle sınırlıdır, snapshot capture UI
tarafında kalır. Çok yavaş disk üzerinde discard aktif yazıyı bekleyebilir.

Unsaved resolution, SaveAs ve iki build coordinator yolu `Func<bool>` kayıt
sonucunu tüketir. Kayıt başarısızlığı SaveAs hedefini oluşturmadan/kopyalamadan
ve build conversion/validation/staging aşamalarından önce işlemi durdurur.
Mevcut hedefler korunur; build `IoFailure / save_project` tanısı verir.
Senkron VM build yolu da sonuç üreten `SaveProjectNow` kullanır.

C ABI, native kaynaklar ve UI tasarımı değişmedi. SourceAssets dahil tam staged
SaveAs A04; immutable build snapshot ve tam linter/UI dispatch A05 kapsamındadır.
Bu teslim onları tamamlanmış saymaz.

## Doğrulama

- Managed/headless süit **519/519** geçti: mevcut 510 + dokuz yeni davranış
  vakası. `EditorSaveDecisionTests` disk baytlarını ve gerçek IO hata sonuçlarını
  kontrol eder.
- Autosave açık/kapalı discard+Dispose; queued async save; cancel; başarılı
  save; canonical file yerine directory ile gerçek save IO failure;
  recovery marker yerine directory ile gerçek cleanup failure; SaveAs ve
  iki build yolunun hata duruşu ve mevcut hedef koruması kapsandı.
- İlk sekiz regresyon vakasında discard ve save-result kusurları geçici olarak
  geri getirildi: **6 başarısız / 2 başarılı**. Mutasyon kaynakları finally ile
  geri yüklendi; ek cleanup testi sonrası nihai 519 test geçti.
- `git diff --check` temiz. Native/ABI değiştirilmediği için bu dilimde yeni
  native tam süit/sanitizer koşusu yapılmadı. Önceki denetimdeki native/package
  sonuçları bu dilimin yeni native kanıtı olarak sayılmaz.
- Headless süit export/package integration yolunu kapsar; gerçek pencere,
  GPU/input/audio, Windows/macOS cihaz kabulü yapılmadı.

Yerel test çıktıları: `verification-evidence/2026-10-06-save-decisions/`.
Tekrar:

```bash
MSBuildEnableWorkloadResolver=false dotnet test editor/Tests/RowlEngine.Editor.Tests.csproj \
  --configuration Debug --logger 'console;verbosity=minimal'
```

4 Ekim probe kaynakları o tarihli API sözleşmesinin arşividir; yeni
`EditorSaveDecisionTests` güncel üretim yollarının regresyon kapısıdır.

## Devam sınırı

İlk paket A02/A03 kapandı; sonraki paket A04 SourceAssets/staged SaveAs.
**UI tasarımı aktarımı aşamasına geldiğinde durulacak. Kullanıcı tasarım
çalışmalarını tamamlayıp açık onay vermeden UI aktarımı başlamayacak.**
Çekirdek kapısının tamamlanması UI aktarımı için otomatik onay değildir.
