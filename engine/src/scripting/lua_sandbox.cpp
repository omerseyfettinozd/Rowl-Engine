#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/util/locale_independent_parse.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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
// A1: sandbox resource budgets (H24/H25). Instruction hook trips at 10M;
// poisoned sessions refuse further runs until clearVariables(). Memory quota
// is enforced by quotaAlloc; oversized source is rejected at entry.
constexpr std::size_t kMaxLuaMemoryBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxScriptBytes = 256u * 1024u;

// A1 (H27): every lua_tostring→std::string site must go through here. A
// script-controlled non-string error value (e.g. error({})) makes
// lua_tostring return nullptr, and std::string(nullptr) is UB/crash.
static std::string takeLuaError(lua_State* state) {
    const char* rawError = lua_tostring(state, -1);
    return rawError ? rawError : "unknown Lua error";
}

// #28: entry accounting runs outside any pcall, so it executes inside the
// recovery reserve (quota-pinned sessions must survive it).
void LuaSandbox::resetInstructionCounter() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    lua_pushinteger(m_luaState, 0);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_instruction_count");
}

// #28: recovery reserve. Script-side allocations are capped at
// kMaxLuaMemoryBytes, so at recovery entry live Lua memory is at most the
// quota (plus one in-flight growth); the reserve strictly exceeds worst-case
// recovery allocation (a few small tables/strings), which makes a second OOM
// during post-pcall recovery impossible.
constexpr std::size_t kRecoveryReserveBytes = 1024u * 1024u;

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
        // A1 (H24): the limit error is catchable by a script-side pcall, so a
        // hostile script could catch-and-respin forever. Poison the session:
        // entry points refuse further runs until clearVariables() resets it.
        lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);
        if (LuaSandbox* sandbox = static_cast<LuaSandbox*>(lua_touserdata(L, -1))) {
            sandbox->tripInstructionLimit();
        }
        lua_pop(L, 1);
        lua_pushstring(L, "Lua sandbox instruction limit exceeded (max 10,000,000 instructions). Possible infinite loop detected!");
        lua_error(L);
        return;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(count));
    lua_setfield(L, LUA_REGISTRYINDEX, "_rowl_instruction_count");
}

// A1 (H25): allocation quota. Replaces the default allocator; requests that
// would push live Lua memory past kMaxLuaMemoryBytes fail with a catchable
// "not enough memory" error instead of OOM-killing the process.
void* LuaSandbox::quotaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* sandbox = static_cast<LuaSandbox*>(ud);
    if (nsize == 0) {
        if (sandbox && osize <= sandbox->m_bytesAllocated) sandbox->m_bytesAllocated -= osize;
        else if (sandbox) sandbox->m_bytesAllocated = 0;
        free(ptr);
        return nullptr;
    }
    const size_t grown = (nsize > osize) ? (nsize - osize) : 0;
    size_t cap = kMaxLuaMemoryBytes;
    if (sandbox && sandbox->m_inRecovery) cap += kRecoveryReserveBytes;
    if (sandbox && sandbox->m_bytesAllocated + grown > cap) return nullptr;
    void* resized = realloc(ptr, nsize);
    if (resized && sandbox) sandbox->m_bytesAllocated += grown;
    return resized;
}

// A1 (H24/H25): fail-closed entry gate for every code-loading path.
bool LuaSandbox::checkRunAllowed(const char* what, std::size_t codeBytes) {
    if (m_limitTripped) {
        m_lastError = "Lua sandbox instruction budget exhausted; session poisoned until clearVariables()";
        ROWL_LOG_ERROR(m_lastError);
        return false;
    }
    if (codeBytes > kMaxScriptBytes) {
        m_lastError = std::string("Lua script too large for ") + what + " (max 256 KiB)";
        ROWL_LOG_ERROR(m_lastError);
        return false;
    }
    return true;
}

// Names owned by the sandbox bridge or the Lua standard libraries. A script
// must never replace these through the variable API; direct global assignment
// inside a script is additionally repaired by bindEngineApis().
bool LuaSandbox::isReservedVariableName(const std::string& key) {
    static const std::unordered_set<std::string_view> kReserved = {
        "rowl", "_G", "_ENV",
        "math", "string", "table", "coroutine", "utf8",
        "package", "io", "os", "debug",
        "dofile", "loadfile", "load", "collectgarbage", "require", "module",
    };
    return key.empty() || kReserved.find(key) != kReserved.end();
}

// Locale-independent number detection: only '.' is a decimal separator, so a
// comma-decimal locale (e.g. tr_TR) can never change what a script value
// means. parseAsciiDouble never consults the global C/C++ locale; from_chars
// is not used because Apple's libc++ still ships floating-point from_chars
// as a deleted function (macOS compile gate).
static bool parseSandboxNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    double value = 0.0;
    if (!Rowl::Util::parseAsciiDouble(text.data(), text.data() + text.size(), value)) return false;
    if (!std::isfinite(value)) return false;
    out = value;
    return true;
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

    // A1 (H25): cap total Lua allocations so one hostile chunk cannot OOM the
    // process (e.g. string.rep building a multi-GB block in a single call,
    // which the instruction hook never sees).
    m_bytesAllocated = 0;
    m_limitTripped = false;
    lua_setallocf(m_luaState, &LuaSandbox::quotaAlloc, this);

    // Store this sandbox pointer in Lua registry for C callback access
    lua_pushlightuserdata(m_luaState, this);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);

    // Initialize instruction counter
    resetInstructionCounter();

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
    snapshotInitialGlobals();

    m_initialized = true;
    ROWL_LOG_INFO("Sandboxed Lua Environment Initialized Successfully.");
    return true;
}

void LuaSandbox::snapshotInitialGlobals() {
    m_initialGlobals.clear();
    if (!m_luaState) return;
    lua_pushglobaltable(m_luaState);
    lua_pushnil(m_luaState);
    while (lua_next(m_luaState, -2) != 0) {
        if (lua_type(m_luaState, -2) == LUA_TSTRING) {
            if (const char* name = lua_tostring(m_luaState, -2)) {
                m_initialGlobals.emplace(name);
            }
        }
        lua_pop(m_luaState, 1);
    }
    lua_pop(m_luaState, 1);
}

void LuaSandbox::sweepStrayGlobals() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    // Scripts can create arbitrary globals (x = 1, function on_enter...).
    // Only sandbox-owned names survive a session boundary; lua_next cannot
    // tolerate mutation mid-iteration, so collect first, then clear.
    std::vector<std::string> strayGlobals;
    lua_pushglobaltable(m_luaState);
    lua_pushnil(m_luaState);
    while (lua_next(m_luaState, -2) != 0) {
        if (lua_type(m_luaState, -2) == LUA_TSTRING) {
            if (const char* name = lua_tostring(m_luaState, -2)) {
                if (m_initialGlobals.find(name) == m_initialGlobals.end()) {
                    strayGlobals.emplace_back(name);
                }
            }
        }
        lua_pop(m_luaState, 1);
    }
    lua_pop(m_luaState, 1);
    for (const auto& name : strayGlobals) {
        lua_pushnil(m_luaState);
        lua_setglobal(m_luaState, name.c_str());
    }
}

void LuaSandbox::bindEngineApis() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);

    // Create rowl namespace table
    lua_newtable(m_luaState);

    // Bind rowl.var_get and rowl.var_set
    lua_pushcfunction(m_luaState, lua_rowl_var_get);
    lua_setfield(m_luaState, -2, "var_get");

    lua_pushcfunction(m_luaState, lua_rowl_var_set);
    lua_setfield(m_luaState, -2, "var_set");

    lua_setglobal(m_luaState, "rowl");
}

// A1 (H26): a module environment's __newindex guard silently ignores `rowl`
// writes, but rawset(_G, "rowl", fake) bypasses the guard and plants an
// impostor bridge that later callbacks in the same module would resolve
// before the real one. The sweep deletes that key with rawset — lua_setfield
// would re-trigger the guard and silently keep the impostor.
void LuaSandbox::sweepModuleEnvRowl(const std::string& moduleId) {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    const auto module = m_modules.find(moduleId);
    if (module == m_modules.end()) return;
    lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, module->second); // env
    lua_pushstring(m_luaState, "rowl");
    lua_pushnil(m_luaState);
    lua_rawset(m_luaState, -3); // bypass __newindex, delete the impostor
    lua_pop(m_luaState, 1); // env
}

// A1 (H31): scripts share the stdlib tables through the global table (and
// module envs through __index), so `math.sqrt = ...` in one script poisons
// every later reader. Fresh tables from the open functions replace the
// polluted ones. _LOADED entries are dropped first — otherwise requiref
// returns the polluted cached table. Re-running luaopen_base registers into
// the EXISTING global table (pristine base funcs overwrite polluted ones,
// other globals survive), but it also reintroduces dofile/load — re-nil them.
void LuaSandbox::repairGlobals() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    lua_getfield(m_luaState, LUA_REGISTRYINDEX, "_LOADED");
    if (lua_istable(m_luaState, -1)) {
        // "_G" must be dropped too: initialize() cached the base table in
        // _LOADED, and requiref would otherwise return it without re-running
        // luaopen_base (leaving polluted base funcs like tostring in place).
        for (const char* lib : {"math", "string", "table", "_G"}) {
            lua_pushnil(m_luaState);
            lua_setfield(m_luaState, -2, lib);
        }
    }
    lua_pop(m_luaState, 1); // _LOADED
    luaL_requiref(m_luaState, "math", luaopen_math, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "string", luaopen_string, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "table", luaopen_table, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "_G", luaopen_base, 1);
    lua_pop(m_luaState, 1);
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "dofile");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "loadfile");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "load");
    lua_pushnil(m_luaState); lua_setglobal(m_luaState, "collectgarbage");
    // The global table itself must carry no metatable: a script with
    // setmetatable() access could otherwise install a hostile __index that
    // intercepts every later global read. Fresh states have none, and engine
    // code never sets one (module envs use their own tables).
    lua_pushglobaltable(m_luaState);
    lua_pushnil(m_luaState);
    lua_setmetatable(m_luaState, -2);
    lua_pop(m_luaState, 1);
    // Rebind the bridge in case a script assigned `rowl` directly. This is
    // deliberately NOT clearVariables(): repair preserves legitimate script
    // globals (on_enter/on_update defined by earlier scripts must survive),
    // the variable map, and the H24 poison — repair is not a session
    // boundary. Stray cleanup stays exclusive to clearVariables().
    bindEngineApis();
}

void LuaSandbox::setVariable(const std::string& key, const std::string& value) {
    if (isReservedVariableName(key)) {
        ROWL_LOG_WARN("Lua Sandbox rejected reserved variable name: '" + key + "'");
        return;
    }
    m_scriptVariables[key] = value;
    if (m_luaState) {
        double num = 0.0;
        if (parseSandboxNumber(value, num)) {
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
    if (isReservedVariableName(key)) {
        ROWL_LOG_WARN("Lua Sandbox rejected reserved variable name: '" + key + "'");
        return;
    }
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
    m_lastError.clear();
    // Fail-closed: an uninitialized or broken sandbox must never open a
    // conditional branch. This check stays above the literal fast-path so even
    // "true" cannot pass on a dead sandbox.
    if (!m_initialized || !m_luaState) {
        m_lastError = "Lua sandbox is not initialized; failing closed on condition evaluation";
        ROWL_LOG_WARN("Lua Sandbox evaluateCondition called without initialization. Failing closed (false).");
        return false;
    }
    if (conditionExpr.empty() || conditionExpr == "true" || conditionExpr == "1") {
        return true;
    }
    if (conditionExpr == "false" || conditionExpr == "0") {
        return false;
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

    if (!checkRunAllowed("condition", code.size())) return false;

    int loadStatus = luaL_loadstring(m_luaState, code.c_str());
    if (loadStatus != LUA_OK) {
        // Pop error and try raw expression with return prefix
        lua_pop(m_luaState, 1);
        code = "return " + conditionExpr;
        loadStatus = luaL_loadstring(m_luaState, code.c_str());
        if (loadStatus != LUA_OK) {
            std::string err = takeLuaError(m_luaState);
            lua_pop(m_luaState, 1);
            m_lastError = err;
            ROWL_LOG_WARN("Lua Condition syntax error in '" + conditionExpr + "': " + err);
            return false;
        }
    }

    int callStatus = lua_pcall(m_luaState, 0, 1, 0);
    if (callStatus != LUA_OK) {
        std::string err = takeLuaError(m_luaState);
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
        sweepStrayGlobals();
        for (const auto& [key, value] : m_scriptVariables) {
            (void)value;
            lua_pushnil(m_luaState);
            lua_setglobal(m_luaState, key.c_str());
        }
        // A script may have assigned `rowl = ...` directly; restore the bridge
        // so the next session starts from a known-good namespace.
        bindEngineApis();
    }
    m_scriptVariables.clear();
    // A1 (H24): a new session boundary lifts the instruction-limit poison.
    m_limitTripped = false;
}

bool LuaSandbox::executeString(const std::string& scriptCode) {
    if (!m_initialized || !m_luaState) {
        m_lastError = "Lua sandbox is not initialized; cannot execute script";
        ROWL_LOG_ERROR("Lua Sandbox executeString called without initialization!");
        return false;
    }

    // Reset instruction counter before each execution
    m_lastError.clear();
    if (!checkRunAllowed("executeString", scriptCode.size())) return false;
    resetInstructionCounter();

    int loadStatus = luaL_loadstring(m_luaState, scriptCode.c_str());
    if (loadStatus != LUA_OK) {
        std::string err = takeLuaError(m_luaState);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        ROWL_LOG_ERROR("Lua Script Syntax Error: " + err);
        return false;
    }

    // Protected call (lua_pcall) prevents script crashes from killing engine process
    int callStatus = lua_pcall(m_luaState, 0, 0, 0);
    if (callStatus != LUA_OK) {
        std::string err = takeLuaError(m_luaState);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        // A1 (H31): a failing script may already have polluted shared stdlib
        // tables before erroring — repair (rebinds the bridge internally).
        repairGlobals();
        ROWL_LOG_WARN("Lua Script Runtime Exception (Caught Safely): " + err);
        return false;
    }

    // Scripts may create globals freely, but cannot permanently replace the
    // engine bridge used by subsequent component scripts or conditions.
    // A1 (H31): same repair on the success path — pollution needs no error.
    repairGlobals();
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
    if (!checkRunAllowed("loadModule", scriptCode.size())) return false;

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

    resetInstructionCounter();
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
        // Env is discarded, but shared globals may be polluted already.
        repairGlobals();
        return false;
    }

    if (replacesExisting) {
        luaL_unref(m_luaState, LUA_REGISTRYINDEX, m_modules.at(moduleId));
    }
    lua_pushvalue(m_luaState, environmentIndex);
    m_modules[moduleId] = luaL_ref(m_luaState, LUA_REGISTRYINDEX);
    lua_pop(m_luaState, 1); // environment
    // A1 (H31+H26): the module chunk ran against shared tables (repair is
    // safe here — loads are rare, never per-frame) and may have planted an
    // env-local `rowl` impostor via rawset (sweep it).
    repairGlobals();
    sweepModuleEnvRowl(moduleId);
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
    resetInstructionCounter();
    if (lua_pcall(m_luaState, 1, 0, 0) != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        ROWL_LOG_ERROR("Lua component lifecycle callback '" + moduleId + "." + functionName + "' failed: " + m_lastError);
        lua_pop(m_luaState, 2); // error, environment
        bindEngineApis();
        // A1 (H26): the failed callback may have planted an env-local `rowl`
        // impostor before erroring. No repairGlobals here — module callbacks
        // are per-frame paths (H31 cost ban); repair runs on code-load paths.
        sweepModuleEnvRowl(moduleId);
        return false;
    }
    lua_pop(m_luaState, 1); // environment
    bindEngineApis();
    // A1 (H26): a successful callback can plant the impostor just as well.
    sweepModuleEnvRowl(moduleId);
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
    resetInstructionCounter();
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

    m_scriptVariables.clear();
    m_initialGlobals.clear();
    m_initialized = false;
    ROWL_LOG_INFO("Lua Environment Shutdown Complete.");
}

} // namespace Rowl::Scripting
