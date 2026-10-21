#!/usr/bin/env python3
"""RC kapı raporu toplayıcı (tools/run_rc_gates.sh çağırır).

Kapı sonuçlarını toplar, report.json + report.md yazar, genel sonucu hesaplar.
Kural: genel sonuç yalnız ZORUNLU her kapı PASS ise PASS'tir. SKIPPED, ENV,
eksik kapı PASS sayılmaz. Tek istisna: --quick, kapıların hepsi PASS ya da
SKIPPED(quick) ise "PASS (partial)" verir ve çıkış kodu 2'dir (RC kanıtı değil).

Alt komutlar:
  record  --file F --name N --status S [--reason R] [--duration D] [--log L] [--hint H]
  finalize --file F --identity I.json --out DIR [--quick] [--allow-dirty]
  power-saver PROFILE     (exit 0: güç tasarrufu profili, 1: değil)

Çıkış kodları (finalize): 0 PASS, 1 FAIL/INCOMPLETE, 2 PASS (partial).
"""

import argparse
import json
import os
import re
import sys

# Zorunlu kapılar (tek kaynak). Sıra rapor sırasıdır.
REQUIRED_GATES = [
    "static-diff-check",
    "static-portability",
    "static-toolchain-parity",
    "static-launcher-parity",
    "native-build-debug",
    "native-ctest-debug",
    "abi-additive",
    "editor-xunit",
    "rowleye-tests",
    "sanitizer-asan-ubsan",
    "sanitizer-tsan",
    "package-gate",
]
STATUSES = ("PASS", "FAIL", "SKIPPED", "ENV")

# ctest'in zaten koştuğu python araç testleri: ayrı kapı değil, listelenir.
COVERED_BY_CTEST_NOTE = (
    "python araç testleri (tests/test_*.py) native-ctest-debug kapısında "
    "ctest üzerinden koşar; argüman isteyenler (test_converters, "
    "test_demo_packaged) ctest'te doğru argümanlarla çağrılır."
)

_POWER_SAVER = re.compile(r"power-?saver|(^|/)(balance_)?power$", re.I)


def is_power_saver(profile):
    """Profil dizesi (env veya governor[/EPP]) güç tasarrufunu gösteriyor mu."""
    profile = (profile or "").strip()
    if _POWER_SAVER.search(profile):
        return True
    # EPP olmadan salt "powersave" governor'ı da tasarruf kabul edilir.
    return profile.lower() == "powersave"


def record(path, gate):
    if gate["status"] not in STATUSES:
        raise ValueError("bilinmeyen durum: %r" % gate["status"])
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(gate, ensure_ascii=False) + "\n")


def load_results(path):
    results = {}
    if not os.path.exists(path):
        return results
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                gate = json.loads(line)
                results[gate["name"]] = gate  # son kayıt kazanır
    return results


def evaluate(results, quick=False, allow_dirty=False, dirty=False,
             required=None):
    """(overall, exit_code, gates_list, reasons) döndürür. Saf fonksiyon."""
    required = list(REQUIRED_GATES if required is None else required)
    gates = []
    reasons = []
    for name in required:
        gate = results.get(name)
        if gate is None:
            gate = {"name": name, "status": "FAIL",
                    "reason": "MISSING: kapı hiç koşmadı/kaydedilmedi",
                    "duration_s": 0, "log": ""}
            reasons.append("%s: eksik kapı" % name)
        gates.append(dict(gate))
    # Zorunlu listede olmayan ama kaydedilmiş kapılar da (bilgi) raporlanır;
    # FAIL olanlar yine de sonucu bozar.
    for name, gate in results.items():
        if name not in required:
            extra = dict(gate)
            extra["extra"] = True
            gates.append(extra)

    failed = [g for g in gates if g["status"] == "FAIL"]
    not_pass = [g for g in gates if g["status"] != "PASS" and not g.get("extra")]
    for gate in failed:
        reasons.append("%s: FAIL" % gate["name"])

    if failed:
        return "FAIL", 1, gates, reasons

    skipped_other = []
    for gate in not_pass:
        quick_skip = (gate["status"] == "SKIPPED"
                      and str(gate.get("reason", "")).startswith("quick"))
        if not (quick and quick_skip):
            skipped_other.append(gate)
    if skipped_other:
        for gate in skipped_other:
            reasons.append("%s: %s (PASS değil)" % (gate["name"], gate["status"]))
        return "INCOMPLETE", 1, gates, reasons

    if quick and not_pass:
        reasons.append("--quick: sanitizer/TSan kapıları koşmadı")
        return "PASS (partial)", 2, gates, reasons
    if dirty:
        reasons.append("kirli ağaç (--allow-dirty): RC kanıtı değil")
        return "PASS (dirty)", 2, gates, reasons
    return "PASS", 0, gates, reasons


def _md(identity, overall, gates, reasons):
    lines = ["# RC kapı raporu", "", "**Genel sonuç: %s**" % overall, ""]
    lines.append("## Kimlik")
    for key in ("commit", "dirty", "mode", "started_utc", "host", "compiler",
                "dotnet", "power_profile", "power_profiles_daemon"):
        if key in identity:
            lines.append("- %s: %s" % (key, identity[key]))
    if identity.get("power_saver"):
        lines.append("- UYARI: güç profili power-saver; RC performans "
                     "profilinde kapılanmalı.")
    lines += ["", "## Kapılar", "",
              "| Kapı | Durum | Süre (sn) | Log | Not |",
              "|---|---|---|---|---|"]
    for gate in gates:
        status = gate["status"]
        reason = gate.get("reason", "")
        shown = status if status in ("PASS", "FAIL") or not reason \
            else "%s(%s)" % (status, reason.split(":")[0])
        note = reason
        if gate.get("env_hint"):
            note = (note + " | " if note else "") + "env hint: " + gate["env_hint"]
        lines.append("| %s | %s | %s | %s | %s |" % (
            gate["name"], shown, gate.get("duration_s", 0),
            gate.get("log", ""), note.replace("|", "/")))
    if reasons:
        lines += ["", "## Neden PASS değil", ""] + ["- " + r for r in reasons]
    lines += ["", "## Notlar", "", "- " + COVERED_BY_CTEST_NOTE,
              "- Kapsam: yalnız Linux x86_64 (Windows/macOS/ARM dışı).", ""]
    return "\n".join(lines)


def finalize(results_path, identity_path, out_dir, quick, allow_dirty):
    identity = {}
    if identity_path and os.path.exists(identity_path):
        with open(identity_path, encoding="utf-8") as handle:
            identity = json.load(handle)
    dirty = bool(identity.get("dirty"))
    identity["mode"] = "quick" if quick else "full"
    overall, code, gates, reasons = evaluate(
        load_results(results_path), quick=quick, allow_dirty=allow_dirty,
        dirty=dirty)
    report = {"overall": overall, "exit_code": code, "identity": identity,
              "gates": gates, "reasons": reasons,
              "covered_by_ctest": COVERED_BY_CTEST_NOTE}
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as h:
        json.dump(report, h, ensure_ascii=False, indent=2)
        h.write("\n")
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as h:
        h.write(_md(identity, overall, gates, reasons))
    return overall, code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawTextHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    rec = sub.add_parser("record")
    rec.add_argument("--file", required=True)
    rec.add_argument("--name", required=True)
    rec.add_argument("--status", required=True, choices=STATUSES)
    rec.add_argument("--reason", default="")
    rec.add_argument("--duration", type=float, default=0)
    rec.add_argument("--log", default="")
    rec.add_argument("--hint", default="")
    fin = sub.add_parser("finalize")
    fin.add_argument("--file", required=True)
    fin.add_argument("--identity", default="")
    fin.add_argument("--out", required=True)
    fin.add_argument("--quick", action="store_true")
    fin.add_argument("--allow-dirty", action="store_true")
    pw = sub.add_parser("power-saver")
    pw.add_argument("profile")
    args = parser.parse_args(argv)

    if args.cmd == "record":
        gate = {"name": args.name, "status": args.status, "reason": args.reason,
                "duration_s": round(args.duration, 1), "log": args.log}
        if args.hint:
            gate["env_hint"] = args.hint
        record(args.file, gate)
        return 0
    if args.cmd == "power-saver":
        return 0 if is_power_saver(args.profile) else 1
    overall, code = finalize(args.file, args.identity, args.out, args.quick,
                             args.allow_dirty)
    print("[rc-gates] genel sonuç: %s (exit %d)" % (overall, code))
    return code


if __name__ == "__main__":
    sys.exit(main())
