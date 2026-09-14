# Second Signal — Rowl Engine Golden Project

Bu sürümlü fixture, 1.0 platform ve paket kapılarının ortak Golden Project'idir.
4 düğümlük ayrılan ve tekrar birleşen hikâye; background, character, dialogue,
audio, camera, transition, script, variable ve koşullu choice bileşenlerini
gerçek runtime zincirinden geçirir. Ürünleşme-v2 kapsamı ayrıca kalıcı
`content_id` değerleri, EN/TR katalogları, Unicode varlık yolu ve 60 saniyelik
PCM ses fixture'ı taşır. Katalogların runtime seçimi Faz 3'ün kabul kapısıdır;
bu fixture şimdiden veri sözleşmesini ve paket bütünlüğünü dondurur.

Kısa ses dosyası (`Assets/audio/signal_tone.wav`) sentetik 0,4 saniyelik,
uzun ses dosyası (`Assets/audio/long_signal_60s.wav`) sentetik 60 saniyelik
PCM sinüstür; ikisi de depoya kayıtlıdır ve harici çalışma zamanı bağımlılığı
yoktur.

## Oynama

Repo kökünden:

```sh
./build/bin/rowl_player --project samples/second_signal
```

## Headless doğrulama

`rowl_native_tests` içindeki `test_demo_second_signal`, projeyi geçici ve
izole bir dizine kopyalar; BGM/render, script ve sinematik bileşenleri, koşullu
dallanma, save/load, süreç yeniden başlatma ve rewind sözleşmesini doğrular.
`golden_project.json` fixture kimliğini, beklenen yetenekleri, pozitif/negatif
ürün senaryolarını ve paketlenecek dosyaların SHA-256 değerlerini dondurur.
