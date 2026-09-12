# Second Signal — ses + Lua'lı ikinci Rowl demosu

4 düğümlük dallanan hikâye: BGM tonu (`audio` bileşeni), hikâye-güdümlü
Lua değişkenleri (`variable` bileşeni: `set`/`add`) ve koşullu seçenek
(`condition` Lua ifadesiyle kapı eşiği). `samples/` altındaki ikinci oyun.

Ses dosyası (`Assets/audio/signal_tone.wav`) depoya kayıtlı, sentetik
0.4 sn'lik yükselen sinüs — harici bağımlılık yok.

## Oynama

Repo kökünden:

```sh
./build/bin/rowl_player --project samples/second_signal
```

## Headless doğrulama

`rowl_native_tests` içindeki `test_demo_second_signal`, bu projenin
dosyalarını gerçek pipeline'dan geçirir: BGM çalıyor mu, hikâye değişkenleri
Lua'ya yazıldı mı, koşul doğru mu değerlendiriliyor (`EvaluateCondition`),
cevap → kod yolu iki düğümde de doğru mu.
