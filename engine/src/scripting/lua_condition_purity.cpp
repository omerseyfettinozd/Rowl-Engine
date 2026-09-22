// D06: evaluateCondition() ham-global saflığı (lua_condition_purity.cpp).
//
// LuaSandbox::D06ConditionGlobalGuard'ın TEK gerçeklemesi bu TU'dadır;
// EngineHost/engine.cpp/window.cpp/MainWindowViewModel'a kod eklenmez.
// snapshotInitialGlobals() (ad-kümesi toplama) + sweepStrayGlobals()
// (collect-then-clear) deseninin koşul giriş/çıkış snapshot-diff karşılığıdır.
//
// Tek-kilit modeli: guard, evaluateCondition() kilidi ALTINDA kurulur ve
// yıkılır; ikinci mutex YOK. repairGlobals() ÇAĞRILMAZ (per-frame yasak):
// yalnızca koşulun EKLEDİĞİ adlar nil'lenir, önceden var olan değerler
// (köprü dahil — pcall-sonrası bindEngineApis zaten onu tazeler) korunur.

#include "rowl/scripting/lua_condition_purity.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"

#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace Rowl::Scripting {

std::unordered_set<std::string> d06_snapshotConditionGlobals(lua_State* state) {
    std::unordered_set<std::string> names;
    if (state == nullptr) return names;
    lua_pushglobaltable(state);
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            if (const char* name = lua_tostring(state, -2)) {
                names.emplace(name);
            }
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return names;
}

std::size_t d06_sweepConditionGlobals(lua_State* state,
                                      const std::unordered_set<std::string>& entry) {
    if (state == nullptr) return 0;
    const int top = lua_gettop(state);
    // lua_next iterasyon sırasında mutasyona izin vermez: önce topla, sonra sil.
    std::vector<std::string> strays;
    lua_pushglobaltable(state);
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            if (const char* name = lua_tostring(state, -2)) {
                if (entry.find(name) == entry.end()) {
                    strays.emplace_back(name);
                }
            }
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    for (const auto& name : strays) {
        lua_pushnil(state);
        lua_setglobal(state, name.c_str());
    }
    lua_settop(state, top);
    return strays.size();
}

LuaSandbox::D06ConditionGlobalGuard::D06ConditionGlobalGuard(LuaSandbox* owner)
    : m_owner(owner), m_armed(false) {
    if (m_owner == nullptr || m_owner->m_luaState == nullptr) return;
    // Salt-okunur okuma bile anahtar ilk kez görülüyorsa interning ile
    // ayırabilir; kota-pinned durumda RecoveryScope rezervi korur (#28).
    const RecoveryScope recovery(m_owner);
    m_entry = d06_snapshotConditionGlobals(m_owner->m_luaState);
    m_armed = true;
}

LuaSandbox::D06ConditionGlobalGuard::~D06ConditionGlobalGuard() {
    if (!m_armed || m_owner == nullptr || m_owner->m_luaState == nullptr) return;
    m_armed = false;
    // pushnil/setglobal ayırabilir; RecoveryScope rezervi altında (#28).
    const RecoveryScope recovery(m_owner);
    const std::size_t swept =
        d06_sweepConditionGlobals(m_owner->m_luaState, m_entry);
    if (swept > 0) {
        ROWL_LOG_WARN("Lua condition raw global(s) swept (" +
                      std::to_string(swept) + ")");
    }
}

} // namespace Rowl::Scripting
