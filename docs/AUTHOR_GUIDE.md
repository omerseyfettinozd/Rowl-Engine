# Rowl Engine — Yazar Kılavuzu (Author Guide)

Kısa, komut-odaklı ham-kaynaktan yayınlanmış kuruluma uzanan yazar hattı:
örnek üret → dönüştür (`rowl_oggenc` / `rowl_webp2png`) → provenance
doğrula → paketle → ham-kaynak denetimi → portable-zip → self-extracting
kurulumcu → kur → doğrula → kaldır.
Bu dosya **çalıştırılabilir dokümandır**: `tests/test_author_guide.py`
aşağıdaki her `sh` bloğunu sırayla GERÇEKTEN koşar. Yeni bir `sh` bloğu
eklersen otomatik koşar; bir komutu değiştirirsen test o komutu koşar —
kılavuzla testin arası açılamaz.

> Çalıştırma dizini: bu deponun kökü (`Rowl Engine/`).
> `AUTHOR_WORK` her blokta geçen çalışma alanıdır; test geçici bir dizine
> kurar, sen elle denerken kendin ata:
>
> ```text
> AUTHOR_WORK=/tmp/rowl-author; mkdir -p "$AUTHOR_WORK"
> ```
>
> `text` fence'ler koşmaz — yukarıdaki gibi el-ayarları ve salt-okunur
> başvuru alıntıları (`Usage:` çıktıları gibi) `text` fence'tedir, çünkü
> test yalnızca `sh` fence'leri çalıştırır; koşmayan alıntı bir `sh`
> bloğunda dursa ya yanlışlıkla çalışır ya da ilk-satır prob etiketi
> yüzünden testi kızartır.

Dönüştürücü ikilileri `build/bin/` altındadır
(`cmake --build build` ile üretilir); yol, editördeki
`MediaConverterService` adlarıyla ezilebilir:
`ROWL_OGGENC_PATH` / `ROWL_WEBP2PNG_PATH`
(`editor/Services/MediaConverterService.cs:259-261`).
Bloklar bu değişkenleri okur, yoksa `build/bin/` varsayımına düşer.

## 1. Gereksinimler

Saf dosya işi — display/GPU gerekmez; yalnızca CPU + dosya araçları:

```sh
# guide-probe: expect-ok
command -v sh sha256sum unzip base64 tail python3 ffmpeg
OGGENC="${ROWL_OGGENC_PATH:-build/bin/rowl_oggenc}"
WEBP2PNG="${ROWL_WEBP2PNG_PATH:-build/bin/rowl_webp2png}"
test -x "$OGGENC" && test -x "$WEBP2PNG"
```

İkili başvurusu (değişmez sözleşme — `tools/rowl_oggenc.c:40`,
`tools/rowl_webp2png.c:31`):

```text
rowl_oggenc [--rate HZ] [--channels N] -o OUT.ogg [--sidecar FILE] [IN.pcm|-]  (varsayılan 44100Hz stereo)
rowl_webp2png -o OUT.png [--sidecar FILE] IN.webp
```

## 2. Örnek kaynak üretimi

Diskte hazır fixture yoktur; kaynaklar her koşuda deterministik üretilir.
Ses: `python3 -c` ile 1 saniyelik 440Hz stereo s16le PCM (her bayt
matematikten gelir — aynı komut her makinede aynı dosyayı yazar):

```sh
# guide-probe: expect-ok
mkdir -p "$AUTHOR_WORK/src" "$AUTHOR_WORK/assets"
python3 -c "import math,struct,os; w=os.environ['AUTHOR_WORK']; n=44100; b=bytearray(); [b.extend(struct.pack('<hh', int(0.5*math.sin(2*math.pi*440*i/44100)*32767), int(0.5*math.sin(2*math.pi*440*i/44100)*32767))) for i in range(n)]; open(w+'/src/tone.pcm','wb').write(bytes(b))"
test -s "$AUTHOR_WORK/src/tone.pcm"
sha256sum "$AUTHOR_WORK/src/tone.pcm"
```

MP3 ara basamaktır: PCM'den sentetik MP3 üretilir, sonra **sabit**
decode bayraklarıyla (`-ar 44100 -ac 2 -sample_fmt s16`) PCM'e dönülür.
Decode determinizmi bu üç bayrağın sabitlenmesine bağlıdır; MP3'ün kendi
baytları için bit-birebirlik iddia edilmez:

```sh
# guide-probe: expect-ok
ffmpeg -nostdin -hide_banner -loglevel error -y -f s16le -ar 44100 -ac 2 -i "$AUTHOR_WORK/src/tone.pcm" "$AUTHOR_WORK/src/tone.mp3"
ffmpeg -nostdin -hide_banner -loglevel error -y -i "$AUTHOR_WORK/src/tone.mp3" -ar 44100 -ac 2 -sample_fmt s16 -f s16le "$AUTHOR_WORK/src/decoded.pcm"
test -s "$AUTHOR_WORK/src/decoded.pcm"
```

Görüntü: python'da webp encoder yoktur; bu yüzden kılavuz 16x16 kayıpsız
WebP'yi base64-gömülü taşır (bu blok aynı zamanda gömülü baytların
çözüldüğünü kanıtlar). Gömülü baytlar şu komutla üretilmiştir, denetim
için saklanır — kılavuz her koşuda gömülü kopyayı kullanır:
`ffmpeg -f lavfi -i "testsrc2=size=16x16:rate=1:duration=1" -frames:v 1 -c:v libwebp -lossless 1 pic.webp`

```sh
# guide-probe: expect-ok
echo 'UklGRjYCAABXRUJQVlA4TCkCAAAvD8ADAGfkKJIk1arHzM/BO//bd/C9fLeshHnAhiPbdtNGsqyUmWkDnfJyOuxCu5CejJlRthtJtlVl3uXh7p4RswsPWT0Rmbvz5Mv8BxB3RCAKJwUQOBosoD8ITAABBgg0AgIOHBExIOgI5SD22iISI21vYjFyP2VMhsIGvwJ5BkqkWjMS2T4GaGGTYNsPVzCArKAKvC9ey5TLqkhGJfc7HGuAEiqgSqgFnQgPwjgCRiAH/YP+QwUwATQILEQgG9dRqBt5Wa/OevhLRrKImS2iLUiTnD+V0BaIVrBFZFUUGqFHrtFp3OrYbFXu2oI3RfwTlQU/7Ki8wB/bZmus3Y8kFwE4SIEyAkkxyAhbzU1GKBAHgaTEeUpciYBv4I+4dQITSZuH7+rLHMM4oJRiKsMpor2II5yNQDlGwkGkRSAAB3K058j0pfiPDL+ZbbdSBUer5djuq/wXBkuD69IZHn4LbD4Tt0vEAEGSZNPWs23btm0b38b7tm1u2v8vYV5E/9W2bcPQU+oh3O/hxBGQNBtfkFweXwAAwscHeDLdvya4/FYVAGqNJtTblbFxrqDqBQCPyz10ljojo//i8wdC4ZjNYS8Wur1P/mBmeW5+gI3ms6sbt3flCHlhmk5ZHBBZS2ui97dn6sS+ToXBzxydTil6KAubcXKhVVvxh1fDa3n/m0v9+grp+MO5+OZv/ypoezsaJfq3lbRkc8vw89GnlEy6fXYwS8DlmJO7AA==' | base64 -d > "$AUTHOR_WORK/src/pic.webp"
test -s "$AUTHOR_WORK/src/pic.webp"
```

## 3. OGG dönüşümü (+sidecar)

Decode-PCM girer, deterministik OGG + provenance sidecarı çıkar
(sabit `-q 4`, sabit vendor, girdiden türeyen seri):

```sh
# guide-probe: expect-ok
OGGENC="${ROWL_OGGENC_PATH:-build/bin/rowl_oggenc}"
"$OGGENC" --rate 44100 --channels 2 -o "$AUTHOR_WORK/assets/tone.ogg" --sidecar "$AUTHOR_WORK/assets/tone.ogg.rowlconv.json" "$AUTHOR_WORK/src/decoded.pcm"
test -s "$AUTHOR_WORK/assets/tone.ogg"
```

## 4. PNG dönüşümü (+sidecar)

Sabit libpng yazıcıyla (seviye 6; tIME/tEXt/pHYs/iCCP asla) deterministik
PNG + sidecar:

```sh
# guide-probe: expect-ok
WEBP2PNG="${ROWL_WEBP2PNG_PATH:-build/bin/rowl_webp2png}"
"$WEBP2PNG" -o "$AUTHOR_WORK/assets/pic.png" --sidecar "$AUTHOR_WORK/assets/pic.png.rowlconv.json" "$AUTHOR_WORK/src/pic.webp"
test -s "$AUTHOR_WORK/assets/pic.png"
```

## 5. Provenance doğrulama

Sidecar'lar gözle okunur; alanlar makineyle doğrulanır. OGG sidecar'ının
`source_sha256` değeri **decode-PCM'in** hash'idir (ham MP3'ün değil).
Aynı girdinin ikinci koşusu aynı hash'i vermelidir — determinizm probu:

```sh
# guide-probe: expect-ok
cat "$AUTHOR_WORK/assets/tone.ogg.rowlconv.json"
cat "$AUTHOR_WORK/assets/pic.png.rowlconv.json"
python3 - <<'PYEOF'
import hashlib, json, os
w = os.environ["AUTHOR_WORK"]
def sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()
ogg = json.load(open(w + "/assets/tone.ogg.rowlconv.json"))
assert ogg["converter_name"] == "rowl_oggenc", ogg
assert ogg["converter_version"] == "1.0.0", ogg
assert ogg["settings"]["quality_q"] == 4, ogg
assert ogg["settings"]["sample_rate_hz"] == 44100, ogg
assert ogg["settings"]["channels"] == 2, ogg
assert ogg["source_sha256"] == sha(w + "/src/decoded.pcm"), "ogg source is not the decode-PCM digest"
assert ogg["output_sha256"] == sha(w + "/assets/tone.ogg"), "ogg output digest mismatch"
png = json.load(open(w + "/assets/pic.png.rowlconv.json"))
assert png["converter_name"] == "rowl_webp2png", png
assert png["converter_version"] == "1.0.0", png
assert png["settings"] == {"png_writer": "libpng", "dpi": None}, png
assert png["source_sha256"] == sha(w + "/src/pic.webp"), "png source digest mismatch"
assert png["output_sha256"] == sha(w + "/assets/pic.png"), "png output digest mismatch"
print("[AuthorGuide] sidecar provenance fields match recomputed digests.")
PYEOF
OGGENC="${ROWL_OGGENC_PATH:-build/bin/rowl_oggenc}"
"$OGGENC" --rate 44100 --channels 2 -o "$AUTHOR_WORK/tone-rerun.ogg" "$AUTHOR_WORK/src/decoded.pcm"
test "$(sha256sum "$AUTHOR_WORK/assets/tone.ogg" | cut -d' ' -f1)" = "$(sha256sum "$AUTHOR_WORK/tone-rerun.ogg" | cut -d' ' -f1)"
```

## 6. Paketle + verify

Dönüştürülmüş çıktılar normal asset olarak paketlenir; yanlarındaki
`.rowlconv.json` sidecar'lar da pakete girer ve gömülü manifestteki
`converted_from` kaydını besler (`tools/package_assets.py:129-155,289`):

```sh
# guide-probe: expect-ok
python3 tools/package_assets.py "$AUTHOR_WORK/assets" "$AUTHOR_WORK/author.rowlpkg"
python3 tools/package_assets.py verify "$AUTHOR_WORK/author.rowlpkg"
python3 tools/package_assets.py verify "$AUTHOR_WORK/author.rowlpkg" --json | python3 -c "import json,sys; assert json.load(sys.stdin)['ok'], 'verify --json not ok'"
```

## 7. Ham-kaynak uyarısı

`.mp3` / `.flac` / `.webp` paketçide **reddedilmez**:
`check_media_format` bu uzantılara uyarısız geçit verir
(`tools/package_assets.py:117-121` — kabul-dönüştürerek kuralı: ham
kaynağın pakete girmemesi import hattının, yani SENİN sorumluluğundadır).
Bu yüzden yazar hattı pakete giren her dosyanın uzantısını kapılar —
ham uzantı görürse blok kızarır:

```sh
# guide-probe: expect-ok
test ! -e "$AUTHOR_WORK/assets/tone.mp3"
test ! -e "$AUTHOR_WORK/assets/tone.flac"
test ! -e "$AUTHOR_WORK/assets/pic.webp"
for f in "$AUTHOR_WORK"/assets/*; do
  case "$f" in
    *.ogg|*.png|*.rowlconv.json) ;;
    *) echo "ham kaynak pakete gidiyor: $f"; exit 1;;
  esac
done
ls "$AUTHOR_WORK/assets"
```

## 8. Yayınlama: portable-zip + self-extracting + kur + doğrula + kaldır

Yazar paketiyle D5 zincirinin tekrarı (`tools/export_game.py:10-12,48-49`).
Determinizm notu: `VERSION` commit-sha + **tarih** içerir; aynı girdiler
aynı gün aynı baytları verir, gün değişince `VERSION` değişir — kılavuz
günler-arası bit-birebirlik iddia etmez. Dönüştürücü determinizmi (5.
adımdaki çift-koşu) bundan etkilenmez.

```sh
# guide-probe: expect-ok
python3 tools/export_game.py portable-zip --output "$AUTHOR_WORK/rowl-portable.zip" --package "$AUTHOR_WORK/author.rowlpkg"
```

```sh
# guide-probe: expect-ok
python3 tools/export_game.py self-extracting --input "$AUTHOR_WORK/rowl-portable.zip" --output "$AUTHOR_WORK/install-rowl-author.sh" --version author-guide
```

Kurulumcu payload'u hedefe **dokunmadan önce** doğrular (base64 çöz →
`PAYLOAD_SHA256` karşılaştır → iç `SHA256SUMS` tekrar-denetimi); ancak
üçü de geçerse dosyalar `--prefix` altına kopyalanır, ardından
kurulum makbuzu atomik yazılır (aşağıda):

```sh
# guide-probe: expect-ok
sh "$AUTHOR_WORK/install-rowl-author.sh" --prefix "$AUTHOR_WORK/prefix"
(cd "$AUTHOR_WORK/prefix" && sha256sum -c SHA256SUMS)
python3 tools/package_assets.py verify "$AUTHOR_WORK/prefix/game.rowlpkg"
```

### Makbuz / rollback

Kurulum, tüm kopyalar + çalıştırılabilir biti geçtikten sonra
`$prefix/.rowl-receipt` makbuzunu atomik yazar (geçici dosya + `mv`;
gömülü stub şablonu, `tools/export_game.py:461-484`). Format: satır 1
`ROWL_SDE_VERSION=<sürüm>`, satır 2 `PAYLOAD_SHA256=<zip-sha256>`,
sonrası satır başı bir kurulu dosya adı. Makbuz yazılamazsa kurulum
başarısız sayılır. Herhangi bir kopya/chmod/makbuz hatasında iki
mekanizma devreye girer: (1) `installed` listesinden geri-sarma — o ana
dek kopyalanan dosyalar `$prefix` altından tek tek `rm -f` ile silinir,
(2) tmp-tuzağı — geçici dizin `EXIT` tuzağı her yolda kaldırır;
kurulumcu çıkış 1 verir, yarı-kurulum bırakılmaz. Kaldırma, makbuz varsa 3. satırdan sonraki isim listesini
okur, makbuz yoksa (eski prefix — legacy-fallback) gömülü FILES
listesine düşer; joker/özyinelemeli silme yoktur (kullanıcı dosyalarına
dokunulmaz), makbuzun kendisi silinir ve boş kalan prefix dizini
kaldırılır (`tools/export_game.py:486-503`). Aşağıdaki prob makbuzun
içeriğini makineyle doğrular, sonra kaldırır:

```sh
# guide-probe: expect-ok
test -s "$AUTHOR_WORK/prefix/.rowl-receipt"
test "$(sed -n '1p' "$AUTHOR_WORK/prefix/.rowl-receipt")" = "ROWL_SDE_VERSION=author-guide"
test "$(sed -n '2p' "$AUTHOR_WORK/prefix/.rowl-receipt" | cut -d= -f2)" = "$(sha256sum "$AUTHOR_WORK/rowl-portable.zip" | cut -d' ' -f1)"
tail -n +3 "$AUTHOR_WORK/prefix/.rowl-receipt" | grep -x "game.rowlpkg"
for name in $(tail -n +3 "$AUTHOR_WORK/prefix/.rowl-receipt"); do test -e "$AUTHOR_WORK/prefix/$name" || exit 1; done
sh "$AUTHOR_WORK/install-rowl-author.sh" uninstall --prefix "$AUTHOR_WORK/prefix"
test ! -e "$AUTHOR_WORK/prefix"
```

## 9. Ters gidenler

- Dönüşüm hatası: bilinmeyen bayrak çıkış 2 verir, çıktı dosyası
  yazılmaz (yanlış komutun sessizce geçmesi imkânsızdır):

```sh
# guide-probe: expect-fail
OGGENC="${ROWL_OGGENC_PATH:-build/bin/rowl_oggenc}"
"$OGGENC" --bogus-flag -o "$AUTHOR_WORK/never.ogg" "$AUTHOR_WORK/src/decoded.pcm"
```

- Hash uyuşmazlığı: paketin tek baytı bile oynasa `verify` çıkış 1
  verir (dağıtım kapısı fail-closed'dur):

```sh
# guide-probe: expect-fail
cp "$AUTHOR_WORK/author.rowlpkg" "$AUTHOR_WORK/tampered.rowlpkg"
cp "$AUTHOR_WORK/author.rowlpkg.sha256" "$AUTHOR_WORK/tampered.rowlpkg.sha256"
python3 -c "import os; p=os.environ['AUTHOR_WORK']+'/tampered.rowlpkg'; b=bytearray(open(p,'rb').read()); b[20]^=1; open(p,'wb').write(bytes(b))"
python3 tools/package_assets.py verify "$AUTHOR_WORK/tampered.rowlpkg"
```

- Diğerleri: `sha256sum -c` bir dosyada BAŞARISIZ derse zip'i yeniden
  aç (bozuk dosyayla devam etme); disk dolarsa kurulum
  `cannot install ...` ile durur — yer açıp aynı komutu baştan çalıştır;
  bu kurulumcu Linux-only POSIX `sh`'tir.

Üçüncü taraf lisans envanteri için bkz. `THIRD_PARTY_LICENSES.md` (repo kökü).
