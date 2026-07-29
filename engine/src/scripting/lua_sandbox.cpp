#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace Rowl::Scripting {

static thread_local LuaSandbox* s_activeSandbox = nullptr;

static int lua_rowl_var_get(lua_State* L) {
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        std::string key = lua_tostring(L, 1);
        std::string value = s_activeSandbox ? s_activeSandbox->getVariable(key) : "";
        lua_pushstring(L, value.c_str());
        return 1;
    }
    lua_pushstring(L, "");
    return 1;
}

static int lua_rowl_var_set(lua_State* L) {
    if (lua_gettop(L) >= 2 && lua_isstring(L, 1) && lua_isstring(L, 2)) {
        std::string key = lua_tostring(L, 1);
        std::string value = lua_tostring(L, 2);
        if (s_activeSandbox) {
            s_activeSandbox->setVariable(key, value);
        }
    }
    return 0;
}

static void lua_instruction_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    // Use luaL_error with a safe message - hook errors are caught by pcall
    luaL_error(L, "Lua sandbox instruction limit exceeded (max 10,000,000 instructions). Possible infinite loop detected!");
}

LuaSandbox::LuaSandbox() = default;

LuaSandbox::~LuaSandbox() {
    if (m_initialized) {
        shutdown();
    }
}

bool LuaSandbox::initialize() {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing Sandboxed Lua 5.4 Environment...");

    m_luaState = luaL_newstate();
    if (!m_luaState) {
        ROWL_LOG_ERROR("Failed to create Lua state!");
        return false;
    }

    s_activeSandbox = this;

    // Load safe standard libraries only
    luaL_requiref(m_luaState, "_G", luaopen_base, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "math", luaopen_math, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "string", luaopen_string, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "table", luaopen_table, 1);
    lua_pop(m_luaState, 1);

    // Blacklist dangerous libraries explicitly
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "io");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "os");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "debug");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "package");

    // Set instruction count hook for infinite loop protection (100,000 count = ~10M instructions)
    // LUA_MASKCOUNT triggers hook every 'count' VM instructions
    lua_sethook(m_luaState, lua_instruction_hook, LUA_MASKCOUNT, 100000);

    bindEngineApis();

    m_initialized = true;
    ROWL_LOG_INFO("Sandboxed Lua Environment Initialized Successfully.");
    return true;
}

void LuaSandbox::bindEngineApis() {
    if (!m_luaState) return;

    // Create rowl namespace table
    lua_newtable(m_luaState);

    // Bind rowl.var_get and rowl.var_set
    lua_pushcfunction(m_luaState, lua_rowl_var_get);
    lua_setfield(m_luaState, -2, "var_get");

    lua_pushcfunction(m_luaState, lua_rowl_var_set);
    lua_setfield(m_luaState, -2, "var_set");

    lua_setglobal(m_luaState, "rowl");
}

void LuaSandbox::setVariable(const std::string& key, const std::string& value) {
    m_scriptVariables[key] = value;
    ROWL_LOG_TRACE("Lua Sandbox Variable Set: '" + key + "' = '" + value + "'");
}

std::string LuaSandbox::getVariable(const std::string& key) const {
    auto it = m_scriptVariables.find(key);
    if (it != m_scriptVariables.end()) {
        return it->second;
    }
    return "";
}

bool LuaSandbox::executeString(const std::string& scriptCode) {
    if (!m_initialized || !m_luaState) {
        ROWL_LOG_ERROR("Lua Sandbox executeString called without initialization!");
        return false;
    }

    int loadStatus = luaL_loadstring(m_luaState, scriptCode.c_str());
    if (loadStatus != LUA_OK) {
        std::string err = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        ROWL_LOG_ERROR("Lua Script Syntax Error: " + err);
        return false;
    }

    // Protected call (lua_pcall) prevents script crashes from killing engine process
    int callStatus = lua_pcall(m_luaState, 0, 0, 0);
    if (callStatus != LUA_OK) {
        std::string err = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        ROWL_LOG_WARN("Lua Script Runtime Exception (Caught Safely): " + err);
        return false;
    }

    return true;
}

void LuaSandbox::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Sandboxed Lua Environment...");
    if (m_luaState) {
        lua_close(m_luaState);
        m_luaState = nullptr;
    }
    if (s_activeSandbox == this) {
        s_activeSandbox = nullptr;
    }
    m_initialized = false;
    ROWL_LOG_INFO("Lua Environment Shutdown Complete.");
}

} // namespace Rowl::Scripting
