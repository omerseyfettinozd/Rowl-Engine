# Paket Tehdit-Modeli Tavanı (W8-g G9)

Kapsam (native scope): `tools/verify_release_package.py` (embedded manifest
+ oran kapısı `:96`) + `tools/package_assets.py:verify_package` (index +
sidecar) + `.rowlconv.json` sidecar'lar. C# kapsamı yok (editör paketi
doğrulamaz, player native verify'e güvenir).

## 1. Tehdit modeli (iki sınıf)

- **Accidental-corruption (kapsanır — EVET):** bit-rot, yarım yazım, bayat
  sidecar, şişirme-bombası. Yakalama mekanizmaları: tamsayı-bölme oran kapısı
  (`verify_release_package.py:96`, `MAX_EXPANSION_RATIO`), index↔manifest
  flags çapraz-kontrolü (W8-fix çift-yüz promote), **manifest digest
  re-hash'i — HİKİ TAŞIMA SINIFININ İKİ ANAHTARI**, sidecar `output_sha256`
  uyuşmazlığında `converted_from` düşürme + fail-soft uyarı. Kilitler:
  `tests/test_d18a_verify_depth.py` R1/R2/S1/S2 + `test_package_ratio_gate.py`
  + `tests/test_vfs_security.cpp` P2-7/P2-8.
  - Packer (`tools/package_assets.py:347`) **HER** kayda `sha256` (ham içerik
    hash'i) yazar; `compressed_sha256` (saklı bayt hash'i, `:354`) yalnız
    flags=1 kayıtlara eklenir.
  - Okuyucu (`rowlpkg_reader.cpp`) iki anahtarı da alır ve **saklama sınıfına
    göre** bağlar: flags=1 → `compressed_sha256`, flags=0 → `sha256`. Ham
    girdiler içeriğini olduğu gibi sakladığından (`compressedSize ==
    uncompressedSize`, `loadIndexTable` zorlar) packer'ın `sha256`'sı doğrudan
    saklı baytlara uygulanır.
  - **P2-7 öncesi bu eşleme eksikti:** okuyucu yalnız `compressed_sha256`
    anahtarını okuyor ve onu yalnız `flags == 1` için bağlıyordu; ham girdiler
    hiç ölçülmüyordu. Bu, **kazara-bozulma** sınıfında gerçek bir boşluktu:
    bozuk bir ham yük sessizce sunulurken zstd ikizi sertçe reddediliyordu
    (aynı bozulma sınıfı iki yolda farklı davranıyordu). P2-7 bu boşluğu
    kapattı: artık **flags=0 da, akış yolu da** kazara bozulmaya karşı ölçülür.
    Eksik/bozuk-şekilli anahtar yoksa legacy warn-open aynen korunur
    (4h/4i/6a-6f varyantları).
- **Malicious-tamper (kapsanmaz — HAYIR):** imzasız mimaride manifest-rewriting
  ayırt edilemez. Kanıt (ölçüm-kolu `353a38c`, `/tmp/forge-proof`): flags=1
  flip + aynı-uzunluk hex-takas + taze sidecar → `verify` exit 0.
  **Kötü-niyet bağlamında flags=0 kolu da atlatır** — saldırgan hem payload'ı
  hem manifest kaydını kendi hash'iyle yeniden yazdığında (denetim varyantı
  4l) kapı sağlamdır, çünkü kimlik değil bütünlük doğrulanır. Bu bir
  implementasyon hatası DEĞİL, mimari tavandır: anahtarsız legacy flags=1
  yükleri `[WARN][legacy-unverified]` ile geçer (KI-11 dürüstlük eki).
  *Kapsam farkı:* "flags=0 kolu da atlatır" yalnız **kötü-niyet** bağlamında
  doğrudur. **Kazara-bozulma** bağlamında flags=0 kolu P2-7 sonrası artık
  atlatmaz — bkz. yukarıdaki accidental-corruption maddesi.

## 2. Tavan hükmü

İmza-mimarisi (manifest imzası + anahtar-dağıtımı) olmadan bu dosyanın §1'deki
EVET kümesi tavandır. İmza kapsam-dışıdır (kayıtlı-sınır, D18 imza-dışı
kabulü); isteyen dilim önce format-anahtarı + verify-kapısı + anahtar-yönetimi
tasarımıyla gelir, tek-satır fix'le kapanmaz.

## 3. Yeniden-puan girdisi (W7 jürisine)

Paket boyutu ölçütü "kötü-niyetli bozmayı yakalar" okunursa skor tavanı
**~70-78 bandı**dır (accidental-küme tam, malicious-küme mimari-dışı).
Ölçüt "accidental-corruption + format-dürüstlüğü" okunursa mevcut kilitler
tamdır; okuma seçimi jürinindir, bu belge iki okumaya da kanıt verir.
