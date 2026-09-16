# Layer Atlas Hükmü (Faz 5 / Dilim 6)

## Hüküm

**Atlas gerekmedi.** Ürün-tavanı konfigürasyon (4 karakter × 4 slot = 16 draw,
paylaşımlı doku önbelleği) steady-state'te 16 ms bütçenin çok altında.
`tests/test_character_layer_benchmarks.cpp` kalıcı guard olarak durur; atlas
yazılmaz.

## Ölçüm (bu makine, offscreen yazılım render, SDL dummy sürücü)

| Konfigürasyon | Ölçülen |
|---|---|
| Soğuk first-frame, 16 draw (doku çözümü dahil) | ~54.9 ms (`texture_load_ms` ~52.9 ms, tek seferlik) |
| Steady 4 draw (sahne-güncelle + render) | ~1.89 ms/frame |
| Steady **16 draw (ürün-tavanı)** | **~2.83 ms/frame** |
| Steady 40 draw (stres, kayıt) | ~6.43 ms/frame |
| Steady 80 draw (stres, kayıt) | ~12.42 ms/frame |

Stres noktaları draw-bound maliyeti ölçer (2 benzersiz doku paylaşılır);
makineye göre değişir, assert edilmez.

## Eşik kuralı (mühürlü)

- Ürün-tavanı **16-draw steady render ≤ 8 ms** (16 ms bütçenin yarısı).
- Test her frame `x`'e jitter katar (identical-frame fast-path cache-busting);
  jittersiz ölçüm 0 ms verir ve geçersizdir.

## Tripwire

- Ürün slot/katman tavanı yükselirse (ör. karakter başına 4 slot üstü ya da
  aynı anda 4 karakter üstü) assert konfigürasyonu güncellenir.
- 16-draw eşiği aşılırsa ya da 80-draw stres noktası bütçeyi zorlarsa **atlas
  dilimi tetiklenir** (doku-atlası / batch birleştirme).
