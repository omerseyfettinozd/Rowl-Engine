# Rowl Engine — Oyuncu Kılavuzu (Player Guide)

Kısa, komut-odaklı kurulum + oynama + doğrulama + kaldırma kılavuzu.
Bu dosya **çalıştırılabilir dokümandır**: `tests/test_player_guide.py`
aşağıdaki her `sh` bloğunu sırayla GERÇEKTEN koşar. Yeni bir `sh` bloğu
eklersen otomatik koşar; bir komutu değiştirirsen test o komutu koşar —
kılavuzla testin arası açılamaz.

> Çalıştırma dizini: bu deponun kökü (`Rowl Engine/`).
> `GUIDE_WORK` her blokta geçen çalışma alanıdır; test geçici bir dizine
> kurar, sen elle denerken kendin ata:
>
> ```text
> GUIDE_WORK=/tmp/rowl-guide; mkdir -p "$GUIDE_WORK"
> ```

## 1. Gereksinimler (saf-sh kurulum araçları)

Kurulum anında yalnızca POSIX kabuk araçları gerekir — python3, root
yetkisi gerekmez:

```sh
# guide-probe: expect-ok
command -v sh sha256sum unzip base64 tail python3
```

(`python3` yalnızca 2–3. adımdaki paket ÜRETİMİ içindir; oyuncunun
kurulumu saf `sh` ile olur.)

## 2. İndir / üret

Sürüm paketini üret (taşınabilir zip), sonra onu `.sh` kurulum dosyasına sar:

```sh
# guide-probe: expect-ok
python3 tools/export_game.py portable-zip --output "$GUIDE_WORK/rowl-portable.zip"
```

```sh
# guide-probe: expect-ok
python3 tools/export_game.py self-extracting --input "$GUIDE_WORK/rowl-portable.zip" --output "$GUIDE_WORK/install-rowl-guide.sh" --version guide
```

Kur (`--prefix` verilmezse `./rowl-game` kurulur):

```sh
# guide-probe: expect-ok
sh "$GUIDE_WORK/install-rowl-guide.sh" --prefix "$GUIDE_WORK/prefix"
```

## 3. Oyna

Kurulu oyuncu `prefix/rowl_player` dosyasıdır. Gerçek oyun penceresi açar:

```text
"$GUIDE_WORK/prefix/rowl_player" --project "$GUIDE_WORK/prefix"
```

Bayraklar (`rowl_player --help` çıktısıyla birebir — uydurma yok):

| Bayrak | Anlam |
|---|---|
| `-h, --help` | Yardımı göster, çık |
| `-v, --version` | Sürümü göster, çık |
| `-p, --project <dir>` | Proje kökü (varsayılan: `.`) |
| `-s, --story <file>` | Hikâye grafiği JSON yolu |
| `-w, --width <px>` / `--height <px>` | Pencere boyutu (varsayılan: 1920x1080) |
| `-t, --title <ad>` | Pencere başlığı |
| `--slot <N>` | Hızlı-kayıt slotu 0-9 (F5/F9, varsayılan: 0) |
| `--no-vsync` | Dikey senkronu kapat |
| `--gpu-smoke-test` | Tek kare çiz, çık (CI) |
| `--package-smoke-test` | Paketli VFS grafiğinden tek kare çiz, çık |

Kontroller: Space/Enter/Click ilerler · F5 hızlı-kayıt · F9 hızlı-yükleme ·
0-9 slot seçer · Backspace/Z 1 adım geri · Escape/P duraklatma menüsü.

### Smoke sınırı (neden pencere açılmıyor?)

CI'da pencere açılmasın diye smoke olarak SADECE `--help` / `--version`
(çıkış 0) ve bir argüman-hatası (çıkış ≠ 0) koşar. Gerçek kare çizimi
(`--gpu-smoke-test` / `--package-smoke-test`) display/GPU ister; kapısı
zaten `rowl_player_gpu_msdf_smoke` ve `rowl_demo_*_packaged` CTest'lerindedir.
Burada tekrarlanmaz.

```sh
# guide-probe: expect-ok
"$GUIDE_WORK/prefix/rowl_player" --help
```

```sh
# guide-probe: expect-ok
"$GUIDE_WORK/prefix/rowl_player" --version
```

Hatalı bayrak değeri 0-dışı çıkış verir (oyuncu argümanları doğrular):

```sh
# guide-probe: expect-fail
"$GUIDE_WORK/prefix/rowl_player" --slot 99
```

## 4. Doğrula

Kurulu ağacın bütünlüğü (`SHA256SUMS` kurulumda zaten denetlendi; burada
tekrar kanıtlanır) + demo paketinin iç doğrulaması:

```sh
# guide-probe: expect-ok
cd "$GUIDE_WORK/prefix" && sha256sum -c SHA256SUMS
```

```sh
# guide-probe: expect-ok
python3 tools/package_assets.py verify "$GUIDE_WORK/prefix/game.rowlpkg"
```

### Bir şey ters giderse

- `sha256sum -c` bir dosyada BAŞARISIZ derse: o dosyayı sil, zip'i
  yeniden aç (veya kurulumcuyu yeniden indir) — bozuk dosyayla oynama.
- Kurulumcu "hash mismatch" / "cannot unpack" deyip çıkarsa hedef dizine
  hiçbir şey yazılmamıştır; aynı kural bozuk `.sh` için de geçerlidir.
- Disk dolarsa kurulum yarıda `cannot install ...` hatasıyla durur; yer
  açıp aynı komutu tekrar çalıştır (kaldığın yerden değil, baştan kurar).
- Bu kılavuzdaki kurulumcu Linux-only POSIX `sh`'tir; Windows kurulumcusu
  ayrı bir dilimde gelecektir.

## 5. Crash-log: nerede, ne yapılır?

Oyuncu çökerse `crash-logs/` dizinine (oyuncunun ÇALIŞMA dizini altında)
`crash-<pid>-<seq>.log` yazar. Her çalışta dizin oluşur — `--help` bile:

```sh
# guide-probe: expect-ok
mkdir -p "$GUIDE_WORK/smoke-cwd"
cd "$GUIDE_WORK/smoke-cwd"
"$GUIDE_WORK/prefix/rowl_player" --help >/dev/null
test -d crash-logs
```

Oyuncu olarak yapacağın: çökmenin yanına `crash-logs/crash-*.log`
dosyasının İÇERİĞİNİ ekle (dosyayı silme, raporuna yapıştır). Ayrıntı:
`docs/CRASH_LOG_CONTRACT.md`.

## 6. Kaldır

Yalnızca kurulan dosyalar silinir (kullanıcı dosyalarına dokunulmaz);
boşalan prefix dizini de kaldırılır:

```sh
# guide-probe: expect-ok
sh "$GUIDE_WORK/install-rowl-guide.sh" uninstall --prefix "$GUIDE_WORK/prefix"
test ! -e "$GUIDE_WORK/prefix"
```

Üçüncü taraf lisans envanteri için bkz. `THIRD_PARTY_LICENSES.md` (repo kökü).
