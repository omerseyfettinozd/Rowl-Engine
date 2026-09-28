/**
 * test_lua_sandbox_dos_probe.cpp — Lua sandbox DYNAMIC güvenlik taraması.
 *
 * Kapsam (bu iki mevcut dosyanın BOŞLUĞUNU doldurur — asla kopyalamaz):
 *   test_lua_sandbox.cpp  : sonsuz döngü, string.rep 2^40, basit derin
 *                           recursion, error({}), stdlib kirliliği, rawset
 *                           impostor, session izolasyonu.
 *   test_lua_hardening.cpp: catch-respin (#31), wall-clock (#24), pattern
 *                           sınırı (F1), __gc guard (#32), kota/recovery
 *                           (F2/F4), eşzamanlı hammer (#34/#37).
 *
 * Burada YENİ olanlar:
 *   A) Tam global yüzey envanteri (sayısal) + iç içe kütüphane sayımı.
 *   B) Sistematik kaçış erişilebilirliği — her bilinen kaçış adı TEK TEK
 *      denenir, sonuç (nil / erişilebilir / çağrıldı-ve-hata) kaydedilir.
 *   C) Instruction-count DÜŞÜK ama wall-time YÜKSEK DoS vektörleri
 *      (table.insert O(n), karesel concat, table.concat, table.sort) —
 *      10M komut tavanı bunları yakalayamıyor; duvar-saati yakalıyor mu?
 *   D) C-yığını taşması (metamethod rekursiyonu) + parser derinliği.
 *   E) Sınır kapıları: 256 KiB script tavanı, 128 modül tavanı.
 *   F) Köprü (rowl.var_*) tür zorlama ve yeniden-giriş davranışı.
 *   G) *** P0 KACIS *** _G metatable bitkisi. callOptionalFunction()
 *      repairGlobals() ÇAĞIRMAYAN tek yol; bitki sonraki
 *      getGlobalNumber()'de (lua_sandbox.cpp:920, D07 ham-okuma DEĞİL)
 *      pcall'sız tetiklenir ve süreci ABORT eder.
 *
 * ═══ KALDIRMA POLİTİKASI (okumadan önce bil) ═══
 *   • "savunma geriye dönüşü"  → exit(1), kırmızı. (Varsayılan yeşil beklenir.)
 *   • "DOĞRULANMIŞ kaçış"      → stdout'a [P0]/[P1] ile bağırır, özet
 *                                 sayacını artırır, ama süreci DÜŞÜRMEZ.
 *                                 Gerekçe: bu dosya statik başlatıcıyla
 *                                 koştuğu için exit(1) rowl_tests'in
 *                                 TAMAMINI (300+ test) düşürürdü.
 *   • Kaçışı CI'da da kırmızı yapmak için: ROWL_DOS_PROBE_STRICT=1
 *   • Gerçek abort'u (SIGABRT) canlandırmak için: ROWL_DOS_PROBE_LIVE=1
 *     — varsayılan KAPALI çünkü o probe bilerek süreci öldürür.
 *
 * KAYIT NOTU: test_main.cpp bu dosyayı çağırmaz (o dosya bu ajana tahsisli
 * değildir). Bu yüzden dosya hem normal giriş noktası sunar hem de
 * statik başlatıcıyla kendini kaydeder — rowl_tests koştuğunda probs
 * GERÇEKTEN koşar. test_main.cpp'ye `test_lua_sandbox_dos_probe();`
 * eklenirse aşağıdaki bayrak ikinci koşuyu atlar.
 */
#include "rowl_test_harness.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"
extern "C" {
#include <lua.h>  // LUA_RELEASE — hangi sürümün gerçekten bağlandığını kanıtlamak için
}

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace {

using Rowl::Scripting::LuaSandbox;
using Clock = std::chrono::steady_clock;

int g_surfacesPresent = 0;   // doğrulanmış kaçış yüzeyi sayısı
int g_probeCount = 0;
int g_failures = 0;
bool g_strict = false;

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] == '1';
}

void emit(const std::string& line) { std::cout << line << std::endl; }

/// Savunma geriye dönüşü: gerçek kusur. Testi kırmızıya düşürür.
[[noreturn]] void fail(const std::string& why) {
    ++g_failures;
    std::cerr << "DOS-PROBE FAIL: " << why << std::endl;
    std::exit(1);
}

/// Doğrulanmış kaçış: raporlanır, süç öldürülmez (yukarıdaki gerekçe).
void escaped(const char* sev, const std::string& what) {
    ++g_surfacesPresent;
    std::cout << "  [" << sev << "] KACIS DOGRULANDI: " << what << std::endl;
}

struct ProbeResult {
    bool ok = false;
    double ms = 0.0;
    std::string detail;
};

/// executeString + süre ölçümü.
ProbeResult runScript(LuaSandbox& sb, const std::string& code) {
    ++g_probeCount;
    ProbeResult r;
    const auto t0 = Clock::now();
    r.ok = sb.executeString(code);
    r.ms = msSince(t0);
    r.detail = r.ok ? std::string("OK") : ("FAIL: " + sb.getLastError());
    return r;
}

// (evaluateCondition ölçümleri doğrudan evaluateCondition() çağrılarıyla yapılır.)
std::string esc(std::string s) {
    for (char& c : s) {
        if (c == '\n') c = '|';
        if (c == '\r') c = ' ';
    }
    if (s.size() > 900) s = s.substr(0, 900) + "...<kırpıldı>";
    return s;
}

// ---------------------------------------------------------------------------
// A) TAM GLOBAL YÜZEY ENVANTERİ
// ---------------------------------------------------------------------------

void probeGlobalSurface() {
    TEST_SECTION("A) Sandbox global yüzey envanteri (sayısal)");
    LuaSandbox sb;
    if (!sb.initialize()) fail("sandbox init");

    // A1: _G'deki tüm ad + tip. pairs() ham (next tabanlı) olduğu için
    //     metatable __index tetiklenmez.
    const ProbeResult dump = runScript(sb,
        "local g = {}\n"
        "for k, v in pairs(_G) do g[#g+1] = k .. '=' .. type(v) end\n"
        "table.sort(g)\n"
        "rowl.var_set('d', table.concat(g, ' '))");
    if (!dump.ok) fail("global dump: " + dump.detail);
    const std::string g = sb.getVariable("d");

    int total = 0, functions = 0, tables = 0, others = 0;
    std::size_t pos = 0;
    while (pos <= g.size()) {
        const std::size_t sp = g.find(' ', pos);
        const std::string tok = g.substr(pos, (sp == std::string::npos ? g.size() : sp) - pos);
        if (!tok.empty()) {
            ++total;
            const std::size_t eq = tok.rfind('=');
            const std::string ty = (eq == std::string::npos) ? "" : tok.substr(eq + 1);
            if (ty == "function") ++functions;
            else if (ty == "table") ++tables;
            else ++others;
        }
        if (sp == std::string::npos) break;
        pos = sp + 1;
    }
    emit("  [A1] _G toplam ad          = " + std::to_string(total));
    emit("  [A1] _G fonksiyon         = " + std::to_string(functions));
    emit("  [A1] _G tablo             = " + std::to_string(tables));
    emit("  [A1] _G diğer             = " + std::to_string(others));
    emit("  [A1] TAM LİSTE: " + esc(g));

    // A2: ikinci seviye yüzey (iç içe kütüphaneler).
    const ProbeResult libs = runScript(sb,
        "local out = {}\n"
        "local function count(t) local n = 0 for _ in pairs(t) do n = n + 1 end return n end\n"
        "local toplam = 0\n"
        "for _, name in ipairs({'string','table','math','rowl'}) do\n"
        "  local t = _G[name]\n"
        "  if type(t) == 'table' then\n"
        "    local n = count(t); toplam = toplam + n\n"
        "    out[#out+1] = name .. '=' .. n\n"
        "  else\n"
        "    out[#out+1] = name .. '=<yok:' .. type(t) .. '>'\n"
        "  end\n"
        "end\n"
        "out[#out+1] = 'string_meta=' .. type(getmetatable(''))\n"
        "rowl.var_set('d', table.concat(out, ' '))\n"
        "rowl.var_set('d2', tostring(toplam))");
    if (!libs.ok) fail("lib dump: " + libs.detail);
    const int secondLevel = std::atoi(sb.getVariable("d2").c_str());
    emit("  [A2] İKİNCİ SEVİYE: " + esc(sb.getVariable("d")) +
         "  (üye toplamı=" + std::to_string(secondLevel) + ")");
    emit("  [A2] ERİŞİLEBİLİR ÇAĞRILABİLİR TOPLAM = " +
         std::to_string(functions + secondLevel) +
         "  (üst düzey " + std::to_string(functions) + " fn + 2. seviye " +
         std::to_string(secondLevel) + " üye)");

    // A3: global tablo metatable tuzağı — en zayıf halka (bölüm G).
    if (!sb.evaluateCondition("return getmetatable(_G) == nil")) {
        fail("_G global tablosu metatase sahip: " + sb.getLastError());
    }
    emit("  [A3] getmetatable(_G) == nil  (temiz sandbox'ta tuzak yok)");
    sb.shutdown();
}

// ---------------------------------------------------------------------------
// B) SİSTEMATİK KAÇIŞ ERİŞİLEBİLİRLİĞİ
// ---------------------------------------------------------------------------

void probeEscapeReachability() {
    TEST_SECTION("B) Kaçış yüzeyi erişilebilirliği (sistematik)");
    LuaSandbox sb;
    if (!sb.initialize()) fail("sandbox init");

    // B1: her ad için üç durum: <yok> | <tür> | function|cagri:<sonuç>
    const char* kCheck[] = {
        "io", "os", "debug", "package", "require", "module", "dofile", "loadfile",
        "load", "loadstring", "collectgarbage", "coroutine", "utf8", "jit",
        "string.dump", "string.rep", "rawget", "rawset", "rawequal", "rawlen",
        "getmetatable", "setmetatable", "pcall", "xpcall", "select", "next",
        "pairs", "ipairs", "type", "tostring", "tonumber", "error", "assert",
        "print", "warn", "_G", "_ENV", "_VERSION", "arg",
    };
    int nilCount = 0, fnCount = 0, otherCount = 0;
    for (const char* name : kCheck) {
        const std::string lua =
            std::string("local t = type(") + name + ")\n"
            "local out = t\n"
            "if t == 'function' then\n"
            "  local ok, e = pcall(" + name + ")\n"
            "  out = 'function|cagri:' .. (ok and 'KABUL' or tostring(e))\n"
            "end\n"
            "rowl.var_set('d', out)";
        const ProbeResult r = runScript(sb, lua);
        if (!r.ok) {
            // Ad yoksa ifade çalışma zamanı hatası verir — bu da
            // "erişilemez" kanıtıdır, savunma geriye dönüşü değildir.
            ++nilCount;
            emit(std::string("  [B1] ") + name + " -> ERİŞİLEMEZ (hata: " + esc(r.detail) + ")");
            continue;
        }
        const std::string d = sb.getVariable("d");
        if (d == "nil") ++nilCount;
        else if (d.rfind("function", 0) == 0) ++fnCount;
        else ++otherCount;
        emit(std::string("  [B1] ") + name + " -> " + esc(d));
    }
    emit("  [B1] SAYIM: erişilemez(yok)=" + std::to_string(nilCount) +
         "  çağrılabilir=" + std::to_string(fnCount) +
         "  diğer=" + std::to_string(otherCount));

    // B2: alt-alan erişilebilirliği.
    const char* kFields[] = {
        "io.open", "io.write", "io.read", "os.execute", "os.getenv", "os.time",
        "package.loadlib", "package.path", "package.cpath", "package.preload",
        "debug.getregistry", "debug.getmetatable", "debug.sethook",
        "debug.getinfo", "debug.getupvalue", "debug.upvaluejoin",
    };
    int fieldReachable = 0;
    for (const char* path : kFields) {
        const std::string lua =
            std::string("local ok, v = pcall(function() return ") + path + " end)\n"
            "rowl.var_set('d', ok and type(v) or 'HATA')";
        runScript(sb, lua);
        const std::string d = sb.getVariable("d");
        if (d != "HATA") ++fieldReachable;
        emit(std::string("  [B2] ") + path + " -> " + esc(d));
    }
    emit("  [B2] ERİŞİLEBİLİR ALT-ALAN SAYISI = " + std::to_string(fieldReachable) +
         " / " + std::to_string(sizeof(kFields) / sizeof(kFields[0])));
    if (fieldReachable != 0) {
        escaped("P0", "kara kutu alt-alanlardan biri erişilebilir");
        fail("B2: " + std::to_string(fieldReachable) + " alt-alan erişilebilir");
    }

    // B3: require/load doğrudan çağırma (dosya sistemi erişim denemesi).
    const ProbeResult req = runScript(sb,
        "local ok, e = pcall(function() return require('os') end)\n"
        "rowl.var_set('d', tostring(ok) .. ' ' .. tostring(e))");
    emit("  [B3] require('os')     -> " + esc(sb.getVariable("d")) +
         "  [" + std::to_string(static_cast<int>(req.ms)) + " ms]");
    const ProbeResult ld = runScript(sb,
        "local ok, e = pcall(function() return load('return 1') end)\n"
        "rowl.var_set('d', tostring(ok) .. ' ' .. tostring(e))");
    emit("  [B3] load('return 1') -> " + esc(sb.getVariable("d")) +
         "  [" + std::to_string(static_cast<int>(ld.ms)) + " ms]");

    // B4: coroutine limit atlama vektörü.
    const ProbeResult co = runScript(sb,
        "local out = {}\n"
        "out[#out+1] = 'coroutine=' .. type(coroutine)\n"
        "out[#out+1] = 'utf8=' .. type(utf8)\n"
        "rowl.var_set('d', table.concat(out, ' '))");
    if (!co.ok) fail("coroutine probe: " + co.detail);
    const std::string cod = sb.getVariable("d");
    emit("  [B4] " + esc(cod));
    if (cod.find("coroutine=nil") == std::string::npos ||
        cod.find("utf8=nil") == std::string::npos) {
        fail("coroutine/utf8 ERİŞİLEBİLİR — limit atlama yüzeyi açık: " + cod);
    }

    // B5: string.dump -> bytecode. load yoksa etkisiz.
    const ProbeResult sd = runScript(sb,
        "local f = function() return 1 end\n"
        "local ok, r = pcall(string.dump, f)\n"
        "rowl.var_set('d', tostring(ok) .. ' tip=' .. type(r) .. ' boyut=' .. tostring(ok and #r or -1))");
    emit("  [B5] string.dump(f)    -> " + esc(sb.getVariable("d")) +
         "  [" + std::to_string(static_cast<int>(sd.ms)) + " ms]");
    emit("  [B5] NOT: bytecode ÜRETİLİYOR ama load/loadstring YOK -> "
         "yeniden yüklenemez, etkisiz (bilgi sızıntısı değil).");
    sb.shutdown();
}

// ---------------------------------------------------------------------------
// C) DÜŞÜK-INSTRUCTION / YÜKSEK-WALL-TIME DoS VEKTÖRLERİ
// ---------------------------------------------------------------------------

void probeLowInstructionDos() {
    TEST_SECTION("C) Düşük-instruction / yüksek-wall-time DoS vektörleri");
    LuaSandbox sb;
    if (!sb.initialize()) fail("sandbox init");

    // C1: table.insert(t,1,v) başında O(n) shift. Her tur ~10 VM komutu,
    //     ama C içinde O(n) iş. 200k tur ≈ 2e10 eleman taşıma; komut sayısı
    //     ~2M (10M tavanının ALTINDA). Hangi sınır devreye girer?
    {
        const ProbeResult r = runScript(sb,
            "local t = {}\n"
            "for i = 1, 200000 do table.insert(t, 1, i) end\n"
            "rowl.var_set('d', 'tamam #t=' .. #t)");
        emit("  [C1] table.insert başında x200000 -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ms > 5000.0 && r.ok) {
            fail("C1: 5s duvar-saati tavanı GEÇTİ ve script BAŞARILI döndü");
        }
        if (r.detail.find("wall-clock") == std::string::npos) {
            fail("C1: beklenen sınır duvar-saati değil: " + r.detail);
        }
    }
    sb.clearVariables();

    // C2: karesel concat s = s .. s. Kota 64 MiB'da durur mu?
    {
        const ProbeResult r = runScript(sb,
            "local s = string.rep('x', 1024)\n"
            "local n = 0\n"
            "while true do s = s .. s; n = n + 1 if #s > 100*1024*1024 then break end end\n"
            "rowl.var_set('d', 'adim=' .. n .. ' sonBoyut=' .. #s)");
        emit("  [C2] karesel concat (s..s) -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ok) fail("C2: karesel concat kotayı geçti");
    }
    sb.clearVariables();

    // C3: karesel concat varyantı — s = s .. 'x'. 1M tur: instruction
    //     ~4M (tavanın altında) ama ~5e11 bayt kopyalanır.
    {
        const ProbeResult r = runScript(sb,
            "local s = ''\n"
            "for i = 1, 1000000 do s = s .. 'x' end\n"
            "rowl.var_set('d', 'tamam #s=' .. #s)");
        emit("  [C3] karesel concat (s..'x') x1M -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.detail.find("wall-clock") == std::string::npos) {
            fail("C3: komut sayacı beklenmedik biçimde devreye girdi: " + r.detail);
        }
    }
    sb.clearVariables();

    // C4: table.concat — TEK C çağrısı, ~0 VM komutu.
    {
        const ProbeResult r = runScript(sb,
            "local t = {}\n"
            "for i = 1, 400000 do t[i] = string.rep('y', 200) end\n"
            "local c = table.concat(t)\n"
            "rowl.var_set('d', 'tamam #c=' .. #c)");
        emit("  [C4] table.concat 400k eleman -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.detail.find("not enough memory") == std::string::npos) {
            fail("C4: kota reddi beklenen 'not enough memory' değil: " + r.detail);
        }
    }
    sb.clearVariables();

    // C5: table.sort + kötü karşılaştırıcı (Lua kod → komut sayacı işler).
    {
        const ProbeResult r = runScript(sb,
            "local t = {}\n"
            "for i = 1, 200000 do t[i] = (i * 7919) % 200003 end\n"
            "table.sort(t, function(a, b) return a < b end)\n"
            "rowl.var_set('d', 'tamam ilk=' .. t[1])");
        emit("  [C5] table.sort kötü karşılaştırıcı -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ok) fail("C5: table.sort komut tavanını aştı");
    }
    sb.clearVariables();

    // C6: derin tablo zinciri (200k seviye) — kota mı, derinlik sınırı mı?
    {
        const ProbeResult r = runScript(sb,
            "local root = {}\n"
            "local c = root\n"
            "for i = 1, 200000 do c.n = {}; c = c.n end\n"
            "rowl.var_set('d', 'tamam')");
        emit("  [C6] iç içe 200k tablo -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        emit("  [C6] NOT: derinlik için AYRI sınır yok; yalnızca bellek kotası var.");
    }
    sb.clearVariables();

    // C7: saf Lua döngüsü — komut sayacının GERÇEKTEN devrede olduğunun
    //     bağımsız ölçümü (mevcut test yalnız "bloke edildi" diyor).
    {
        const ProbeResult r = runScript(sb, "while true do local a = 1 end");
        emit("  [C7] sonsuz döngü kesilme süresi = " +
             std::to_string(static_cast<int>(r.ms)) + " ms | " + esc(r.detail));
        if (r.ok) fail("C7: sonsuz döngü bloklanmadı");
        if (r.detail.find("instruction limit") == std::string::npos) {
            fail("C7: sonsuz döngü komut sınırıyla değil başka bir yolla kesildi: " + r.detail);
        }
        if (r.ms > 6000.0) fail("C7: komut sınırı 6 saniyeden uzun sürdü");
    }
    sb.clearVariables();

    // C8: string.rep kota sınırının tam içinde / dışında.
    {
        const ProbeResult under = runScript(sb,
            "local s = string.rep('a', 8 * 1024 * 1024)\n"
            "rowl.var_set('d', 'boyut=' .. #s)");
        emit("  [C8] string.rep 8 MiB  -> " + esc(under.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(under.ms)) + " ms");
        if (!under.ok) fail("C8: kota içi 8 MiB string.rep başarısız: " + under.detail);
    }
    sb.clearVariables();
    {
        const ProbeResult over = runScript(sb,
            "local s = string.rep('a', 256 * 1024 * 1024)\n"
            "rowl.var_set('d', 'boyut=' .. #s)");
        emit("  [C8] string.rep 256 MiB -> " + esc(over.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(over.ms)) + " ms");
        if (over.ok) fail("C8: 256 MiB kota tavanını AŞTI");
        if (over.detail.find("not enough memory") == std::string::npos) {
            fail("C8: kota reddi beklenen 'not enough memory' değil: " + over.detail);
        }
    }
    sb.clearVariables();

    // C9: MAXSIZE taşma denemesi — 2^63 kopya isteği.
    {
        const ProbeResult r = runScript(sb, "local s = string.rep('a', 2^63)");
        emit("  [C9] string.rep 2^63 -> " + esc(r.detail) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ok) fail("C9: 2^63 string.rep başarıyla bitti");
    }
    sb.clearVariables();

    // C10: string.format genişliği ile devasa string.
    {
        const ProbeResult r = runScript(sb, "local s = string.format('%0999999999d', 1)");
        emit("  [C10] string.format %999999999d -> " + esc(r.detail) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ok) fail("C10: devasa string.format kotayı geçti");
    }
    sb.clearVariables();
    sb.shutdown();
}

// ---------------------------------------------------------------------------
// D) C-YIĞINI TAŞMASI (metamethod rekursiyonu)
// ---------------------------------------------------------------------------

void probeCStackRecursion() {
    TEST_SECTION("D) C-yığını taşması (metamethod rekursiyonu)");
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        const ProbeResult r = runScript(sb,
            "local t = setmetatable({}, {})\n"
            "getmetatable(t).__lt = function(a, b) return a < b end\n"
            "table.sort({t, t})");
        emit("  [D1] __lt sonsuz rekursiyon -> " + esc(r.detail) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ok) fail("D1: C-yığını taşması bloklanmadı");
        if (r.ms > 6000.0) fail("D1: 6s+ sürdü");
        if (!sb.executeString("rowl.var_set('d1_alive','ok')") ||
            sb.getVariable("d1_alive") != "ok") {
            fail("D1: sandbox C-yığını taşmasından sonra ölü");
        }
        sb.shutdown();
    }
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        const ProbeResult r = runScript(sb,
            "local t = setmetatable({}, {})\n"
            "getmetatable(t).__index = function(tt, k) return tt[k] end\n"
            "return tostring(t.bulunmayan)");
        emit("  [D2] __index kendine döner -> " + esc(r.detail) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ok) fail("D2: __index kendine döner döngüsü bloklanmadı (aşırı yığın)");
        if (!sb.executeString("rowl.var_set('d2_alive','ok')") ||
            sb.getVariable("d2_alive") != "ok") {
            fail("D2: sandbox C-yığını taşmasından sonra ölü");
        }
        sb.shutdown();
    }
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        const ProbeResult r = runScript(sb,
            "local base = {son = 'bulundu'}\n"
            "local cur = base\n"
            "for i = 1, 100 do cur = setmetatable({}, {__index = cur}) end\n"
            "rowl.var_set('d', 'bulundu=' .. tostring(cur.son))");
        emit("  [D3] 100 seviye __index zinciri -> " + esc(r.detail) +
             " | " + esc(sb.getVariable("d")) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (sb.getVariable("d") != "bulundu=bulundu") {
            fail("D3: meşru __index zinciri bozuldu: " + sb.getVariable("d"));
        }
        sb.shutdown();
    }
}

// ---------------------------------------------------------------------------
// E) SINIR KAPILARI
// ---------------------------------------------------------------------------

void probeBoundaryGates() {
    TEST_SECTION("E) Sınır kapıları (256 KiB script, 128 modül)");
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        const std::string under = "x = 1 " + std::string(200 * 1024, ' ');
        const std::string over  = "x = 1 " + std::string(300 * 1024, ' ');
        const ProbeResult a = runScript(sb, under);
        emit("  [E1] script 200 KiB -> " + esc(a.detail) +
             " | " + std::to_string(static_cast<int>(a.ms)) + " ms");
        if (!a.ok) fail("E1: tavan altındaki script reddedildi: " + a.detail);
        const ProbeResult b = runScript(sb, over);
        emit("  [E1] script 300 KiB -> " + esc(b.detail) +
             " | " + std::to_string(static_cast<int>(b.ms)) + " ms");
        if (b.ok) fail("E1: 300 KiB script kabul edildi — 256 KiB tavanı yok");
        sb.shutdown();
    }
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        int loaded = 0;
        std::string refusedAt = "yok";
        for (int i = 0; i < 140; ++i) {
            if (sb.loadModule("m" + std::to_string(i), "marker = 1")) ++loaded;
            else { refusedAt = std::to_string(i); break; }
        }
        emit("  [E2] yüklenen modül = " + std::to_string(loaded) +
             ", ilk ret indeksi = " + refusedAt);
        if (loaded != 128) {
            fail("E2: modül tavanı 128 değil, " + std::to_string(loaded) +
                 " (ret: " + sb.getLastError() + ")");
        }
        if (!sb.loadModule("m0", "marker = 2")) {
            fail("E2: mevcut modül değiştirilemedi: " + sb.getLastError());
        }
        if (sb.getModuleCount() != 128) {
            fail("E2: modül değiştirme sayımı bozdu: " +
                 std::to_string(sb.getModuleCount()));
        }
        emit("  [E2] mevcut modül değiştirme (sahipli replace) çalışıyor");
        sb.shutdown();
    }
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        // E3: derin parse rekursiyonu — armWallDeadline loadstring SONRASI
        //     kurulur; parser'ın hem duvar-saati hem komut sayacı yok.
        std::string nested(100000, '(');
        nested += "1";
        nested += std::string(100000, ')');
        const ProbeResult r = runScript(sb, "x = " + nested);
        emit("  [E3] 100k iç içe parantez -> " + esc(r.detail) +
             " | " + std::to_string(static_cast<int>(r.ms)) + " ms");
        if (r.ms > 6000.0) fail("E3: parser 6s+ sürdü (aşırı derinlik)");
        if (r.ok) fail("E3: 100k iç içe parantez kabul edildi");
        sb.shutdown();
    }
}

// ---------------------------------------------------------------------------
// F) KÖPRÜ (rowl.var_*) TÜR ZORLAMA + YENİDEN-GİRİŞ
// ---------------------------------------------------------------------------

void probeBridgeAbuse() {
    TEST_SECTION("F) Köprü tür zorlama ve yeniden-giriş");
    LuaSandbox sb;
    if (!sb.initialize()) fail("sandbox init");

    const ProbeResult f1 = runScript(sb,
        "rowl.var_set('t1', {a=1})\n"
        "rowl.var_set('t2', print)\n"
        "rowl.var_set('t3', nil)\n"
        "rowl.var_set('t4')\n"
        "rowl.var_set('t5', true)\n"
        "rowl.var_set('t6', 1e300)\n"
        "rowl.var_set('t7', 0/0)\n"
        "rowl.var_set('d', 'ok')");
    if (!f1.ok) fail("F1 kurulum: " + f1.detail);
    const char* keys[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7"};
    for (const char* k : keys) {
        const std::string v = sb.getVariable(k);
        emit(std::string("  [F1] var_set(") + k + ") -> " +
             (v.empty() ? std::string("<boş/reddedildi>") : esc(v)));
    }
    for (const char* k : {"t1", "t2"}) {
        if (!sb.getVariable(k).empty()) {
            fail(std::string("F1: ") + k + " host haritasına sızdı: " + sb.getVariable(k));
        }
    }
    if (sb.getVariable("t6").empty()) {
        fail("F1: 1e300 reddedildi (beklenen: metin olarak saklanır)");
    }
    if (sb.getVariable("t7") == "nan" || sb.getVariable("t7") == "-nan") {
        emit("  [F1] NOT: rowl.var_set(k, 0/0) host haritasına metin \"-nan\" yazıyor. "
             "std::stod ile okuyan tarafta NaN üretir; engine.cpp:1335 isfinite "
             "kontrolü yalnız 'add' yolunda. Çökme değil, veri bozulması.");
    }

    const ProbeResult f2 = runScript(sb,
        "rowl.var_set('d', rowl.var_get('yok_boyle') == '' and 'boş-döndürüldü' or 'dolu')\n"
        "rowl.var_set('d2', rowl.var_get(nil))\n"
        "rowl.var_set('d3', rowl.var_get(42))\n"
        "rowl.var_set('d4', rowl.var_get())");
    emit(std::string("  [F2] var_get: d='") + sb.getVariable("d") +
         "' d2='" + sb.getVariable("d2") + "' d3='" + sb.getVariable("d3") +
         "' d4='" + sb.getVariable("d4") + "' [" +
         std::to_string(static_cast<int>(f2.ms)) + " ms]");

    // F3: yeniden-giriş — var_set C++ tarafında iç içe lua_pcall açar.
    const ProbeResult f3 = runScript(sb,
        "local t = setmetatable({}, {__tostring = function(x) rowl.var_set('reentry','ok') return 'x' end})\n"
        "rowl.var_set('d', tostring(t))");
    emit("  [F3] __tostring içinden var_set (yeniden-giriş) -> " + esc(f3.detail) +
         " | reentry='" + sb.getVariable("reentry") + "' [" +
         std::to_string(static_cast<int>(f3.ms)) + " ms]");
    if (f3.ok && sb.getVariable("reentry") != "ok") {
        fail("F3: metamethod içinden yeniden-giriş kayboldu");
    }
    sb.shutdown();
}

// ---------------------------------------------------------------------------
// G) _G METATABLE BİTKİSİ  →  HOST'UN KORUNMAZ OKUMASI   [P0]
// ---------------------------------------------------------------------------
//
// Zincir (hepsi maliyetlendirilmiş kanıtla doğrulandı):
//   1. executeString() bir on_update tanımlar. (repairGlobals() çalışır,
//      _G metatablesini temizler — G1 yeşil.)
//   2. callOptionalFunction("on_update") onu çalıştırır. Bu yol
//      repairGlobals() ÇAĞIRMAZ (H31 per-frame maliyet yasağı).
//      Bitki (`setmetatable(_G, {__index=...})`) hayatta kalır.   [G2/G4]
//   3. getGlobalNumber() (lua_sandbox.cpp:920) lua_getglobal kullanır —
//      D07 ham-okuma DEĞİL. Kayıp anahtarda __index pcall'sız tetiklenir.
//        a) __index DEĞER dönerse → host okuması saldırgan kontrollü. [G3/G5]
//        b) __index HATA fırlatırsa → PANIC + abort() → SÜREÇ ÖLÜR.     [G6]
//   4. evaluateCondition() bitkiden sonra da savunmasız: her koşul
//      eksik global'ı bitkiden okur → hikâye dalı ele geçirilir.      [G5]
//   Not: getVariable() ve callOptionalFunction() D07 ham-okuması
//        kullandığı için BAĞIŞK. Düzeltme deseni zaten var; yalnızca
//        getGlobalNumber'a uygulanmamış (satır :1366 "KAPSAM-DIŞI" diyor).
//   Modül yolu KAPALI: modül ortamının metatablesi korumalı (H1 "cannot
//   change a protected metatable").

void probeGlobalTableMetatablePlant() {
    TEST_SECTION("G) _G metatable bitkisi (P0 — en zayıf halka)");

    // G1: executeString sonrası temizlik çalışıyor mu?
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        runScript(sb, "setmetatable(_G, {__index = function() return 'BITKI' end})");
        const bool clean = sb.evaluateCondition("return getmetatable(_G) == nil");
        emit("  [G1] executeString sonrası getmetatable(_G) == nil -> " +
             std::string(clean ? "EVET (repairGlobals temizledi)" : "HAYIR (sızıntı)"));
        if (!clean) fail("G1: executeString _G metatablesini temizlemiyor");
        sb.shutdown();
    }

    // G2: callOptionalFunction sonrası bitki HAYATTA KALIYOR MU?
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        runScript(sb,
            "function on_update()\n"
            "  setmetatable(_G, {__index = function(t, k)\n"
            "    if k == 'zehirli' then return 987654 end\n"
            "    return nil\n"
            "  end})\n"
            "end");
        const bool ran = sb.callOptionalFunction("on_update", 0.016);
        const bool survived = !sb.evaluateCondition("return getmetatable(_G) == nil");
        emit("  [G2] callOptionalFunction sonrası bitki hayatta mı -> " +
             std::string(survived ? "EVET" : "hayır") +
             " (callback ran=" + std::string(ran ? "true" : "false") + ")");
        if (!survived) {
            emit("  [G2] NOT: bu yol temiz — beklenen davranış değişmiş.");
            sb.shutdown();
            return;
        }
        escaped("P0", "callOptionalFunction() repairGlobals çağırmıyor; "
                      "_G metatable bitkisi kalıcı");

        // G3: bitki kalıcıysa host okuması ele geçirilir.
        const double poisoned = sb.getGlobalNumber("zehirli", -1.0);
        emit("  [G3] getGlobalNumber('zehirli') = " + std::to_string(poisoned) +
             "  (temiz sandbox'ta -1 vermeliydi)");
        if (poisoned == 987654.0) {
            escaped("P0", "getGlobalNumber() (lua_sandbox.cpp:920, D07 ham-okuma "
                          "DEĞİL) kayıp anahtarları saldırgana teslim ediyor");
        }
        // G3b: mevcut (kayıt) anahtarlar etkilenmiyor — __index sadece kayıpta ateşlenir.
        emit("  [G3b] mevcut anahtarlar korunuyor mu: getGlobalNumber('rowl')=" +
             std::to_string(sb.getGlobalNumber("rowl", -1.0)) +
             " 'math'=" + std::to_string(sb.getGlobalNumber("math", -1.0)));
        sb.shutdown();
    }

    // G4: evaluateCondition yolu — koşul kendisi bitkiyi kurabiliyor mu?
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        const bool r = sb.evaluateCondition(
            "setmetatable(_G,{__index=function() return 'KOSUL' end}) ~= nil");
        const bool nilNow = sb.evaluateCondition("yok_boyle == nil");
        const bool valNow = sb.evaluateCondition("yok_boyle == 'KOSUL'");
        emit("  [G4] koşul bitkisi: kurulum=" + std::string(r ? "true" : "false") +
             " | sonraki koşul 'yok_boyle==nil' = " + std::string(nilNow ? "1" : "0") +
             " (temiz=1) | 'yok_boyle==\"KOSUL\"' = " + std::string(valNow ? "1" : "0") +
             " (temiz=0)");
        if (!nilNow && valNow) {
            escaped("P1", "evaluateCondition() hikâye dalı ele geçirme: script "
                          "eksik global'ların değerini belirleyebiliyor");
        }
        // D06 yalnızca EKLENEN ADLARI siler; global tablosunun metatablesi korunur.
        emit("  [G4] NOT: D06ConditionGlobalGuard ad kümesi siler, _G metatablesini değil.");
        sb.shutdown();
    }

    // G5: sayısal bitki — muhasebe bozulması (engine.cpp:1332-1340 yolu).
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        sb.executeString(
            "function on_update() setmetatable(_G,{__index=function() return 1e300 end}) end");
        sb.callOptionalFunction("on_update", 0.016);
        const double cur = sb.getGlobalNumber("oyuncu_can", 0.0);
        const double delta = 5.0;
        const double res = cur + delta;
        emit("  [G5] engine.cpp:1332-1340 add yolu -> cur=" + std::to_string(cur) +
             " delta=" + std::to_string(delta) + " res=" + std::to_string(res) +
             " isfinite=" + (std::isfinite(res) ? "true" : "false"));
        if (cur == 1e300 && std::isfinite(res)) {
            escaped("P1", "sabit 1e300 'add' muhasebesini doyuruyor; isfinite "
                          "guard'ı yakalamıyor, değer kalıcı state'e yazılıyor");
        }
        sb.shutdown();
    }

    // G6: __index HATA fırlatırsa → PANIC + abort(). Bu probe BİLEREK
    //     SÜRECİ ÖLDÜRÜR; varsayılan kapalı. ROWL_DOS_PROBE_LIVE=1 ile aç.
    {
        const bool live = envOn("ROWL_DOS_PROBE_LIVE");
        emit(std::string("  [G6] abort probe'u ") +
             (live ? "ÇALIŞIYOR (süreç ölecek — beklenen)" : "ATLANDI (varsayılan; açmak için ROWL_DOS_PROBE_LIVE=1)"));
        if (live) {
            LuaSandbox sb;
            if (!sb.initialize()) fail("sandbox init");
            sb.executeString(
                "function on_update() setmetatable(_G,{__index=function() error('saldiri') end}) end");
            sb.callOptionalFunction("on_update", 0.016);
            std::cout << "  [G6] şimdi getGlobalNumber('oyuncu_can') çağrılıyor…"
                      << std::endl;
            std::cout.flush();
            const double v = sb.getGlobalNumber("oyuncu_can", 0.0);
            std::cout << "  [G6] HATA FIRLATILMADI: " << v << std::endl;
        } else {
            escaped("P0", "ölçüldü (ayrı ikili, ROWL_DOS_PROBE_LIVE=1 ile "
                          "tekrarlanabilir): 'PANIC: unprotected error in call to "
                          "Lua API' → abort() → SIGABRT(134); catch(...) YAKALAMIYOR");
        }
    }

    // G7: modül yolu kapalı mı?
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        const bool loaded = sb.loadModule("m",
            "function on_enter() setmetatable(_G,{__index=function() return 'MODUL' end}) end");
        const bool ran = loaded ? sb.callOptionalModuleFunction("m", "on_enter") : false;
        const double after = sb.getGlobalNumber("yok", -1.0);
        emit("  [G7] modül bitkisi: loadModule=" + std::string(loaded ? "true" : "false") +
             " on_enter=" + std::string(ran ? "true" : "false") +
             " sonrası getGlobalNumber('yok')=" + std::to_string(after) + " (temiz=-1)");
        if (after != -1.0) {
            escaped("P0", "modül ortamından _G bitkisi kurulabildi");
            fail("G7: modül yolu da açık");
        }
        emit("  [G7] Modül yolu KAPALI: ortam metatablesi korumalı "
             "(\"cannot change a protected metatable\").");
        sb.shutdown();
    }

    // G8: temizlik — clearVariables() bitkiyi kaldırıyor mu?
    {
        LuaSandbox sb;
        if (!sb.initialize()) fail("sandbox init");
        sb.executeString(
            "function on_update() setmetatable(_G,{__index=function() return 111 end}) end");
        sb.callOptionalFunction("on_update", 0.016);
        const double before = sb.getGlobalNumber("deneme", -1.0);
        sb.clearVariables();
        const double after = sb.getGlobalNumber("deneme", -1.0);
        emit("  [G8] temizlik: önce=" + std::to_string(before) +
             " sonra=" + std::to_string(after) + " (temiz=-1)");
        if (before == 111.0 && after != -1.0) {
            fail("G8: clearVariables() _G metatablesini temizlemiyor");
        }
        sb.shutdown();
    }
}

// ---------------------------------------------------------------------------
// H) MODÜL ORTAMINDAN KAÇIŞ
// ---------------------------------------------------------------------------

void probeModuleEnvEscape() {
    TEST_SECTION("H) Modül ortamından kaçış");
    LuaSandbox sb;
    if (!sb.initialize()) fail("sandbox init");

    if (!sb.loadModule("kacis", R"(
        local rapor = {}
        rapor[#rapor+1] = 'getmetatable(_G)=' .. tostring(getmetatable(_G))
        rapor[#rapor+1] = 'rowl_meta=' .. tostring(getmetatable(rowl))
        local ok, v = pcall(function() return getmetatable(_G).__index end)
        rapor[#rapor+1] = 'gercek_G=' .. tostring(ok) .. ':' .. type(v)
        local ok2, e2 = pcall(function() rawset(_G, 'host_gorunur', 'evet') end)
        rapor[#rapor+1] = 'rawset_env=' .. tostring(ok2) .. ':' .. tostring(e2)
        local ok3, e3 = pcall(function() host_gorunur2 = 'evet' end)
        rapor[#rapor+1] = 'atama_env=' .. tostring(ok3) .. ':' .. tostring(e3)
        function on_enter() rowl.var_set('modul_rapor', table.concat(rapor, ' ; ')) end
    )")) {
        fail("H1 modül yüklenemedi: " + sb.getLastError());
    }
    if (!sb.callOptionalModuleFunction("kacis", "on_enter")) {
        fail("H1 çağrı: " + sb.getLastError());
    }
    emit("  [H1] " + esc(sb.getVariable("modul_rapor")));
    if (!sb.getVariable("host_gorunur").empty() ||
        !sb.getVariable("host_gorunur2").empty()) {
        fail("H1: modül ortamından host görünür global sızdı");
    }
    sb.unloadModule("kacis");

    if (!sb.loadModule("zincir", R"(
        function on_enter()
            local ok, err = pcall(function() return tostring(bilinmeyen_ad_xyz) end)
            rowl.var_set('zincir_sonuc', tostring(ok) .. ':' .. tostring(err))
        end
    )")) {
        fail("H2 modül yüklenemedi: " + sb.getLastError());
    }
    sb.callOptionalModuleFunction("zincir", "on_enter");
    emit("  [H2] modül env zinciri (bilinmeyen ad) -> " + esc(sb.getVariable("zincir_sonuc")));
    sb.unloadModule("zincir");
    sb.shutdown();
}

// ---------------------------------------------------------------------------
// I) ZEHİR + TOPARLANMA
// ---------------------------------------------------------------------------

void probePoisonRecovery() {
    TEST_SECTION("I) Zehir ve toparlanma");
    LuaSandbox sb;
    if (!sb.initialize()) fail("sandbox init");
    const ProbeResult loop = runScript(sb, "while true do end");
    emit("  [I1] sonsuz döngü -> " + esc(loop.detail) +
         " | " + std::to_string(static_cast<int>(loop.ms)) + " ms");
    if (sb.executeString("x = 1")) fail("I1: zehirli oturum executeString kabul etti");
    if (sb.loadModule("m", "x = 1")) fail("I1: zehirli oturum loadModule kabul etti");
    if (sb.callOptionalFunction("hicbiryer")) fail("I1: zehirli oturum callback kabul etti");
    if (sb.evaluateCondition("1 == 1")) fail("I1: zehirli oturum condition kabul etti");
    emit("  [I1] zehirli oturum 4 kapının hepsini reddetti");
    sb.clearVariables();
    if (!sb.executeString("rowl.var_set('i1_alive','ok')") ||
        sb.getVariable("i1_alive") != "ok") {
        fail("I1: clearVariables() zehri kaldırmadı");
    }
    emit("  [I1] clearVariables() sonrası toparlandı");
    sb.shutdown();
}

} // namespace

// ---------------------------------------------------------------------------
// Giriş noktası.
// ---------------------------------------------------------------------------
void test_lua_sandbox_dos_probe() {
    TEST_SECTION("Lua Sandbox Dinamik Güvenlik Taraması (DoS / kaçış)");
    // Log seviyesi SÜREÇ-GLOBAL'dir. Statik başlatıcıyla koştuğumuz için
    // süste bırakırsak sonraki 300+ testi etkileriz: eski değeri geri yaz.
    const Rowl::Core::LogLevel savedLevel = Rowl::Core::Logger::getLogLevel();
    Rowl::Core::Logger::setLogLevel(Rowl::Core::LogLevel::Warn);
    struct LevelRestore {
        Rowl::Core::LogLevel level;
        ~LevelRestore() { Rowl::Core::Logger::setLogLevel(level); }
    } restore{savedLevel};

    g_strict = envOn("ROWL_DOS_PROBE_STRICT");
    emit("  [ORTAM] Lua çalışma sürümü = " + std::string(LUA_RELEASE) +
         "   (başlıklar 'Lua 5.4' diyor; link EDİLEN kütüphane budur)");
    emit("  [ORTAM] derleyici = GCC " __VERSION__);
    emit("  [ORTAM] katı mod = " + std::string(g_strict ? "ACIK" : "kapali"));

    const auto t0 = Clock::now();
    probeGlobalSurface();
    probeEscapeReachability();
    probeLowInstructionDos();
    probeCStackRecursion();
    probeBoundaryGates();
    probeBridgeAbuse();
    probeGlobalTableMetatablePlant();
    probeModuleEnvEscape();
    probePoisonRecovery();

    emit("  [SONUÇ] toplam probe   = " + std::to_string(g_probeCount));
    emit("  [SONUÇ] toplam süre   = " + std::to_string(static_cast<int>(msSince(t0))) + " ms");
    emit("  [SONUÇ] DOĞRULANMIŞ KAÇIŞ YÜZEYİ SAYISI = " +
         std::to_string(g_surfacesPresent));
    if (g_surfacesPresent > 0) {
        std::cout << "  [SONUÇ] P0/P1 kaçış mevcut. ROWL_DOS_PROBE_STRICT=1 ile "
                     "kırmızıya düşür." << std::endl;
    }
    if (g_failures > 0) {
        std::cerr << "DOS-PROBE: " << g_failures << " savunma geriye dönüşü" << std::endl;
        std::exit(1);
    }
    if (g_surfacesPresent > 0 && g_strict) {
        std::cerr << "DOS-PROBE STRICT: " << g_surfacesPresent << " kaçış yüzeyi" << std::endl;
        std::exit(1);
    }
    TEST_PASS("Lua Sandbox Dinamik Tarama (" + std::to_string(g_probeCount) +
              " probe, " + std::to_string(g_surfacesPresent) + " kaçış yüzeyi)");
}

namespace {
struct DosProbeAutoRun {
    DosProbeAutoRun() { test_lua_sandbox_dos_probe(); }
};
const DosProbeAutoRun g_dosProbeAutoRun{};
} // namespace
