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
    ile cross-thread okunur + loglanır. Damgalayan çağrılar: Shutdown,
    Destroy, Step, Init (canlı owner'ın claim reddi), prefetch/character aux
    guard'ları, visible-step dispatch gate'i. Gerçekten ölü handle'daki
    çağrılar sessiz no-op kalır (kaydedilecek motor yok); INVALID_HANDLE
    hep "ölü" demektir, asla "yabancı" değil.
- Host'lar bir handle'ın tüm çağrılarını owner thread'e serileştirir
  (editör: OffscreenRuntimeWorker dispatch).

## 2. Legacy köprü haritası (sessiz/loud)

| Yüzey (c_api.h) | Legacy form | Tier | Not |
|---|---|---|---|
| Save/Load slot sarmalayıcıları (1008-1023: `SaveGameSlot`, `LoadGameSlot` → `...Result` forward) | `int` 1/0 | **loud** | `Save/LoadGameSlotResult`, `Has/Delete/Rewind`, metadata: `claimHandleOrClassify` + `stampWrongThread` (D4 #49). Legacy `int` form sonucu forward'lar. |
| Mixer fade/polyphony/bed (816-822 comment + devamı: `Set/GetFadeCurve`, `Set/GetSfxPoolDepth`, …) | `void` / `int` | **sessiz** | Yalnız `isLiveHandle`; yabancı da sessiz düşer. Strict isteyen host → §3 checked varyantlar. |
| Chapter/prefetch penceresi (~1323-1358: `LoadChapterIndexJson`, `AppendChapterFileJson`, `Load/UnloadChapter`, `GetLoadedChaptersJson`, `IsChapterBoundaryNode`, …) | karışık | **loud** | Aux-map guard'ları: erase yalnız gerçek ölümde (`Dead`), yabancı damgalanır (D3 #150/#157). `int`-taşıyıcılarda kod taşınamaz → 0 + damga. |
| Provenance (`GetAssetProvenanceJson`) | ResultCode | sessiz-yabancı | Yabancı `toEngineChecked` null → INVALID_HANDLE (damgasız). Strict isteyen host → checked varyant adayı (henüz yok). |
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
- D (doküman): bu dosya + malzeme paritesi (1008-1023, 816-822, ~1323-1358,
  `c_api_contract.cpp:1-8`).
- A (ABI): `tools/check_abi_additive.py` — yalnızca ekleme (iki yeni sembol),
  çıkarma yok.
- DN (dotnet): **uygulanır** — başlık değişti (`c_api.h:852-854`),
  bayraklı Editor+Tests derlemesi 0-hata.
- S / TSan / X: yok.
