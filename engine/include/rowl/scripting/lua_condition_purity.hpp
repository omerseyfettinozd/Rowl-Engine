#pragma once

// D06: evaluateCondition() ham-global saflığı yardımcıları.
//
// LuaSandbox::evaluateCondition() koşul girişinde _G ad-kümesini snapshot'lar,
// çıkışında koşulun eklediği adları nil'ler (snapshot-diff). sweepStrayGlobals()
// deseninin (collect-then-clear, lua_next-safe) koşul giriş/çıkışına taşınmış
// halidir; repairGlobals() ÇAĞRILMAZ (per-frame yasak).
//
// Tek-kilit modeli: bu yardımcılar LuaSandbox kilidi ALTINDA çağrılır, ikinci
// mutex YOK. Sembol çakışması yok: her sembol d06_ önekini taşır (D07 kendi
// önekiyle aynı TU'yu paylaşabilir).

#include <cstddef>
#include <string>
#include <unordered_set>

struct lua_State;

namespace Rowl::Scripting {

// Koşul girişindeki _G ad-kümesini toplar. Salt-okunur iterasyon;
// state == nullptr ise boş küme döner. Kilit almaz (çağıran kilitlidir).
std::unordered_set<std::string> d06_snapshotConditionGlobals(lua_State* state);

// Giriş snapshot'ında OLMAYAN her _G adını nil'ler (collect-then-clear).
// Önceden var olan adların DEĞERLERİNE dokunulmaz (yalnızca eklenenler
// süpürülür; köprü/başlangıç adları girişte zaten mevcuttur). Süpürülen
// ad sayısını döner. Kilit almaz (çağıran kilitlidir).
std::size_t d06_sweepConditionGlobals(lua_State* state,
                                      const std::unordered_set<std::string>& entry);

// D07: koşul-vektörü ham-okuma. Koşul yolu onarım çalıştırmaz (per-frame
// yasak) ve D06 ad-kümesi snapshot'ı bir _G METATABLE'ını göremez; bir koşul
// setmetatable(_G, {__index=...}) ile düşmanca __index ekebilir ve bu plant
// kalıcı olur. Sonrasında kayıp-anahtara dokunan her ÇIPLAK lua_getglobal
// script kodu çalıştırır (kirlilik + pcall-dışı okumada C++ çerçevelerinden
// geçen hata). Bu yardımcı _G[key] değerini rawget ile yığına iter (+1):
// __index ASLA ateşlenmez, hata fırlatılamaz, yığın dengesi çağrı başına
// sabittir. Dönen değer itilen değerin Lua tip kodudur (LUA_TNIL kayıp).
// Kilit almaz, kota-rezervi almaz (çağıran kilitlidir ve RecoveryScope içindedir).
int d07_rawGetGlobal(lua_State* state, const char* key);

// G4: koşul-vektörü ham-YAZMA — d07_rawGetGlobal'ın yazma tarafındaki eşi.
// lua_setglobal = lua_setfield(LUA_GLOBALSINDEX, k) ve metamethod'a duyarlıdır:
// _G'de __newindex varsa ve anahtar YOKSA atama yerine __newindex ÇALIŞIR, yani
// köprü hiç yazılmaz. bindEngineApis bunu koşulun hemen ardından çağırdığı için
// (evaluateCondition başarı ve hata yolları) bir koşul `rowl = nil` +
// `setmetatable(_G, {__newindex=...})` ile köprüyü YAYINLATAMAZ hale geliyordu;
// ardından gelen lua_getglobal da __index'i ateşleyip saldırganın tablosunu
// registry'ye "_rowl_bridge" olarak yazıyordu (D6 #158'in "doğrulanmış köprü"
// işaretçisi ele geçiyordu). Bu yardımcı _G[key] = value atamasını rawset ile
// yapar: __newindex ASLA ateşlenmez, yığın dengesi çağrı başına sabittir
// (net 0). valueIndex, push'tan ÖNCE mutlak dizine çevrilir; çağıran
// RecoveryScope içindedir ve kilidi zaten tutar.
void d07_rawSetGlobal(lua_State* state, const char* key, int valueIndex);

// G4: _G metatable'ini kaldır. Mimari kural: "_G asla metatable taşımamalı"
// (lua_sandbox.cpp bindEngineApis çağrısının yanındaki not). İki yerde
// çağrılır: (1) evaluateCondition'da pcall'den HEMEN SONRA — koşulun
// bıraktığı düşman metatable'i host kodu _G'ye dokunmadan önce düşsün, D06
// süpürmesi de ancak o zaman işini görebilsin; (2) D06ConditionGlobalGuard
// dtor'unda — diğer çıkış yolları. Metatable yoksa no-op (idempotent), yığın
// dengesi net 0. Çağıran RecoveryScope içinde ve kilitlidir.
void d07_clearGlobalTableMetatable(lua_State* state);

} // namespace Rowl::Scripting
