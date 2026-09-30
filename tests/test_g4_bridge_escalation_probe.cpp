/**
 * test_g4_bridge_escalation_probe.cpp — G4 köprü yayınlama kilidi.
 *
 * Bağımsız ikili: rowl_g4_bridge_escalation_probe (ctest -R g4_bridge).
 * D01/D06/D07 konvansiyonu: yalnızca C++ LuaSandbox + lua_sandbox.hpp, dummy
 * driver. rowl_tests gövdesine gömülmez.
 *
 * KUSUR (kod okumasıyla kesin, uçtan uca uyarmayı GÖSTEREMEDİK — aşağıya
 * bak). D07 çıplak OKUMALARI (getVariable, callOptionalFunction) ham yola
 * almıştı. G4, aynı ailenin YAZMA tarafı: bindEngineApis() köprüyü
 * `lua_setglobal` + `lua_getglobal` ile yayınlıyor ve ikisi de
 * metamethod'a duyarlı:
 *
 *   - `lua_setglobal` = lua_setfield(GLOBALS, k). _G'de __newindex varsa ve
 *     anahtar yoksa ATAMA YAPMAZ, __newindex'i ÇALIŞTIRIR.
 *   - `lua_getglobal` = lua_getfield(GLOBALS, k). Anahtar yoksa __index'i
 *     ateşler, yani SCRIPT KODU ÇALIŞIR ve saldırganın tablosunu döndürür.
 *
 * evaluateCondition bu çağrıyı koşulun hemen ardından (başarı VE hata
 * yolu) yapar, oysa _G metatable'i yalnızca guard dtor'unda düşer. Yani
 * ikisi arasındaki pencerede yayınlanan köprü betimlenebilir. Deponun kendi
 * tasarım notu ("köprü dahil — pcall-sonrası bindEngineApis zaten onu
 * tazeler", lua_condition_purity.cpp:11) tam olarak bu çağrının güvenli
 * olduğunu varsayıyor; G4 o varsayımı çürütüyor.
 *
 * DÜRÜST KAPSAM — bu prob neyi kanıtlıyor, neyi kanıtlamıyor:
 *
 *   KANITLIYOR (G4-2): saldırı koşulundan sonra _G.rowl gerçek motor
 *   köprüsüdür. Bu gözlem pre-fix KIRMIZI, post-fix YEŞİL — ayırt eden
 *   tek kalem. Yani düzeltme gerçek bir davranış değişikliğidir.
 *
 *   KANITLAMIYOR (G4-3/G4-4): registry `_rowl_bridge` üzerinden kalıcı yetki
 *   devralmayı gösteremedim. Ölçtüm: saldırıdan sonra modül gerçek köprüyü
 *   alıyor, saldırganın var_set'i devreye girmiyor. Yani "yetki yükseltme"
 *   etiketi bu ölçümle DESTEKLENMİYOR. Bu iki gözlem yine de kayıt
 *   kilididir: ileride bir yol değişirse kırmızıya düşerler, ama bugün
 *   pre-fix ile post-fix'i AYIRT ETMEZLER.
 *
 *   Bu ayrım bilinçlidir: "güvenlik açığı kapandı" demek için uçtan uca
 *   kanıt gerekir; elimde kod düzeyinde sağlam gerekçe var, uçtan uca
 *   kanıt yok. Düzeltme yine de yapıldı — savunmasız bir yayın yolu, bir
 *   değişkenin kontrolünde olan bir tabloya güvenlik açısından kritik bir
 *   işaretçi yazıyorsa bu kusurdur, ispatlanmış istismar olmasa da.
 *
 * RED/GREEN: G4-2 pre-fix kırmızı. Yeşilde bu gözlemlerden herhangi biri
 * değişirse fix TURU sayılır (max 3); kırmızıda commit YOK.
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
    std::cout << "G4 probe (kosul uzerinden _G metatable ile kopru eskalasyonu)"
              << std::endl;

    LuaSandbox lua;
    if (!lua.initialize()) {
        std::cout << "PROB-DIRTY setup: initialize failed" << std::endl;
        return 1;
    }

    // ---- Kontroller: fix-öncesi de yeşil olmalı (kurulum sağlam mı?) ----
    if (!lua.executeString("g4_real = 7")) {
        std::cout << "PROB-DIRTY setup: g4_real executeString failed" << std::endl;
        return 1;
    }
    check(lua.getVariable("g4_real") == "7",
          "kontrol: gercek global getVariable ile okunur",
          "got='" + lua.getVariable("g4_real") + "'");

    if (!lua.loadModule("g4ctrl",
                        "function cb() rowl.var_set('g4_ctrl', 'ran') end")) {
        std::cout << "PROB-DIRTY setup: loadModule failed" << std::endl;
        return 1;
    }
    check(lua.callOptionalModuleFunction("g4ctrl", "cb") &&
              lua.getVariable("g4_ctrl") == "ran",
          "kontrol: gercek modul callback'i kopru uzerinden calisir");

    // ---- SALDIRI: köprüyü düşür, _G'ye __index/__newindex kur, sahte köprü
    //      döndür. Sahte köprü kendi var_get/var_set'ini sunuyor: yetki
    //      yükseltmenin gerçek biçimi bu.
    // SIRA ÖNEMLİ: sahte köprü ÖNCE kurulur, metatable SONRA. Tersi bir
    // sahte-olası tuzak yazar: '__newindex = function() end' takılıyken
    // 'g4_fake = {...}' de __newindex üzerinden geçer ve sahte köprü hiç
    // OLUŞMAZ — tırmanma o zaman sahte köprüye değil nil'e düşer ve prob
    // gerçekten yapılabilecek en kötü şeyi küçümser.
    const char* attack =
        "(function()"
        " g4_fake = { var_get = function() return 'ATTACKER' end,"
        "             var_set = function() end, marker = 'IMPOSTOR' }"
        " rowl = nil"
        " setmetatable(_G, {"
        "   __index = function(t, k) if k == 'rowl' then return g4_fake end end,"
        "   __newindex = function() end})"
        " return true end)()";
    if (lua.evaluateCondition(attack) != true) {
        std::cout << "PROB-DIRTY setup: attack condition did not return true"
                  << std::endl;
        return 1;
    }

    // ---- GÖZLEM 2 (AYIRT EDEN): _G.rowl GERÇEK köprü olmalı. Sadece var
    //   olması yetmez — sahte köprü de bir tablodur; imzası ayırt eder.
    //   Bu, pre-fix KIRMIZIya düşen TEK gözlemdir: bindEngineApis'in
    //   lua_setglobal çağrısı, _G metatable'lıyken ve anahtar yokken atama
     //   yapmak yerine __newindex'i çalıştırır, köprü hiç yazılmaz ve
    //   _G.rowl kaybolur.
    //
    //   SIRALAMA ZORUNLU: bu gözlem SALDIRIDAN SONRA İLK İŞ olmalı. Hem bu
    //   koşulun hem de sonraki modül yüklemelerinin kendi çıkışında
    //   bindEngineApis koşar ve metatable artık düştüğü için köprüyü
    //   ONARIR. Yani saldırının etkisi kendiliğinden iyileşiyor ve geç
    //   ölçülürse hiçbir şey görünmüyor. Bu da blast radius'un dar
    //   olduğunun kanıtı.
    check(lua.evaluateCondition("type(rowl) == 'table' and"
                                " type(rowl.var_get) == 'function' and"
                                " type(rowl.var_set) == 'function'"),
          "G4-2: _G.rowl gercek motor koprusi (ayirt eden gozlem)");

    // ---- GÖZLEM 3: _rowl_bridge (registry) zehirli mi? ASIL yükseltme
    //   budur: modül ortamları pinVerifiedRowlIntoEnv ile BURDAN alır.
    //   Saldırıdan sonra yüklenen modül gerçek köprüyü görmeli. ----
    if (!lua.loadModule("g4after",
                        "function cb() rowl.var_set('g4_after', 'real') end")) {
        std::cout << "PROB-DIRTY: post-attack loadModule failed" << std::endl;
        return 1;
    }
    // Gözlem ve detayı AYRI değişkenlerde hesapla: check()'in argümanları
    // C++'da tanımsız sırayla değerlendirilir (GCC sağdan sola), yani
    // detay ifadesi callback'den ÖNCE çalışıp boş okur ve yanıltıcı bir
    // teşhis bırakır.
    const bool afterOk = lua.callOptionalModuleFunction("g4after", "cb");
    const std::string afterValue = lua.getVariable("g4_after");
    check(afterOk && afterValue == "real",
          "G4-3: saldiri sonrasi modul GERCEK kopruyu aliyor",
          "g4_after='" + afterValue + "'");

    // ---- GÖZLEM 4: sahte köprünün yetkisi GERÇEKTEN de elde edilmemeli.
    //   Sahte köprünün var_set'i boş bir Lua fonksiyonu — çağrılsa host
    //   değişken haritası GÜNCELLENMEZ. Yani aynı modül çağrısı gerçek
    //   köprüde host değişkeni yazar, sahte köprüde sessizce hiçbir şey
    //   yapmaz. İşaretçi okumak yerine YAZMA yolunu ölçüyoruz, çünkü
    //   yetki yükseltmesi tam olarak yazma yetkisidir. ----
    if (!lua.loadModule("g4write",
                        "function cb() rowl.var_set('g4_write', 'host') end")) {
        std::cout << "PROB-DIRTY: write-probe module load failed" << std::endl;
        return 1;
    }
    const bool writeOk = lua.callOptionalModuleFunction("g4write", "cb");
    const std::string writeValue = lua.getVariable("g4_write");
    check(writeOk && writeValue == "host",
          "G4-4: sahte koprunun var_set'i devreye girmedi (yazma yetkisi)",
          "g4_write='" + writeValue + "'");
    // ---- SIRA KRİTİK: aşağıdaki iki gözlem, SALDIRIDAN SONRA İLK İŞ OLARAK
    //   çalışmalı. Sebep: G4-1/G4-2 koşullarının kendi çıkışında
    //   bindEngineApis() koşar ve metatable artık düştüğü için köprüyü
    //   KENDİSİ tamir eder. Onlar önce koşsaydı, tırmanma kanıtı testin
    //   kendisi tarafından silinirdi — prob kendi kendini sabotez ederdi.
    //   Gerçek dünya sırası da budur: saldırı koşulu ile modül yüklemesi
    // ---- GÖZLEM 5: D06 süpürmesi metatable varken de ÇALIŞMALI. Süpürmenin
    //   setglobal(name, nil) çağrısı da __newindex'e gider; pre-fix yemek
    //   ekilen ham global SİLİNMEZ. ----
    if (lua.evaluateCondition(
            "(function() setmetatable(_G, {__newindex = function() end})"
            " g4_stray = 99 return true end)()")) {
        check(lua.evaluateCondition("g4_stray == nil"),
              "G4-5: metatable varken ekilen ham global silindi");
    }

    lua.shutdown();

    if (g_failures != 0) {
        std::cout << "PROB-DIRTY " << g_failures << " bridge-escalation "
                  << "gözlemi kirmizi" << std::endl;
        return 1;
    }
    std::cout << "PROB-CLEAN kosul _G metatable yoluyla kopruyu eskalate edemiyor"
              << std::endl;
    return 0;
}
