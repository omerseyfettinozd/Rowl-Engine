# Second Signal — Rowl Engine Golden Project

Bu sürümlü fixture, 1.0 platform ve paket kapılarının ortak Golden Project'idir.
4 düğümlük ayrılan ve tekrar birleşen hikâye; background, character, dialogue,
audio, camera, transition, script, variable ve koşullu choice bileşenlerini
gerçek runtime zincirinden geçirir.

Ses dosyası (`Assets/audio/signal_tone.wav`) depoya kayıtlı, sentetik
0.4 sn'lik yükselen sinüs — harici bağımlılık yok.

## Oynama

Repo kökünden:

```sh
./build/bin/rowl_player --project samples/second_signal
```

## Headless doğrulama

`rowl_native_tests` içindeki `test_demo_second_signal`, projeyi geçici ve
izole bir dizine kopyalar; BGM/render, script ve sinematik bileşenleri, koşullu
dallanma, save/load, süreç yeniden başlatma ve rewind sözleşmesini doğrular.
`golden_project.json` fixture kimliğini, beklenen yetenekleri ve paketlenecek
dosyaların SHA-256 değerlerini dondurur.
