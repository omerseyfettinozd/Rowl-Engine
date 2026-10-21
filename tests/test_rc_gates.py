#!/usr/bin/env python3
"""RC kapı toplayıcısının sözleşmesi (C05): kapı sonuçlarından genel sonuç.

Yapay kapı sonuçlarıyla tools/rc_gate_report.py'nin PASS yalnız her zorunlu
kapı PASS iken verdiğini; FAIL / SKIPPED / ENV / eksik kapının PASS vermediğini
kilitler. Bir test kusuru geri gelirse ilgili kapı FAIL olur ve RC geçmez.
"""

import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "rc_gate_report.py"
SCRIPT = ROOT / "tools" / "run_rc_gates.sh"
sys.path.insert(0, str(ROOT / "tools"))
import rc_gate_report as rc  # noqa: E402


def gate(name, status="PASS", reason=""):
    return {"name": name, "status": status, "reason": reason,
            "duration_s": 1, "log": "logs/%s.log" % name}


def all_pass():
    return {n: gate(n) for n in rc.REQUIRED_GATES}


def check(label, condition):
    if not condition:
        raise SystemExit("FAIL: " + label)


def evaluate(results, **kw):
    overall, code, _, _ = rc.evaluate(results, **kw)
    return overall, code


# 1. hepsi PASS -> PASS, exit 0
check("hepsi PASS", evaluate(all_pass()) == ("PASS", 0))

# 2. tek FAIL -> FAIL
for name in rc.REQUIRED_GATES:
    r = all_pass()
    r[name] = gate(name, "FAIL", "rc=1")
    overall, code = evaluate(r)
    check("FAIL %s -> FAIL" % name, overall == "FAIL" and code == 1)

# 3. SKIPPED / ENV PASS sayılmaz (quick dışında)
for status in ("SKIPPED", "ENV"):
    r = all_pass()
    r["sanitizer-tsan"] = gate("sanitizer-tsan", status, "libtsan yok")
    overall, code = evaluate(r)
    check("%s PASS değil" % status, overall != "PASS" and code != 0)
    check("%s FAIL da değil ama INCOMPLETE" % status, overall == "INCOMPLETE")

# 4. eksik kapı -> FAIL
for name in rc.REQUIRED_GATES:
    r = all_pass()
    del r[name]
    overall, code = evaluate(r)
    check("eksik %s -> FAIL" % name, overall == "FAIL" and code == 1)

# 5. --quick: yalnız SKIPPED(quick) ise PASS (partial), exit 2
r = all_pass()
for name in ("sanitizer-asan-ubsan", "sanitizer-tsan"):
    r[name] = gate(name, "SKIPPED", "quick: atlandı")
check("quick partial", evaluate(r, quick=True) == ("PASS (partial)", 2))
# quick değilken aynı sonuç PASS olamaz
check("quick olmadan partial yok", evaluate(r)[0] == "INCOMPLETE")
# quick + başka bir SKIPPED sebebi -> partial değil
r["sanitizer-tsan"] = gate("sanitizer-tsan", "SKIPPED", "libtsan yok")
check("quick + başka skip", evaluate(r, quick=True)[0] == "INCOMPLETE")
# quick + FAIL -> FAIL
r = all_pass()
r["sanitizer-asan-ubsan"] = gate("sanitizer-asan-ubsan", "SKIPPED", "quick")
r["native-ctest-debug"] = gate("native-ctest-debug", "FAIL", "rc=8")
check("quick + FAIL", evaluate(r, quick=True) == ("FAIL", 1))

# 6. kirli ağaç en iyi "PASS (dirty)", exit 2
check("dirty", evaluate(all_pass(), allow_dirty=True, dirty=True) == ("PASS (dirty)", 2))

# 7. güç profili
for profile, expected in (("power-saver", True), ("powersave/power", True),
                          ("powersave/balance_power", True), ("powersave", True),
                          ("performance", False), ("powersave/performance", False),
                          ("powersave/balance_performance", False),
                          ("unknown", False), ("", False)):
    check("power-saver(%r)" % profile, rc.is_power_saver(profile) is expected)

# 8. uçtan uca: record + finalize dosyaları ve exit kodu
with tempfile.TemporaryDirectory() as directory:
    d = pathlib.Path(directory)
    results = d / "results.jsonl"
    for name in rc.REQUIRED_GATES:
        status, reason, hint = "PASS", "", []
        if name == "native-ctest-debug":
            status, reason = "FAIL", "rc=8"
            hint = ["--hint", "power profile is power-saver"]
        subprocess.run([sys.executable, str(TOOL), "record", "--file", str(results),
                        "--name", name, "--status", status, "--reason", reason,
                        "--duration", "2.5", "--log", "logs/%s.log" % name] + hint,
                       check=True)
    identity = d / "identity.json"
    identity.write_text(json.dumps({"commit": "abc", "dirty": False}))
    out = d / "out"
    run = subprocess.run([sys.executable, str(TOOL), "finalize", "--file", str(results),
                          "--identity", str(identity), "--out", str(out)],
                         capture_output=True, text=True)
    check("finalize FAIL exit 1", run.returncode == 1)
    report = json.loads((out / "report.json").read_text())
    check("report.json overall FAIL", report["overall"] == "FAIL")
    md = (out / "report.md").read_text()
    check("report.md hint", "env hint: power profile is power-saver" in md)
    check("report.md genel sonuç", "Genel sonuç: FAIL" in md)
    # Düzeltilmiş kapı (aynı ad, son kayıt kazanır) -> PASS, exit 0
    subprocess.run([sys.executable, str(TOOL), "record", "--file", str(results),
                    "--name", "native-ctest-debug", "--status", "PASS"], check=True)
    run = subprocess.run([sys.executable, str(TOOL), "finalize", "--file", str(results),
                          "--identity", str(identity), "--out", str(out)],
                         capture_output=True, text=True)
    check("finalize PASS exit 0", run.returncode == 0)

# 9. betik sözdizimi ve yardımcıyı çağırdığı
check("bash -n", subprocess.run(["bash", "-n", str(SCRIPT)]).returncode == 0)
text = SCRIPT.read_text(encoding="utf-8")
check("betik rc_gate_report.py kullanır", "rc_gate_report.py" in text)
for name in rc.REQUIRED_GATES:
    check("betik %s kapısını koşar/raporlar" % name, name in text)

# 10. RC Soak ayrı ctest testi: tam olarak bir testte koşar, sanitizer
# kapılarının -R regex'i ikisini de kapsar (bölüm sessizce düşmesin).
cm = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
check("soak testi kayıtlı", "add_test(NAME rowl_native_soak_tests COMMAND rowl_tests --only-soak)" in cm)
check("ana test soak'sız ve soak testiyle aynı blokta",
      "add_test(NAME rowl_native_tests COMMAND rowl_tests --skip-soak)" in cm)
ci = (ROOT / ("." + "github") / "workflows" / "ci.yml").read_text(encoding="utf-8")
for label, body in (("ci.yml", ci), ("run_rc_gates.sh", text)):
    for line in body.splitlines():
        if "-R " in line and "rowl_native_tests" in line:
            check("%s sanitizer -R soak testini içerir" % label, "rowl_native_soak_tests" in line)

print("[RcGates] kapı toplama sözleşmesi geçti.")
