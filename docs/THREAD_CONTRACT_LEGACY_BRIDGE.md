# Thread-Contract Legacy Köprüsü (D14)

D14 kapsamı: `c_api.h` threading-contract yorumu (:12-35) + yeni guard TU
(`engine/src/c_api_thread_contract_guard.cpp`) + bu doküman. W8-c ile
prototipler public başlığa taşındı (`c_api.h:852-854`); DN kapısı uygulanır.

## 1. Sözleşme (c_api.h:12-35 özeti)

- Handle başına tek owner thread. İlk Init claim'ler; sonraki yabancı-thread
  çağrıları fail-closed, motora dokunulmaz.
- İki tier (D3/B1d):
  - **sessiz** — sıradan getter/setter'lar ölü-handle varsayılanını döner
    (0 / empty / no-op, ResultCode noktalarında INVALID_HANDLE), kayıt yok.
    Yabancı-thread okumaları da sessiz düşer (hot-path thrash/log seli yok;
    native S3/S7 kilitler).
  - **loud** — ownership-düzeyi çağrılar canlı handle'da WRONG_THREAD (14)
    damgalar (op adıyla), `RowlEngine_GetLastResultCode/Message` (+Utf8)
    ile cross-thread okunur + loglanır. Damgalayanların TAM listesi için
    tek-kaynak: `c_api.h:29-37` Stamping census (37 call-site, grep-kanıtlı;
    op-adı↔call-site birebir DEĞİL — init/pause çift-site, parametrik
    guard'lar; ikinci liste burada tutulmaz, listeler çürür). Gerçekten ölü
    handle'daki çağrılar sessiz no-op kalır (kaydedilecek motor yok);
    INVALID_HANDLE hep "ölü" demektir, asla "yabancı" değil.
- Host'lar bir handle'ın tüm çağrılarını owner thread'e serileştirir
  (editör: OffscreenRuntimeWorker dispatch).

## 2. Legacy köprü haritası (sessiz/loud)

| Yüzey (c_api.h) | Legacy form | Tier | Not |
|---|---|---|---|
| Save/Load slot sarmalayıcıları (1030-1077: `SaveGameSlotResult`, `LoadGameSlotResult`, `Has/DeleteSaveSlot`, `Rewind`, metadata) | `int` 1/0 | **loud** | `claimHandleOrClassify` + `stampWrongThread` (D4 #49). Legacy `int` form sonucu forward'lar. |
| Mixer fade/polyphony/bed (yorum :827, decl'ler :842-859+: `Set/GetFadeCurve`, `Set/GetSfxPoolDepth`, bed'ler, pump) | `void` / `int` | **sessiz** | Yalnız `isLiveHandle`; yabancı da sessiz düşer. Strict isteyen host → §3 checked varyantlar. |
| Chapter/prefetch penceresi (yorum :1335, decl'ler :1365-1384+: `LoadChapterIndexJson`, `AppendChapterFileJson`, `Load/UnloadChapter`, `GetLoadedChaptersJson`, `IsChapterBoundaryNode`, …) | karışık | **loud** | Aux-map guard'ları: erase yalnız gerçek ölümde (`Dead`), yabancı damgalanır (D3 #150/#157). `int`-taşıyıcılarda kod taşınamaz → 0 + damga. |
| Provenance (`GetAssetProvenanceJson`) | ResultCode | **loud** | Yabancı `classifyHandle` → `stampWrongThread(handle, "get_asset_provenance")` + WRONG_THREAD (`c_api_provenance.cpp:71-73`); başlık damga-listesinde (`c_api.h:32`). Strict isteyen host → checked varyant adayı (henüz yok). |
| Additive temel (`c_api_contract.cpp:1-8`) | — | — | Versiyon/capability + caller-buffer; yeni ResultCode'lu API'lerin temeli. Legacy girişler alt-sistem TU'larında uyumluluk sarmalayıcısı olarak kalır. |

Kural: **sembol silmek YOK** (additive ABI). Yeni davranış yeni sembole
(`...Checked`), eski sembole dokunulmaz.

## 3. D14 checked varyantlar (yeni TU)

Prototipler public başlıkta (`c_api.h:852-854`, additive ABI — eski
sembollere dokunulmaz):

```c
RowlEngine_ResultCode RowlEngine_SetFadeCurveChecked(RowlEngineHandle handle, int curve);
RowlEngine_ResultCode RowlEngine_GetFadeCurveChecked(RowlEngineHandle handle, int* outValue);
```

Davranış (`set_fade_curve` / `get_fade_curve` op damgasıyla; native kapsam:
motor-tier kararı, C# kapsam: strict host ResultCode okur, legacy form
sessiz kalır):

- Canlı + owner → uygula/oku, `ROWL_RESULT_OK`. Legacy form aynı değeri görür.
- Canlı + yabancı → `ROWL_RESULT_WRONG_THREAD` (14) + damga; motora dokunulmaz.
- Ölü (null/bogus/destroyed) → sessiz `ROWL_RESULT_INVALID_HANDLE` (1); kayıt yok.
- Geçersiz curve (0/1 dışı) / null `outValue` → `ROWL_RESULT_INVALID_ARGUMENT`
  (2) + damga (legacy `void` form geçersiz eğriyi sessiz yoksayar; checked
  strict reddeder — fark belgelenir, legacy korunur).
- Ses alt-sistemi yoksa → `ROWL_RESULT_UNKNOWN_ERROR` (fail-closed).

RED kilidi: `tests/test_thread_contract_red_probe.cpp`
(`ctest -R thread_contract_red_probe`).

## 4. Kapı etkisi

- N (native): `build-d14` + CTest (en az `thread_contract_red_probe`; tam
  `rowl_native_tests` yeşili hedeflenir).
- D (doküman): bu dosya + malzeme paritesi (1030-1077, 827/842-859,
  1335/1365-1384, `c_api_contract.cpp:1-8`).
- A (ABI): `tools/check_abi_additive.py` — yalnızca ekleme (iki yeni sembol),
  çıkarma yok.
- DN (dotnet): **uygulanır** — başlık değişti (`c_api.h:852-854`),
  bayraklı Editor+Tests derlemesi 0-hata.
- S / TSan / X: yok.
