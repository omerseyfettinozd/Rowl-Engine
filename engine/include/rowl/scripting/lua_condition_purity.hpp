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

} // namespace Rowl::Scripting
