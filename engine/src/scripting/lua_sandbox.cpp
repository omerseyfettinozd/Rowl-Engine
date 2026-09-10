#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"
#include <cmath>
#include <cstdint>
#include <string_view>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace Rowl::Scripting {

// Per-sandbox state stored in Lua registry
static const char* SANDBOX_REGISTRY_KEY = "_rowl_sandbox_ptr";
constexpr std::size_t kMaxLoadedModules = 128;
constexpr std::size_t kMaxModuleIdBytes = 256;

static void resetInstructionCounter(lua_State* state) {
    lua_pushinteger(state, 0);
    lua_setfield(state, LUA_REGISTRYINDEX, "_rowl_instruction_count");
}

// Component environments intentionally accept ordinary globals (their private
// state), but the engine bridge itself must remain read-only. Keeping this at
// the environment boundary also prevents `_G.rowl = ...` from breaking the
// component later in the same callback.
static int lua_module_newindex(lua_State* state) {
    const char* key = lua_tostring(state, 2);
    if (key && std::string_view(key) == "rowl") return 0;
    lua_rawset(state, 1);
    return 0;
}

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
    resetInstructionCounter(m_luaState);

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
    // luaopen_base also exposes filesystem/dynamic-code helpers. Keeping
    // those available would bypass the library blacklist above.
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "dofile");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "loadfile");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "load");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "collectgarbage");

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
    if (m_luaState) {
        char* end = nullptr;
        double num = std::strtod(value.c_str(), &end);
        if (end != value.c_str() && *end == '\0') {
            lua_pushnumber(m_luaState, num);
        } else {
            lua_pushstring(m_luaState, value.c_str());
        }
        lua_setglobal(m_luaState, key.c_str());
    }
    ROWL_LOG_TRACE("Lua Sandbox Variable Set: '" + key + "' = '" + value + "'");
}

std::string LuaSandbox::getVariable(const std::string& key) const {
    auto it = m_scriptVariables.find(key);
    if (it != m_scriptVariables.end()) {
        return it->second;
    }
    if (m_luaState) {
        lua_getglobal(m_luaState, key.c_str());
        if (lua_isstring(m_luaState, -1) || lua_isnumber(m_luaState, -1)) {
            std::string val = lua_tostring(m_luaState, -1);
            lua_pop(m_luaState, 1);
            return val;
        }
        lua_pop(m_luaState, 1);
    }
    return "";
}

void LuaSandbox::setGlobalNumber(const std::string& key, double value) {
    if (!std::isfinite(value)) {
        ROWL_LOG_WARN("Lua Sandbox rejected non-finite numeric variable: '" + key + "'");
        return;
    }
    m_scriptVariables[key] = std::to_string(value);
    if (m_luaState) {
        lua_pushnumber(m_luaState, value);
        lua_setglobal(m_luaState, key.c_str());
    }
    ROWL_LOG_TRACE("Lua Sandbox Number Set: '" + key + "' = " + std::to_string(value));
}

double LuaSandbox::getGlobalNumber(const std::string& key, double defaultValue) const {
    if (m_luaState) {
        lua_getglobal(m_luaState, key.c_str());
        if (lua_isnumber(m_luaState, -1)) {
            double val = lua_tonumber(m_luaState, -1);
            lua_pop(m_luaState, 1);
            return std::isfinite(val) ? val : defaultValue;
        }
        lua_pop(m_luaState, 1);
    }
    auto it = m_scriptVariables.find(key);
    if (it != m_scriptVariables.end()) {
        try {
            const double value = std::stod(it->second);
            return std::isfinite(value) ? value : defaultValue;
        } catch (...) {}
    }
    return defaultValue;
}

bool LuaSandbox::evaluateCondition(const std::string& conditionExpr) {
    if (conditionExpr.empty() || conditionExpr == "true" || conditionExpr == "1") {
        return true;
    }
    if (conditionExpr == "false" || conditionExpr == "0") {
        return false;
    }
    if (!m_initialized || !m_luaState) {
        ROWL_LOG_WARN("Lua Sandbox evaluateCondition called without initialization. Defaulting to true.");
        return true;
    }

    // Reset instruction counter
    lua_pushinteger(m_luaState, 0);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_instruction_count");

    // Try wrapping in return (...)
    std::string code;
    if (conditionExpr.rfind("return", 0) == 0) {
        code = conditionExpr;
    } else {
        code = "return (" + conditionExpr + ")";
    }

    int loadStatus = luaL_loadstring(m_luaState, code.c_str());
    if (loadStatus != LUA_OK) {
        // Pop error and try raw expression with return prefix
        lua_pop(m_luaState, 1);
        code = "return " + conditionExpr;
        loadStatus = luaL_loadstring(m_luaState, code.c_str());
        if (loadStatus != LUA_OK) {
            std::string err = lua_tostring(m_luaState, -1);
            lua_pop(m_luaState, 1);
            m_lastError = err;
            ROWL_LOG_WARN("Lua Condition syntax error in '" + conditionExpr + "': " + err);
            return false;
        }
    }

    int callStatus = lua_pcall(m_luaState, 0, 1, 0);
    if (callStatus != LUA_OK) {
        std::string err = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        bindEngineApis();
        ROWL_LOG_WARN("Lua Condition runtime error in '" + conditionExpr + "': " + err);
        return false;
    }

    bool result = lua_toboolean(m_luaState, -1) != 0;
    lua_pop(m_luaState, 1);
    bindEngineApis();
    return result;
}

void LuaSandbox::clearVariables() {
    if (m_luaState) {
        for (const auto& [key, value] : m_scriptVariables) {
            (void)value;
            lua_pushnil(m_luaState);
            lua_setglobal(m_luaState, key.c_str());
        }
    }
    m_scriptVariables.clear();
}

bool LuaSandbox::executeString(const std::string& scriptCode) {
    if (!m_initialized || !m_luaState) {
        ROWL_LOG_ERROR("Lua Sandbox executeString called without initialization!");
        return false;
    }

    // Reset instruction counter before each execution
    m_lastError.clear();
    resetInstructionCounter(m_luaState);

    int loadStatus = luaL_loadstring(m_luaState, scriptCode.c_str());
    if (loadStatus != LUA_OK) {
        std::string err = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        ROWL_LOG_ERROR("Lua Script Syntax Error: " + err);
        return false;
    }

    // Protected call (lua_pcall) prevents script crashes from killing engine process
    int callStatus = lua_pcall(m_luaState, 0, 0, 0);
    if (callStatus != LUA_OK) {
        std::string err = lua_tostring(m_luaState, -1);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        bindEngineApis();
        ROWL_LOG_WARN("Lua Script Runtime Exception (Caught Safely): " + err);
        return false;
    }

    // Scripts may create globals freely, but cannot permanently replace the
    // engine bridge used by subsequent component scripts or conditions.
    bindEngineApis();
    return true;
}

bool LuaSandbox::loadModule(const std::string& moduleId, const std::string& scriptCode) {
    m_lastError.clear();
    if (!m_initialized || !m_luaState || moduleId.empty() ||
        moduleId.size() > kMaxModuleIdBytes || scriptCode.empty()) {
        m_lastError = "Invalid Lua component module input";
        return false;
    }

    // Replacing a module is deliberate (e.g. editor hot scene update), but a
    // newly created scene cannot allocate an unbounded number of environments.
    const bool replacesExisting = m_modules.contains(moduleId);
    if (!replacesExisting && m_modules.size() >= kMaxLoadedModules) {
        m_lastError = "Lua component module limit exceeded (max 128)";
        ROWL_LOG_ERROR(m_lastError);
        return false;
    }

    // Each component owns an environment. It inherits only the sandbox's safe
    // globals, keeps _G local, and hides the metatable so one component cannot
    // mutate another component's lookup path.
    lua_newtable(m_luaState);                         // env
    const int environmentIndex = lua_gettop(m_luaState);
    lua_pushvalue(m_luaState, environmentIndex);
    lua_setfield(m_luaState, environmentIndex, "_G");
    lua_newtable(m_luaState);                         // metatable
    lua_pushglobaltable(m_luaState);
    lua_setfield(m_luaState, -2, "__index");
    lua_pushcfunction(m_luaState, lua_module_newindex);
    lua_setfield(m_luaState, -2, "__newindex");
    lua_pushboolean(m_luaState, 0);
    lua_setfield(m_luaState, -2, "__metatable");
    lua_setmetatable(m_luaState, environmentIndex);

    resetInstructionCounter(m_luaState);
    const int loadStatus = luaL_loadbufferx(m_luaState, scriptCode.data(), scriptCode.size(),
                                            moduleId.c_str(), "t");
    if (loadStatus != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        ROWL_LOG_ERROR("Lua component syntax error in '" + moduleId + "': " + m_lastError);
        lua_pop(m_luaState, 2); // error, environment
        return false;
    }

    // The first upvalue of a Lua chunk is _ENV. Setting it before pcall makes
    // globals declared by this source private to the component.
    lua_pushvalue(m_luaState, environmentIndex);
    if (lua_setupvalue(m_luaState, -2, 1) == nullptr) {
        m_lastError = "Lua component could not bind its isolated environment";
        lua_pop(m_luaState, 2); // chunk, environment
        return false;
    }
    if (lua_pcall(m_luaState, 0, 0, 0) != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        ROWL_LOG_WARN("Lua component runtime exception in '" + moduleId + "': " + m_lastError);
        lua_pop(m_luaState, 2); // error, environment
        bindEngineApis();
        return false;
    }

    if (replacesExisting) {
        luaL_unref(m_luaState, LUA_REGISTRYINDEX, m_modules.at(moduleId));
    }
    lua_pushvalue(m_luaState, environmentIndex);
    m_modules[moduleId] = luaL_ref(m_luaState, LUA_REGISTRYINDEX);
    lua_pop(m_luaState, 1); // environment
    bindEngineApis();
    return true;
}

bool LuaSandbox::callOptionalModuleFunction(const std::string& moduleId,
                                            const std::string& functionName,
                                            double deltaTime) {
    m_lastError.clear();
    if (!m_initialized || !m_luaState || functionName.empty()) {
        m_lastError = "Lua sandbox is unavailable";
        return false;
    }
    const auto module = m_modules.find(moduleId);
    if (module == m_modules.end()) {
        m_lastError = "Lua component module is not loaded";
        return false;
    }

    lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, module->second); // env
    lua_getfield(m_luaState, -1, functionName.c_str());
    if (lua_isnil(m_luaState, -1)) {
        lua_pop(m_luaState, 2);
        return true;
    }
    if (!lua_isfunction(m_luaState, -1)) {
        m_lastError = "Lifecycle callback is not a function: " + functionName;
        lua_pop(m_luaState, 2);
        ROWL_LOG_WARN("Lua component lifecycle callback is not a function: " + moduleId + "." + functionName);
        return false;
    }
    lua_pushnumber(m_luaState, deltaTime);
    resetInstructionCounter(m_luaState);
    if (lua_pcall(m_luaState, 1, 0, 0) != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        ROWL_LOG_ERROR("Lua component lifecycle callback '" + moduleId + "." + functionName + "' failed: " + m_lastError);
        lua_pop(m_luaState, 2); // error, environment
        bindEngineApis();
        return false;
    }
    lua_pop(m_luaState, 1); // environment
    bindEngineApis();
    return true;
}

bool LuaSandbox::unloadModule(const std::string& moduleId) {
    const auto module = m_modules.find(moduleId);
    if (module == m_modules.end()) return false;
    if (m_luaState) luaL_unref(m_luaState, LUA_REGISTRYINDEX, module->second);
    m_modules.erase(module);
    return true;
}

void LuaSandbox::clearModules() {
    if (m_luaState) {
        for (const auto& [moduleId, reference] : m_modules) {
            (void)moduleId;
            luaL_unref(m_luaState, LUA_REGISTRYINDEX, reference);
        }
    }
    m_modules.clear();
}

bool LuaSandbox::callOptionalFunction(const std::string& functionName, double deltaTime) {
    if (!m_initialized || !m_luaState || functionName.empty()) return false;

    lua_getglobal(m_luaState, functionName.c_str());
    if (lua_isnil(m_luaState, -1)) {
        lua_pop(m_luaState, 1);
        return true;
    }
    if (!lua_isfunction(m_luaState, -1)) {
        lua_pop(m_luaState, 1);
        ROWL_LOG_WARN("Lua lifecycle callback is not a function: " + functionName);
        return false;
    }

    lua_pushnumber(m_luaState, deltaTime);
    resetInstructionCounter(m_luaState);
    if (lua_pcall(m_luaState, 1, 0, 0) != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        const std::string error = rawError ? rawError : "unknown Lua error";
        lua_pop(m_luaState, 1);
        bindEngineApis();
        ROWL_LOG_ERROR("Lua lifecycle callback '" + functionName + "' failed: " + error);
        return false;
    }
    bindEngineApis();
    return true;
}

void LuaSandbox::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Sandboxed Lua Environment...");

    // Clean up registry entries
    if (m_luaState) {
        clearModules();
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
