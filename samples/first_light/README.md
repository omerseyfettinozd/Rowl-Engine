# First Light — ilk oynanabilir Rowl demosu

3 düğümlük dallanan mini görsel roman: arka plan, karakter, daktilo
diyaloğu ve 2 seçenekli yol ayrımı. Motorun gerçek oyun içeriğiyle
doğrulandığı ilk örnek (`samples/`).

## Oynama

Repo kökünden:

```sh
./build/bin/rowl_player --project samples/first_light
```

## Headless doğrulama

`rowl_native_tests` içindeki `test_demo_first_light`, bu projenin
dosyalarını gerçek pipeline'dan geçirir: mount → story load → step →
render + `SelectChoice` ile iki sona da ulaşır. CI'da her platformda koşar.
