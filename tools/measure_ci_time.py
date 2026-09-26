#!/usr/bin/env python3
"""Süre-ölçümlü smoke koşucu (493): bir komutu çalıştırır, duvar-saat
süresini ölçer ve süre eşiğine karşı raporlar.

Kullanım:
    measure_ci_time.py --label <ad> --threshold <sn> [--json-out <dosya>]
        -- <komut> [args...]

Davranış:
  - Komutun çıkış kodunu aynen geçirir (komut patlarsa süre raporlanır
    ama çıkış kodu komutunki olur; eşik aşımı ayrıca belirtilir).
  - Komut 0 ile bitip süre <= eşik ise exit 0 + "SONUÇ: YEŞİL".
  - Komut 0 ile bitip süre > eşik ise exit 2 + "SONUÇ: KIRMIZI"
    (eşik aşımı; komutun kendisi değil süre kırmızı).
  - GITHUB_STEP_SUMMARY tanımlıysa tek satır özet oraya da eklenir.
  - --json-out verilirse {"label","elapsed_s","threshold_s","ok",...}
    JSON'u o dosyaya yazılır (artifact kanıtı için).

Örnek (CI):
    python3 tools/measure_ci_time.py --label linux-package-smoke \\
        --threshold 300 -- xvfb-run -a ./RowlGame --project . --package-smoke-test
"""

import argparse
import json
import os
import subprocess
import sys
import time


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Komutu süre-ölçümüyle çalıştır, süre eşiğini raporla."
    )
    parser.add_argument("--label", required=True, help="Rapor etiketi (job/smoke adı).")
    parser.add_argument(
        "--threshold",
        required=True,
        type=float,
        help="Süre eşiği (saniye). Süre bunu aşarsa exit 2.",
    )
    parser.add_argument(
        "--json-out",
        default=None,
        help="Sonuç JSON'unun yazılacağı dosya (opsiyonel).",
    )
    parser.add_argument(
        "command",
        nargs=argparse.REMAINDER,
        help="Çalıştırılacak komut (-- ayracından sonra).",
    )
    args = parser.parse_args(argv)
    # argparse.REMAINDER "--" ayracını da yakalar; temizle.
    cmd = list(args.command)
    while cmd and cmd[0] == "--":
        cmd.pop(0)
    args.command = cmd
    if not args.command:
        parser.error("çalıştırılacak komut yok (-- <komut> gerekli).")
    if args.threshold < 0:
        parser.error("--threshold negatif olamaz.")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    start = time.monotonic()
    try:
        proc = subprocess.run(args.command, check=False)
        rc = proc.returncode
    except FileNotFoundError:
        print(
            f"[ci-time] {args.label}: komut bulunamadı: {args.command[0]}",
            file=sys.stderr,
        )
        return 1
    except OSError as exc:
        print(f"[ci-time] {args.label}: komut çalışmadı: {exc}", file=sys.stderr)
        return 1
    elapsed = time.monotonic() - start

    over = elapsed > args.threshold
    status = "KIRMIZI" if over else "YEŞİL"
    print(
        f"[ci-time] {args.label}: süre={elapsed:.1f}sn "
        f"eşik={args.threshold:.0f}sn SONUÇ: {status} "
        f"(komut çıkışı: {rc})"
    )
    if over:
        print(
            f"[ci-time] EŞİK AŞIMI: {args.label} {elapsed:.1f}sn > "
            f"{args.threshold:.0f}sn eşiği.",
            file=sys.stderr,
        )

    if args.json_out:
        try:
            with open(args.json_out, "w", encoding="utf-8") as fh:
                json.dump(
                    {
                        "label": args.label,
                        "elapsed_s": round(elapsed, 2),
                        "threshold_s": args.threshold,
                        "command": args.command,
                        "command_rc": rc,
                        "over_threshold": over,
                    },
                    fh,
                    ensure_ascii=False,
                    indent=2,
                )
        except OSError as exc:
            print(f"[ci-time] JSON yazılamadı: {exc}", file=sys.stderr)

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        try:
            with open(summary, "a", encoding="utf-8") as fh:
                fh.write(
                    f"[ci-time] {args.label}: {elapsed:.1f}sn / "
                    f"{args.threshold:.0f}sn eşiği — {status}\n"
                )
        except OSError:
            pass

    if rc != 0:
        return rc
    return 2 if over else 0


if __name__ == "__main__":
    sys.exit(main())
