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

Oyunu `prefix/run_game.sh` başlatır: kendi klasörüne geçip kurulu oyuncuyu
(`prefix/RowlGame`) çalıştırır; nereden açılırsa açılsın (dosya yöneticisi,
mutlak yol) `Assets/packages/game.rowlpkg` bulunur. Gerçek oyun penceresi açar:

```text
"$GUIDE_WORK/prefix/run_game.sh"
```

Bayraklar (`RowlGame --help` çıktısıyla birebir — uydurma yok; `run_game.sh`
hepsini aynen iletir):

| Bayrak | Anlam |
|---|---|
| `-h, --help` | Yardımı göster, çık |
| `-v, --version` | Sürümü göster, çık |
| `-p, --project <dir>` | Proje kökü (varsayılan: `.`) |
| `-s, --story <file>` | Hikâye grafiği JSON yolu |
| `-w, --width <px>` / `--height <px>` | Pencere boyutu (varsayılan: 1920x1080) |
| `-t, --title <ad>` | Pencere başlığı |
| `--title-state <ad>` | `title` (varsayılan): oyun başlık ekranıyla açılır · `story`: başlık ekranını atla, hikâyeye doğrudan gir (geliştirme) |
| `--slot <N>` | Hızlı-kayıt slotu 0-9 (F5/F9, varsayılan: 0) |
| `--no-vsync` | Dikey senkronu kapat |
| `--gpu-smoke-test` | Tek kare çiz, çık (CI) |
| `--package-smoke-test` | Paketli VFS grafiğinden tek kare çiz, oyuncu profilini dene (ilk satırı okundu say, bir tercihi değiştirip geri al, kaydet), başlık ekranını açıp Yeni Oyun ile kapat (`Package smoke title: ok`), çık. Oyunun profiline yazar (tercihler aynı kalır, ilk satır okundu sayılır); deneme için `XDG_DATA_HOME`'u geçici bir dizine ver |
| `--print-game-identity` | Projeyi bağla, tek satır JSON yaz (oyun kimliği, kimlik kaynağı, veri/kayıt/profil yolları, profil durumu), çık; pencere açmaz |

Kontroller: Space/Enter/Click ilerler · F5 hızlı-kayıt · F9 hızlı-yükleme ·
0-9 slot seçer · Backspace/Z 1 adım geri · Escape/P duraklatma menüsü.

### Başlık ekranı

Oyun, hikâyeye girmeden önce başlık ekranıyla açılır (başlık = oyunun adı):

| Satır | Ne yapar |
|---|---|
| Yeni Oyun | Başlangıç düğümünden başlar |
| Devam Et | En yeni **geçerli** kaydı (`saved_at`'e göre, 0-99 yuvaları) yükler; satırda yuva ve tarih görünür. Kayıt yoksa ya da hepsi bozuksa devre dışıdır (`kayit yok`) |
| Oyunu Yukle | Yuva sayfası: her yuvada tarih, bölüm ve özet, seçili yuvanın küçük resmi. Boş yuva `bos yuva`, bozuk yuva `bozuk kayit` der ve hiçbir şeyi değiştirmez |
| Tercihler | Ana ses, müzik, SFX, seslendirme, metin hızı (Sol/Sağ); değişiklikler oyuncu profiline yazılır |
| Cikis | İki adımlı onay (Enter, Enter) |

Escape/P ve Geri başlık ekranından hikâyeyi **başlatmaz**: alt sayfada
başlığa döner, başlıkta yalnız çıkış onayını bırakır. Bir kayıt yüklenemezse
başlıkta kalınır (`kayit yuklenemedi`) ve sonraki Devam Et bir önceki geçerli
kaydı dener. Duraklatma menüsündeki ses ve metin hızı değişiklikleri de
profile yazılır.

### Kayıtlar ve oyuncu profili nerede?

Her oyun kendi **oyun kimliği** (`game_id`, paketin içindeki `rowl/game.json`)
ile anılır; kayıtlar ve oyuncu profili oyun klasöründe DEĞİL, kullanıcı veri
dizininde, kimliğe göre ayrı tutulur:

```text
$XDG_DATA_HOME/rowl-engine/games/<game-id>/saves/     kayıt slotları
$XDG_DATA_HOME/rowl-engine/games/<game-id>/profile/   player-profile.json
```

(`XDG_DATA_HOME` boşsa `~/.local/share`; Windows'ta `%LOCALAPPDATA%`.) Oyun
klasörünü taşımak ya da `run_game.sh`'i başka bir dizinden açmak verini
değiştirmez; aynı varlık adlarını kullanan iki ayrı oyun birbirinin kaydını,
okundu bilgisini ya da tercihlerini görmez. Başlatıcı (`run_game.sh`) yalnız
oyun klasörüne geçer, veri dizinini yönlendirmez. Kimliği yazılı olmayan
(geliştirme) projeler eskisi gibi `<proje>/saves` ve `<proje>/profile`
kullanır; eski `<oyun>/saves` kayıtları ilk açılışta kullanıcı veri dizinine
**kopyalanır** (eskisi silinmez).

Profil (`player-profile.json`, sürüm 1) okunan satırların kimliklerini ve
oyuncu tercihlerini tutar. Oyuncu açılışta kayıtlı tercihleri uygular: ana /
müzik / ses / efekt / ortam / arayüz seslerini, yazı hızını, yazı ölçeğini,
yüksek karşıtlığı, azaltılmış hareketi, karışım eğrisini ve SFX havuz
derinliğini; dil yalnız `en` dışındaysa ve oyun o dili sunuyorsa uygulanır.
Henüz uygulanmayanlar (menü/başlık ekranı gelince): otomatik ilerleme
gecikmesi, atlama kipi, otomatik mod.

Açılışta oyuncu şunu stdout'a yazar (sorun gidermek için):

```text
[Player] Game id: <uuid> (source: package)
[Player]   data dir: .../rowl-engine/games/<uuid>
[Player]   save dir: .../games/<uuid>/saves
[Player]   profile:  .../games/<uuid>/profile/player-profile.json (status: loaded, read ids: 12)
```

`status`: `loaded`, `missing` (ilk çalıştırma), `corrupt` (bozuk dosya
`player-profile.corrupt-<zaman>.json` olarak kenara alındı, varsayılanlarla
devam), `unsupported_version` (dosya daha yeni bir sürümden; **salt-okunur** —
dosya asla değiştirilmez, smoke "profile is read-only" yazar) ve `wrong_game`
(başka oyunun dosyası; salt-okunur). Aynı bilgi makine okunur biçimde:

```sh
# guide-probe: expect-ok
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$GUIDE_WORK/prefix/RowlGame" --project "$GUIDE_WORK/prefix" --print-game-identity | grep -q '^{"data_dir"'
```

### Smoke sınırı (neden pencere açılmıyor?)

CI'da pencere açılmasın diye smoke olarak SADECE `--help` / `--version`
(çıkış 0) ve bir argüman-hatası (çıkış ≠ 0) koşar. Gerçek kare çizimi
(`--gpu-smoke-test` / `--package-smoke-test`) display/GPU ister; kapısı
zaten `rowl_player_gpu_msdf_smoke` ve `rowl_demo_*_packaged` CTest'lerindedir.
Burada tekrarlanmaz.

```sh
# guide-probe: expect-ok
"$GUIDE_WORK/prefix/RowlGame" --help
```

```sh
# guide-probe: expect-ok
"$GUIDE_WORK/prefix/RowlGame" --version
```

Hatalı bayrak değeri 0-dışı çıkış verir (oyuncu argümanları doğrular):

```sh
# guide-probe: expect-fail
"$GUIDE_WORK/prefix/RowlGame" --slot 99
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
python3 tools/package_assets.py verify "$GUIDE_WORK/prefix/Assets/packages/game.rowlpkg"
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
"$GUIDE_WORK/prefix/RowlGame" --help >/dev/null
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
