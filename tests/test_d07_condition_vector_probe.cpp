/**
 * test_d07_condition_vector_probe.cpp — D07 koşul-vektörü kilidi.
 *
 * Kapsam: lua_sandbox.cpp iki aralık — getVariable (:777-798) ve
 * callOptionalFunction (:1277-1315) içindeki çıplak lua_getglobal okumaları.
 * Üçüncü çıplak okuma (:850 getGlobalNumber) KAPSAM-DIŞIDIR.
 *
 * Delik (RED'i veren gözlem): koşul yolu onarım çalıştırmaz (per-frame yasak)
 * ve D06 ad-kümesi snapshot'ı bir _G METATABLE'ını göremez; o yüzden bir koşul
 *   (function() setmetatable(_G, {__index = function(t,k) ... end}); return true end)()
 * ile düşmanca _G __index eker ve bu plant kalıcı olur. Sonrasında kayıp-anahtar
 * okuyan her çıplak lua_getglobal (getVariable + callOptionalFunction
 * kayıp-callback çözümü) script kodu çalıştırır: kirlilik eker (ve hata fırlatan
 * varyant pcall-dışı okumada C++ çerçevelerinden geçerek UB'ye düşer).
 *
 * Prob (yalnızca C++ LuaSandbox + lua_sandbox.hpp; callOptionalFunction'ın
 * public C API karşılığı yoktur — D08 emsaliyle rowl_engine_objects'a bağlanır):
 *   RED  (pre-fix):  "PROB-DIRTY ..." + exit 1 (tuzak ateşlendi)
 *   GREEN (post-fix): "PROB-CLEAN ..." + exit 0
 *
 * Kontroller (fix-öncesi de YEŞİL olmalı; kırmızıysa kurulum bozuktur):
 *   gerçek global getVariable ile okunur, gerçek callback çalışır,
 *   kayıp callback başarılı no-op döner.
 * Temizlik kontrolü (yalnızca GREEN yolunda koşar — pre-fix'te çakılır):
 *   hata-fırlatan __index'e rağmen okumalar fırlatmaz (ham-read kanıtı).
 *
 * KIRMIZI-YEŞİL SÖZLEŞMESİ: yeşilde bu gözlemlerden herhangi biri değişirse
 * fix TURU sayılır (max 3); kırmızıda commit YOK.
 */
#include <iostream>
#include <string>

#include "rowl/scripting/lua_sandbox.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* label, const std::string& detail = "") {
    std::cout << (ok ? "  [ok] " : "  [DIRTY] ") << label;
    if (!ok || !detail.empty()) std::cout << " :: " << detail;
    std::cout << std::endl;
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using Rowl::Scripting::LuaSandbox;
    std::cout << "D07 probe (kosul-vektoru: _G __index vs ciplak getglobal)" << std::endl;

    LuaSandbox lua;
    if (!lua.initialize()) {
        std::cout << "PROB-DIRTY setup: initialize failed" << std::endl;
        return 1;
    }

    // Kurulum kontrolleri (plant ÖNCESİ; fix-öncesi/sonrası YEŞİL kalmalı).
    if (!lua.executeString("d07_real = 123")) {
        std::cout << "PROB-DIRTY setup: d07_real executeString failed" << std::endl;
        return 1;
    }
    if (!lua.executeString(
            "function d07_real_cb() rowl.var_set('d07_cb_ran', '1') end")) {
        std::cout << "PROB-DIRTY setup: d07_real_cb executeString failed" << std::endl;
        return 1;
    }
    check(lua.getVariable("d07_real") == "123",
          "kontrol: gercek global getVariable ile okunur",
          "got='" + lua.getVariable("d07_real") + "'");
    check(lua.callOptionalFunction("d07_real_cb") &&
              lua.getVariable("d07_cb_ran") == "1",
          "kontrol: gercek callback calisir (kopru saglam)");

    // Düşmanca _G __index plant'i — KOŞUL üzerinden (koşul yolunda onarım yok;
    // D06 ad-snapshot'ı metatable'ı göremez). Tuzak: ilk kayıp-anahtar
    // okumasında _G'ye d07_trap_fired eker, nil döner.
    const char* plant =
        "(function()"
        " setmetatable(_G, {__index = function(t, k)"
        " rawset(t, 'd07_trap_fired', 'yes') return nil end})"
        " return true end)()";
    if (lua.evaluateCondition(plant) != true) {
        std::cout << "PROB-DIRTY setup: planting condition did not return true"
                  << std::endl;
        return 1;
    }

    // Vektör-A (:788 getVariable): kayıp-anahtar okuma __index çalıştırmamalı.
    check(lua.getVariable("d07_no_such_key").empty(),
          "kurulum: kayip anahtar bos okunur", "");
    const std::string trapA = lua.getVariable("d07_trap_fired");
    check(trapA.empty(), "vektor-A: getVariable _G __index calistirmamali",
          "tuzak='" + trapA + "'");

    // Tuzağı sıfırla — executeString YOK (onarım plant'i temizlerdi); koşul
    // içinden rawset-nil siler (ad SİLME D06 süpürmesine takılmaz).
    lua.evaluateCondition(
        "(function() rawset(_G, 'd07_trap_fired', nil) return true end)()");

    // Vektör-B (:1288 callOptionalFunction): kayıp-callback çözümü __index
    // çalıştırmamalı (no-op sözleşmesi bozulmamalı).
    const bool noop = lua.callOptionalFunction("d07_no_such_cb");
    check(noop, "kurulum: kayip callback basarili no-op doner", "");
    const std::string trapB = lua.getVariable("d07_trap_fired");
    check(trapB.empty(),
          "vektor-B: callOptionalFunction _G __index calistirmamali",
          "tuzak='" + trapB + "'");

    if (g_failures > 0) {
        std::cout << "PROB-DIRTY d07 kosul-vektoru acik (" << g_failures
                  << " kirli gozlem)" << std::endl;
        lua.shutdown();
        return 1;
    }

    // Temizlik kontrolü (yalnızca GREEN yolunda): hata-fırlatan __index'e
    // rağmen pcall-dışı okumalar fırlatmaz — ham-read (rawget) kanıtı.
    lua.evaluateCondition(
        "(function() setmetatable(_G, {__index = function(t, k)"
        " error('d07-boom') end}) return true end)()");
    check(lua.getVariable("d07_no_such_key_2").empty(),
          "temizlik: firlatan __index getVariable'i bozmaz", "");
    check(lua.callOptionalFunction("d07_no_such_cb_2"),
          "temizlik: firlatan __index callOptionalFunction no-op'unu bozmaz",
          "");

    lua.shutdown();
    if (g_failures > 0) {
        std::cout << "PROB-DIRTY d07 temizlik kontrolu dustu" << std::endl;
        return 1;
    }
    std::cout << "PROB-CLEAN d07 kosul-vektoru kapali (ham-read)" << std::endl;
    return 0;
}
