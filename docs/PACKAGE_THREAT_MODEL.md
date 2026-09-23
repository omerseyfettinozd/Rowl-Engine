# Paket Tehdit-Modeli Tavanı (W8-g G9)

Kapsam (native scope): `tools/verify_release_package.py` (embedded manifest
+ oran kapısı `:96`) + `tools/package_assets.py:verify_package` (index +
sidecar) + `.rowlconv.json` sidecar'lar. C# kapsamı yok (editör paketi
doğrulamaz, player native verify'e güvenir).

## 1. Tehdit modeli (iki sınıf)

- **Accidental-corruption (kapsanır — EVET):** bit-rot, yarım yazım, bayat
  sidecar, şişirme-bombası. Yakalama mekanizmaları: tamsayı-bölme oran kapısı
  (`verify_release_package.py:96`, `MAX_EXPANSION_RATIO`), index↔manifest
  flags çapraz-kontrolü (W8-fix çift-yüz promote), `compressed_sha256`
  re-hash (anahtar varsa; KI-11 (a)), sidecar `output_sha256` uyuşmazlığında
  `converted_from` düşürme + fail-soft uyarı. Kilitler:
  `tests/test_d18a_verify_depth.py` R1/R2/S1/S2 + `test_package_ratio_gate.py`.
- **Malicious-tamper (kapsanmaz — HAYIR):** imzasız mimaride manifest-rewriting
  ayırt edilemez. Kanıt (ölçüm-kolu `353a38c`, `/tmp/forge-proof`): flags=1
  flip + aynı-uzunluk hex-takas + taze sidecar → `verify` exit 0. flags=0 kolu
  da atlatır. Bu bir implementasyon hatası DEĞİL, mimari tavandır: anahtarsız
  legacy flags=1 yükleri `[WARN][legacy-unverified]` ile geçer (KI-11
  dürüstlük eki).

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
