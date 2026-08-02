#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"
#include <cstdint>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace Rowl::Scripting {

// Per-sandbox state stored in Lua registry
static const char* SANDBOX_REGISTRY_KEY = "_rowl_sandbox_ptr";

static int lua_rowl_var_get(lua_State* L) {
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        std::string key = lua_tostring(L, 1);

        // Retrieve sandbox pointer from registry
        lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);
        LuaSandbox* sandbox = static_cast<LuaSandbox*>(lua_touserdata(L, -1));
        lua_pop(L, 1);

        std::string value = sandbox ? sandbox->getVariable(key) : "";
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

        // Retrieve sandbox pointer from registry
        lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);
        LuaSandbox* sandbox = static_cast<LuaSandbox*>(lua_touserdata(L, -1));
        lua_pop(L, 1);

        if (sandbox) {
            sandbox->setVariable(key, value);
        }
    }
    return 0;
}

// Instruction counter hook - counts accumulated instructions
static void lua_instruction_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;

    // Retrieve instruction count from registry
    lua_getfield(L, LUA_REGISTRYINDEX, "_rowl_instruction_count");
    uint64_t count = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    count += 100000; // Called every 100K instructions

    if (count > 10000000) { // 10M total instruction limit
        lua_pushstring(L, "Lua sandbox instruction limit exceeded (max 10,000,000 instructions). Possible infinite loop detected!");
        lua_error(L);
        return;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(count));
    lua_setfield(L, LUA_REGISTRYINDEX, "_rowl_instruction_count");
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

    // Store this sandbox pointer in Lua registry for C callback access
    lua_pushlightuserdata(m_luaState, this);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);

    // Initialize instruction counter
    lua_pushinteger(m_luaState, 0);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_instruction_count");

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

    // Set instruction count hook for infinite loop protection (every 100K instructions)
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

    // Reset instruction counter before each execution
    lua_pushinteger(m_luaState, 0);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_instruction_count");

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

    // Clean up registry entries
    if (m_luaState) {
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_instruction_count");

        lua_close(m_luaState);
        m_luaState = nullptr;
    }

    m_initialized = false;
    ROWL_LOG_INFO("Lua Environment Shutdown Complete.");
}

} // namespace Rowl::Scripting