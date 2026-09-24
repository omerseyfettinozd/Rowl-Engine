#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/scripting/lua_condition_purity.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/util/locale_independent_parse.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
static std::size_t luaMemoryBytes(lua_State* state) {
    const int kib = lua_gc(state, LUA_GCCOUNT);
    const int remainder = lua_gc(state, LUA_GCCOUNTB);
    return static_cast<std::size_t>(kib) * 1024u + static_cast<std::size_t>(remainder);
}
constexpr std::size_t kMaxLoadedModules = 128;
constexpr std::size_t kMaxModuleIdBytes = 256;
// A1: sandbox resource budgets (H24/H25). Instruction hook trips at 10M;
// poisoned sessions refuse further runs until clearVariables(). Memory quota
// is enforced by quotaAlloc; oversized source is rejected at entry.
constexpr std::size_t kMaxLuaMemoryBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxScriptBytes = 256u * 1024u;
// B7 (#24): wall-clock ceiling for one script callback. The instruction hook
// polls it, so a pcall-trapped infinite loop (whose limit error stays
// catchable by design) still terminates on wall time.
constexpr auto kCallbackWallBudget = std::chrono::seconds(5);
// B7 (#22): host-side variable-map budget (keys + values, bytes). Rejects
// further setVariable() past the cap instead of growing unboundedly.
constexpr std::size_t kMaxVariableMapBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxVariableKeyBytes = 256u;
constexpr std::size_t kMaxVariableValueBytes = 64u * 1024u;
// B7 (#31): consecutive catch-and-respin trips before the session is
// poisoned. A finite-catch script records the per-streak escalation.
constexpr unsigned kMaxTripStreak = 64;

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

// Lua's pattern matcher runs inside one C call, so the VM hook cannot stop
// catastrophic backtracking. Bound the work before entering the stdlib call.
static int boundedPatternCall(lua_State* L) {
    size_t subjectBytes = 0;
    size_t patternBytes = 0;
    luaL_checklstring(L, 1, &subjectBytes);
    const char* pattern = luaL_checklstring(L, 2, &patternBytes);
    const bool plainFind = lua_toboolean(L, lua_upvalueindex(2)) && lua_toboolean(L, 4);
    if (plainFind) {
        if (subjectBytes > 1024u * 1024u || patternBytes > 256)
            return luaL_error(L, "Lua pattern work limit exceeded");
    } else {
        unsigned quantifiers = 0;
        for (size_t i = 0; i < patternBytes; ++i) {
            if (pattern[i] == '%' && i + 1 < patternBytes) { ++i; continue; }
            if (pattern[i] == '[') {
                while (++i < patternBytes && pattern[i] != ']') {
                    if (pattern[i] == '%' && i + 1 < patternBytes) ++i;
                }
                continue;
            }
            if (pattern[i] == '*' || pattern[i] == '+' || pattern[i] == '-' || pattern[i] == '?')
                ++quantifiers;
        }
        if (patternBytes > 256 || quantifiers > 2 ||
            (subjectBytes > 65536 && quantifiers == 0) ||
            (subjectBytes > 4096 && quantifiers == 1) ||
            (subjectBytes > 128 && quantifiers == 2))
            return luaL_error(L, "Lua pattern work limit exceeded");
    }
    const int arguments = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, arguments, LUA_MULTRET);
    return lua_gettop(L);
}

static void boundStringPatterns(lua_State* L) {
    lua_getglobal(L, "string");
    for (const char* name : {"find", "match", "gmatch", "gsub"}) {
        lua_getfield(L, -1, name);
        lua_pushboolean(L, std::strcmp(name, "find") == 0);
        lua_pushcclosure(L, boundedPatternCall, 2);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
}

// Instruction counter hook - counts accumulated instructions
static void lua_instruction_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;

    // B7 (#24): wall-clock ceiling first. A script-side pcall can trap the
    // instruction-limit error and respin (#31: uncatchable-error is impossible
    // in single-threaded Lua, so the limit error stays catchable by design);
    // wall time is trappable by nothing and terminates the callback anyway.
    lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);
    LuaSandbox* sandbox = static_cast<LuaSandbox*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (sandbox) {
        std::string wallError;
        if (!sandbox->checkWallDeadline(wallError)) {
            sandbox->tripInstructionLimit();
            lua_pushstring(L, wallError.c_str());
            lua_error(L);
            return;
        }
    }

    // Retrieve instruction count from registry
    lua_getfield(L, LUA_REGISTRYINDEX, "_rowl_instruction_count");
    uint64_t count = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    count += 100000; // Called every 100K instructions

    if (count > 10000000) { // 10M total instruction limit
        // A1 (H24): the limit error is catchable by a script-side pcall, so a
        // hostile script could catch-and-respin forever. Poison the session:
        // entry points refuse further runs until clearVariables() resets it.
        // B7 (#31): only a tight respin tightens the streak — a single trip
        // is written back by the hook's own counter reset below, so the
        // streak naturally decays to 1 for isolated trips. Hitting the cap
        // poisons the session fail-closed; the message rotates per streak so
        // a finite-catch script records the escalation before the poison.
        if (sandbox) {
            sandbox->tripInstructionLimit();
            unsigned streak = sandbox->noteInstructionTrip();
            std::string msg = "Lua sandbox instruction limit exceeded (max 10,000,000 instructions). Possible infinite loop detected!";
            if (streak > 1) {
                msg += " [respin " + std::to_string(streak) + "/" + std::to_string(kMaxTripStreak) + "]";
            }
            if (streak >= kMaxTripStreak) {
                sandbox->poisonSession("consecutive instruction-limit respin budget exhausted");
                msg += " Session poisoned.";
            }
            lua_pushstring(L, msg.c_str());
        } else {
            lua_pushstring(L, "Lua sandbox instruction limit exceeded (max 10,000,000 instructions). Possible infinite loop detected!");
        }
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
    // Lua 5.4 passes a type tag, not an allocation size, for fresh objects.
    // The old size is meaningful only when ptr names a live allocation.
    if (!ptr) osize = 0;
    if (nsize == 0) {
        if (sandbox && osize <= sandbox->m_bytesAllocated) sandbox->m_bytesAllocated -= osize;
        else if (sandbox) sandbox->m_bytesAllocated = 0;
        free(ptr);
        return nullptr;
    }
    const size_t grown = (nsize > osize) ? (nsize - osize) : 0;
    size_t cap = kMaxLuaMemoryBytes;
    if (sandbox && sandbox->m_inRecovery) cap += kRecoveryReserveBytes;
    if (sandbox && grown &&
        (sandbox->m_bytesAllocated > cap || grown > cap - sandbox->m_bytesAllocated))
        return nullptr;
    void* resized = realloc(ptr, nsize);
    if (resized && sandbox) {
        if (nsize > osize) sandbox->m_bytesAllocated += nsize - osize;
        else sandbox->m_bytesAllocated -= std::min(sandbox->m_bytesAllocated, osize - nsize);
    }
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

// B7 (#26): fail-closed gate for every script-callback path
// (callOptionalFunction, callOptionalModuleFunction, evaluateCondition).
// Poisoned sessions refuse without touching Lua state or the wall clock,
// so a poisoned session can never launder itself through a callback.
bool LuaSandbox::checkCallbackAllowed(const char* what) {
    if (m_limitTripped) {
        m_lastError = std::string("Lua callback refused for ") + what +
            "; session poisoned until clearVariables()";
        m_lastConditionPhase = ConditionPhase::Runtime;
        ROWL_LOG_ERROR(m_lastError);
        return false;
    }
    return true;
}

// B7 (#24): stamps a monotonic 5s deadline for the in-flight callback.
// Called after the poison gate, before any script runs.
void LuaSandbox::armWallDeadline() {
    m_entryDeadline = std::chrono::steady_clock::now() + kCallbackWallBudget;
    m_deadlineArmed = true;
}

bool LuaSandbox::checkWallDeadline(std::string& outError) {
    if (!m_deadlineArmed) return true;
    if (std::chrono::steady_clock::now() <= m_entryDeadline) return true;
    m_deadlineArmed = false;
    outError = "Lua sandbox wall-clock budget exceeded (max 5s per callback). Possible infinite loop detected!";
    return false;
}

// B7 (#31): consecutive-trip accounting for the hook. Returns the new streak.
unsigned LuaSandbox::noteInstructionTrip() {
    return ++m_tripStreak;
}

void LuaSandbox::poisonSession(const std::string& reason) {
    m_limitTripped = true;
    m_lastError = "Lua sandbox session poisoned: " + reason + " (until clearVariables())";
    m_lastConditionPhase = ConditionPhase::Runtime;
    ROWL_LOG_ERROR(m_lastError);
}

// Names owned by the sandbox bridge or the Lua standard libraries. A script
// must never replace these through the variable API; direct global assignment
// inside a script is additionally repaired by bindEngineApis().
bool LuaSandbox::isReservedVariableName(const std::string& key) {
    static const std::unordered_set<std::string_view> kReserved = {
        "rowl", "_G", "_ENV", "_VERSION",
        "math", "string", "table", "coroutine", "utf8",
        "package", "io", "os", "debug",
        "dofile", "loadfile", "load", "collectgarbage", "require", "module",
        // #71: kalan Lua 5.4 taban-kutuphane adlari. setVariable/
        // setGlobalNumber (ve icinden gecen rowl.var_set) dogrudan _G'ye
        // yazar; bu adlar listede yokken host koprusu pcall/tostring'i ezip
        // clearVariables sinirinda nil-olu birakiyordu (quarantine
        // luaopen_base calistirmaz). print/warn cagrilabilir kalir (B7 #24);
        // rezerv yalnizca host-uzerine-yazmayi reddeder.
        "assert", "error", "getmetatable", "setmetatable",
        "ipairs", "pairs", "next", "pcall", "xpcall",
        "print", "warn", "select", "tonumber", "tostring", "type",
        "rawget", "rawset", "rawequal", "rawlen",
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
    // luaL_newstate allocated its base state with Lua's original allocator.
    // Include those live bytes before switching to the quota allocator.
    m_bytesAllocated = luaMemoryBytes(m_luaState);
    m_limitTripped = false;
    // B7 (#22/#24/#27/#31): fresh-session accounting. Budgets and poison live
    // per lua_State, so a reused object must not inherit the old session's.
    m_variablesBytes = 0;
    m_tripStreak = 0;
    m_deadlineArmed = false;
    m_lastConditionPhase = ConditionPhase::None;
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
    boundStringPatterns(m_luaState);
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
    // B7 (#32): capture the pristine base setmetatable before the guard wraps
    // it. The registry is unreachable without the debug library, so no script
    // can tamper with this capture; installSetmetatableGuard() always wraps
    // this copy, never the (possibly replaced) current global.
    lua_getglobal(m_luaState, "setmetatable");
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_pristine_setmetatable");
    installSetmetatableGuard();
    // D6 (#158): capture the pristine base rawset before the guard wraps it
    // (same pattern as setmetatable above — the registry is unreachable
    // without the debug library, and installRawsetGuard() always wraps this
    // copy, never the current global).
    lua_getglobal(m_luaState, "rawset");
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_pristine_rawset");
    installRawsetGuard();
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

    // D6 (#158): publish the verified bridge reference. Module callbacks pin
    // this table into their environment before the pcall (pre-pcall scrub+pin)
    // and the guarded rawset compares impostor candidates against it, so
    // bridge resolution never depends on an env key a script can plant.
    lua_getglobal(m_luaState, "rowl");
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_bridge");
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

// D6 (#158): pins the verified bridge into a module env (rawset from C,
// which bypasses the __newindex guard by design — the same path the sweep
// uses). Afterwards `rowl` inside the module resolves to the registry
// reference without consulting __index, so a planted key cannot win.
void LuaSandbox::pinVerifiedRowlIntoEnv(int envIndex) {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    const int env = lua_absindex(m_luaState, envIndex);
    lua_getfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_bridge");
    if (lua_istable(m_luaState, -1) == 0) {
        lua_pop(m_luaState, 1); // no verified bridge; leave the env alone
        return;
    }
    lua_pushstring(m_luaState, "rowl");
    lua_pushvalue(m_luaState, -2); // bridge
    lua_rawset(m_luaState, env);
    lua_pop(m_luaState, 1); // bridge
}

// D6 (#158): pre-pcall scrub+pin. Deletes any env-local `rowl` impostor an
// earlier run planted (belt-and-braces alongside the post-pcall sweep) and
// pins the verified bridge, closing the intra-callback TOCTOU window the
// post-hoc sweep could not reach.
void LuaSandbox::scrubAndPinModuleEnvRowl(const std::string& moduleId) {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    const auto module = m_modules.find(moduleId);
    if (module == m_modules.end()) return;
    lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, module->second); // env
    lua_pushstring(m_luaState, "rowl");
    lua_pushnil(m_luaState);
    lua_rawset(m_luaState, -3); // delete any impostor (C rawset: no guard)
    pinVerifiedRowlIntoEnv(-1);
    lua_pop(m_luaState, 1); // env
}

// B7 (#22/#28-class): host-driven global commit behind a pcall. The pushes
// and the settable run under the caller's RecoveryScope, and the pcall turns
// any residual allocation failure into a catchable error — the map commit in
// setVariable()/setGlobalNumber() only happens on LUA_OK, so a quota-pinned
// Lua state can never leave the map and the globals split-brained.
// Upvalue 1 = key (string), upvalue 2 = value (number or string).
static int lua_host_commit_global(lua_State* L) {
    lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_settable(L, -3);
    return 0;
}

// B7 (#32): __gc-rejecting setmetatable wrapper. A script calling
// setmetatable(t, {__gc = f}) would otherwise smuggle a finalizer into the
// state — a second execution context outside hook/quota supervision (a
// finalizer runs at GC time, not inside any pcall the sandbox monitors).
// Upvalue 1 = the pristine base setmetatable captured at initialize().
static int lua_guarded_setmetatable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "__gc");
        const bool hasGc = lua_isnil(L, -1) == 0;
        lua_pop(L, 1);
        if (hasGc) {
            return luaL_error(L, "rowl sandbox: __gc metamethods are not allowed");
        }
    } else if (lua_isnoneornil(L, 2) == 0) {
        luaL_checktype(L, 2, LUA_TTABLE); // pristine type error for bad 2nd arg
    }
    lua_pushvalue(L, lua_upvalueindex(1)); // pristine setmetatable
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

// D6 (#158): rawset wrapper. The module __newindex guard swallows plain
// `rowl = fake`, but the pristine rawset bypasses it by design —
// rawset(_G, "rowl", fake) planted an env-local impostor that won every
// rowl.* lookup for the rest of the SAME pcall (the post-hoc sweep ran too
// late). The wrapper compares any "rowl" write against the verified bridge
// in the registry and swallows non-bridge values (returning the table, like
// rawset); every other call delegates to the pristine rawset captured at
// initialize(). Upvalue 1 = pristine rawset. `rowl` is a reserved name no
// legitimate module writes, so the swallow changes nothing legitimate.
static int lua_guarded_rawset(lua_State* L) {
    const int top = lua_gettop(L);
    if (top >= 3 && lua_type(L, 2) == LUA_TSTRING) {
        size_t len = 0;
        const char* key = lua_tolstring(L, 2, &len);
        if (key && len == 4 && std::memcmp(key, "rowl", 4) == 0) {
            lua_getfield(L, LUA_REGISTRYINDEX, "_rowl_bridge");
            const bool isBridge = lua_rawequal(L, 3, -1) != 0;
            lua_pop(L, 1);
            if (!isBridge) {
                lua_settop(L, 1); // swallow the plant; return the table
                return 1;
            }
        }
    }
    lua_pushvalue(L, lua_upvalueindex(1)); // pristine rawset
    lua_insert(L, 1);
    lua_call(L, top, LUA_MULTRET);
    return lua_gettop(L);
}

void LuaSandbox::installSetmetatableGuard() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    // Stacking protection: wrapping the wrapper would nest closures and chain
    // the pristine as upvalues. quarantineEnvironment() resets this marker
    // before calling here, so a set marker means an install already active.
    lua_getfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_setmeta_guarded");
    const bool already = lua_toboolean(m_luaState, -1) != 0;
    lua_pop(m_luaState, 1);
    if (already) return;
    // The pristine comes from the registry capture, never from the current
    // global — a script may have replaced the global with its own function,
    // and wrapping that would launder hostile power through the guard.
    lua_getfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_pristine_setmetatable");
    if (lua_isfunction(m_luaState, -1) == 0) {
        lua_pop(m_luaState, 1); // no pristine captured; leave base as-is
        return;
    }
    lua_pushcclosure(m_luaState, lua_guarded_setmetatable, 1);
    lua_setglobal(m_luaState, "setmetatable");
    lua_pushboolean(m_luaState, 1);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_setmeta_guarded");
}

// D6 (#158): installs the rawset wrapper over the pristine base rawset.
// Idempotent; called by initialize() and quarantineEnvironment(). Always
// wraps the registry capture, never the current global — a script may have
// shadowed the global with its own function (module envs accept ordinary
// globals), and wrapping that would launder hostile power through the guard.
void LuaSandbox::installRawsetGuard() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    lua_getfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_rawset_guarded");
    const bool already = lua_toboolean(m_luaState, -1) != 0;
    lua_pop(m_luaState, 1);
    if (already) return;
    lua_getfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_pristine_rawset");
    if (lua_isfunction(m_luaState, -1) == 0) {
        lua_pop(m_luaState, 1); // no pristine captured; leave base as-is
        return;
    }
    lua_pushcclosure(m_luaState, lua_guarded_rawset, 1);
    lua_setglobal(m_luaState, "rawset");
    lua_pushboolean(m_luaState, 1);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_rawset_guarded");
}

// B7 (#35): restores the string metatable's __index to the pristine string
// library. `("").upper = f` shadows stdlib string methods process-wide via
// the shared string metatable; repairGlobals() replaces the string TABLE but
// the metatable survives it, so it is restored explicitly here. All strings
// share one metatable, so repairing through one value repairs every string.
void LuaSandbox::repairStringMetatable() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    lua_getglobal(m_luaState, "string"); // pristine (fresh requiref upstream)
    if (lua_istable(m_luaState, -1) == 0) {
        lua_pop(m_luaState, 1);
        return;
    }
    lua_pushstring(m_luaState, "");
    bool hasTable = false;
    if (lua_getmetatable(m_luaState, -1) != 0) {
        hasTable = lua_istable(m_luaState, -1) != 0;
    }
    if (hasTable) {
        lua_pushvalue(m_luaState, -3); // pristine string library
        lua_setfield(m_luaState, -2, "__index");
        lua_pop(m_luaState, 1); // metatable
    } else {
        lua_pop(m_luaState, 1); // false / hostile non-table value
        lua_newtable(m_luaState); // fresh metatable, shared by all strings
        lua_pushvalue(m_luaState, -3); // pristine string library
        lua_setfield(m_luaState, -2, "__index");
        lua_setmetatable(m_luaState, -2); // C-API set: bypasses the wrapper
    }
    lua_pop(m_luaState, 1); // ""
    lua_pop(m_luaState, 1); // string table
}

// B7 (#29/#39): full environment quarantine shared by repairGlobals() and
// clearVariables(). Sweeps the extended env-local shadow list (module envs
// chaining to _G resolve `os` etc. through their __index — killing the source
// globals unshadows every env at once), restores the string metatable,
// reinstalls the setmetatable guard, and rebinds the bridge. Runs under
// RecoveryScope — never throws out.
void LuaSandbox::quarantineEnvironment() {
    if (!m_luaState) return;
    const RecoveryScope recovery(this);
    repairStringMetatable();
    lua_pushboolean(m_luaState, 0);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_setmeta_guarded");
    installSetmetatableGuard();
    // load/loadfile/dofile/collectgarbage are base funcs that luaopen_base
    // reintroduces on every repair — the sweep re-nils them each time.
    // print/warn stay available by compatibility decision (B7 #24: the wall
    // clock, not output denial, is the stall backstop).
    static const char* kShadow[] = {
        "io", "os", "debug", "package",
        "dofile", "loadfile", "load", "collectgarbage",
        "require", "module",
    };
    for (const char* name : kShadow) {
        lua_pushnil(m_luaState);
        lua_setglobal(m_luaState, name);
    }
    bindEngineApis();
    // D6 (#158): luaopen_base reintroduces the pristine rawset on every
    // repair — re-wrap it (marker reset mirrors the setmetatable guard
    // above; the bridge ref is fresh again after bindEngineApis()).
    lua_pushboolean(m_luaState, 0);
    lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_rawset_guarded");
    installRawsetGuard();
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
    boundStringPatterns(m_luaState);
    luaL_requiref(m_luaState, "table", luaopen_table, 1);
    lua_pop(m_luaState, 1);
    luaL_requiref(m_luaState, "_G", luaopen_base, 1);
    lua_pop(m_luaState, 1);
    // B7 (#29/#39/#32/#35): luaopen_base reintroduces the shadowed base
    // funcs (re-nil them) and restores the pristine setmetatable global over
    // the wrapper — quarantineEnvironment() sweeps the shadows, restores the
    // string metatable, reinstalls the guard, and rebinds the bridge.
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
    quarantineEnvironment();
}

void LuaSandbox::setVariable(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (isReservedVariableName(key)) {
        ROWL_LOG_WARN("Lua Sandbox rejected reserved variable name: '" + key + "'");
        return;
    }
    // B7 (#22): host-side variable-map budget. Both a hostile script stuffing
    // the map via rowl.var.set and a chatty host caller feed this counter;
    // past the cap the write is rejected instead of growing unboundedly.
    // Embedded NULs are rejected too: lua_pushstring would truncate them and
    // leave the map and the globals split-brained on the same key.
    if (key.size() > kMaxVariableKeyBytes || value.size() > kMaxVariableValueBytes ||
        key.size() != std::strlen(key.c_str())) {
        ROWL_LOG_WARN("Lua Sandbox rejected oversized variable: '" + key + "'");
        return;
    }
    const std::size_t entryBytes = key.size() + value.size();
    std::size_t oldBytes = 0;
    if (const auto existing = m_scriptVariables.find(key); existing != m_scriptVariables.end()) {
        oldBytes = existing->first.size() + existing->second.size();
    }
    if (m_variablesBytes - oldBytes + entryBytes > kMaxVariableMapBytes) {
        ROWL_LOG_WARN("Lua Sandbox variable-map budget exhausted; rejected: '" + key + "'");
        return;
    }
    // B7 (#22/#28-class bonus): Lua-first commit. The old code wrote the map
    // unconditionally, then pushed the global — and a host-side lua_pushstring
    // can throw straight through C++ frames when the quota is pinned at its
    // ceiling (unprotected-throw UB). The commit runs behind a pcall under
    // RecoveryScope; the map is only updated on LUA_OK.
    if (m_luaState) {
        const RecoveryScope recovery(this);
        double num = 0.0;
        lua_pushstring(m_luaState, key.c_str());
        if (parseSandboxNumber(value, num)) {
            lua_pushnumber(m_luaState, num);
        } else {
            lua_pushstring(m_luaState, value.c_str());
        }
        lua_pushcclosure(m_luaState, lua_host_commit_global, 2);
        if (lua_pcall(m_luaState, 0, 0, 0) != LUA_OK) {
            const std::string err = takeLuaError(m_luaState);
            lua_pop(m_luaState, 1);
            m_lastError = err;
            m_lastConditionPhase = ConditionPhase::Runtime;
            ROWL_LOG_WARN("Lua Sandbox variable commit failed for '" + key + "': " + err);
            return;
        }
    }
    m_variablesBytes = m_variablesBytes - oldBytes + entryBytes;
    m_scriptVariables[key] = value;
    ROWL_LOG_TRACE("Lua Sandbox Variable Set: '" + key + "' = '" + value + "'");
}

std::string LuaSandbox::getVariable(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_scriptVariables.find(key);
    if (it != m_scriptVariables.end()) {
        return it->second;
    }
    if (m_luaState) {
        // B7 (#28-class): even a read interns (allocates) when the key is new,
        // and a hostile _G metatable could run code — RecoveryScope keeps a
        // quota-pinned state from throwing through these C++ frames.
        // D07: ham-okuma (d07_rawGetGlobal) — koşulun ektiği _G __index
        // (D06 ad-snapshot'ına görünmez) kayıp-anahtarda ateşlenemez; mevcut
        // anahtarlarda getglobal ile birebir aynı değer döner.
        const RecoveryScope recovery(const_cast<LuaSandbox*>(this));
        d07_rawGetGlobal(m_luaState, key.c_str());
        if (lua_isstring(m_luaState, -1) || lua_isnumber(m_luaState, -1)) {
            const char* raw = lua_tostring(m_luaState, -1);
            const std::string val = raw ? raw : "";
            lua_pop(m_luaState, 1);
            return val;
        }
        lua_pop(m_luaState, 1);
    }
    return "";
}

void LuaSandbox::setGlobalNumber(const std::string& key, double value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (isReservedVariableName(key)) {
        ROWL_LOG_WARN("Lua Sandbox rejected reserved variable name: '" + key + "'");
        return;
    }
    if (!std::isfinite(value)) {
        ROWL_LOG_WARN("Lua Sandbox rejected non-finite numeric variable: '" + key + "'");
        return;
    }
    // B7 (#22): same budget and Lua-first commit as setVariable(). A number
    // serializes short, but the map budget counts it all the same.
    const std::string text = std::to_string(value);
    if (key.size() > kMaxVariableKeyBytes || text.size() > kMaxVariableValueBytes ||
        key.size() != std::strlen(key.c_str())) {
        ROWL_LOG_WARN("Lua Sandbox rejected oversized numeric variable: '" + key + "'");
        return;
    }
    const std::size_t entryBytes = key.size() + text.size();
    std::size_t oldBytes = 0;
    if (const auto existing = m_scriptVariables.find(key); existing != m_scriptVariables.end()) {
        oldBytes = existing->first.size() + existing->second.size();
    }
    if (m_variablesBytes - oldBytes + entryBytes > kMaxVariableMapBytes) {
        ROWL_LOG_WARN("Lua Sandbox variable-map budget exhausted; rejected: '" + key + "'");
        return;
    }
    if (m_luaState) {
        const RecoveryScope recovery(this);
        lua_pushstring(m_luaState, key.c_str());
        lua_pushnumber(m_luaState, value);
        lua_pushcclosure(m_luaState, lua_host_commit_global, 2);
        if (lua_pcall(m_luaState, 0, 0, 0) != LUA_OK) {
            const std::string err = takeLuaError(m_luaState);
            lua_pop(m_luaState, 1);
            m_lastError = err;
            m_lastConditionPhase = ConditionPhase::Runtime;
            ROWL_LOG_WARN("Lua Sandbox numeric commit failed for '" + key + "': " + err);
            return;
        }
    }
    m_variablesBytes = m_variablesBytes - oldBytes + entryBytes;
    m_scriptVariables[key] = text;
    ROWL_LOG_TRACE("Lua Sandbox Number Set: '" + key + "' = " + text);
}

double LuaSandbox::getGlobalNumber(const std::string& key, double defaultValue) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_luaState) {
        const RecoveryScope recovery(const_cast<LuaSandbox*>(this));
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

// D4 (#45): surgical variable-map restore after a condition ran. Fast-path
// on map-identical snapshots (read-only conditions stay silent). Otherwise
// added keys are erased from the map with their global nilled under
// RecoveryScope, and modified/deleted keys are replayed through
// setVariable() (Lua-first commit, budget-checked, no-throw — snapshot keys
// fit the budget by construction). m_variablesBytes is assigned exactly, not
// recomputed, so the byte counter cannot drift across evaluations. Runs on
// every evaluateCondition exit path via ConditionPurityGuard; never throws.
void LuaSandbox::rollbackConditionVariables(
    const std::unordered_map<std::string, std::string>& snapshot,
    std::size_t snapshotBytes, const char* exprForLog) {
    if (m_scriptVariables == snapshot && m_variablesBytes == snapshotBytes) {
        return;
    }
    std::size_t restored = 0;
    for (auto it = m_scriptVariables.begin(); it != m_scriptVariables.end();) {
        if (snapshot.find(it->first) == snapshot.end()) {
            const std::string key = it->first;
            it = m_scriptVariables.erase(it);
            if (m_luaState) {
                const RecoveryScope recovery(this);
                lua_pushnil(m_luaState);
                lua_setglobal(m_luaState, key.c_str());
            }
            ++restored;
        } else {
            ++it;
        }
    }
    for (const auto& [key, value] : snapshot) {
        const auto current = m_scriptVariables.find(key);
        if (current == m_scriptVariables.end() || current->second != value) {
            setVariable(key, value);
            ++restored;
        }
    }
    m_variablesBytes = snapshotBytes;
    std::string expr = (exprForLog != nullptr) ? exprForLog : "";
    if (expr.size() > 64) {
        expr = expr.substr(0, 64) + "...";
    }
    ROWL_LOG_WARN("Lua condition side-effect rolled back (" +
                  std::to_string(restored) + " var(s)): '" + expr + "'");
}

bool LuaSandbox::evaluateCondition(const std::string& conditionExpr) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lastError.clear();
    m_lastConditionPhase = ConditionPhase::None;
    // Fail-closed: an uninitialized or broken sandbox must never open a
    // conditional branch. This check stays above the literal fast-path so even
    // "true" cannot pass on a dead sandbox.
    if (!m_initialized || !m_luaState) {
        m_lastError = "Lua sandbox is not initialized; failing closed on condition evaluation";
        m_lastConditionPhase = ConditionPhase::Runtime;
        ROWL_LOG_WARN("Lua Sandbox evaluateCondition called without initialization. Failing closed (false).");
        return false;
    }
    if (conditionExpr.empty() || conditionExpr == "true" || conditionExpr == "1") {
        return true;
    }
    if (conditionExpr == "false" || conditionExpr == "0") {
        return false;
    }
    // B7 (#26): poisoned sessions refuse before touching Lua state.
    if (!checkCallbackAllowed("condition")) return false;

    // D4 (#45): purity guard — snapshot before any Lua runs; the dtor rolls
    // back rowl.var_set writes on every exit path below (result, syntax
    // error, runtime error, oversize refuse). Literal fast-paths above stay
    // guard-free: they execute nothing.
    const ConditionPurityGuard purityGuard(this, conditionExpr);
    // D06: raw-global guard — _G name snapshot on entry; the dtor nils every
    // global the condition planted. Single-lock model (this function's lock;
    // no second mutex), no repairGlobals (per-frame forbidden).
    const D06ConditionGlobalGuard d06Guard(this);

    // B7 (#28-class residual): the old code reset the instruction counter with
    // a raw push/setfield pair — unprotected-throw UB on a quota-pinned
    // state. The member reset runs inside the recovery reserve.
    resetInstructionCounter();

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
            // B7 (#27): load-time failure — the engine wrapper maps this to
            // ROWL_SCRIPT_SYNTAX_ERROR instead of the old single-branch sink.
            m_lastConditionPhase = ConditionPhase::Syntax;
            ROWL_LOG_WARN("Lua Condition syntax error in '" + conditionExpr + "': " + err);
            return false;
        }
    }

    // B7 (#24): arm the wall clock around the run only — loadstring executes
    // nothing, so arming earlier would bill parse time against script time.
    // The hook polls it; disarm immediately after the pcall returns.
    armWallDeadline();
    int callStatus = lua_pcall(m_luaState, 0, 1, 0);
    m_deadlineArmed = false;
    if (callStatus != LUA_OK) {
        std::string err = takeLuaError(m_luaState);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        m_lastConditionPhase = ConditionPhase::Runtime;
        bindEngineApis();
        ROWL_LOG_WARN("Lua Condition runtime error in '" + conditionExpr + "': " + err);
        return false;
    }

    bool result = lua_toboolean(m_luaState, -1) != 0;
    lua_pop(m_luaState, 1);
    bindEngineApis();
    // B7 (#31): a clean run proves the streak's trips were isolated, not a
    // tight catch-and-respin — decay it back to zero.
    m_tripStreak = 0;
    return result;
}

void LuaSandbox::clearVariables() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_luaState) {
        const RecoveryScope recovery(this);
        sweepStrayGlobals();
        for (const auto& [key, value] : m_scriptVariables) {
            (void)value;
            lua_pushnil(m_luaState);
            lua_setglobal(m_luaState, key.c_str());
        }
        // B7 (#23/#33): a session boundary runs the FULL quarantine, not just
        // a bridge rebind. Env-local shadows (#29/#39), the string-metatable
        // poison (#35), and a replaced setmetatable (#32) all survive a bare
        // sweep+rebind — quarantineEnvironment() resets each of them, so the
        // next session starts from a known-good environment.
        // #71: quarantine luaopen_base calistirmaz — nil'lenmis taban
        // fonksiyonlar (rezerv-oncesi harita artiklari ya da onarimsiz
        // kosul-yolundan sizanlar) sinirda olu kalir. repairGlobals tabani
        // luaopen_base ile yeniden acar, kuyrugundaki quarantineEnvironment
        // (:710) golgeleri/korumalari/kopruyu tazeler.
        repairGlobals();
        // Release unreachable values from the old session, then reconcile
        // with Lua's live-byte accounting before the next session starts.
        lua_gc(m_luaState, LUA_GCCOLLECT);
        m_bytesAllocated = luaMemoryBytes(m_luaState);
    }
    m_scriptVariables.clear();
    // A1 (H24): a new session boundary lifts the instruction-limit poison.
    m_limitTripped = false;
    // B7: ...together with every other per-session budget (a reused object
    // must not inherit the old session's streak, deadline, or map bytes).
    m_variablesBytes = 0;
    m_tripStreak = 0;
    m_deadlineArmed = false;
    m_lastConditionPhase = ConditionPhase::None;
}

bool LuaSandbox::executeString(const std::string& scriptCode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_luaState) {
        m_lastError = "Lua sandbox is not initialized; cannot execute script";
        m_lastConditionPhase = ConditionPhase::Runtime;
        ROWL_LOG_ERROR("Lua Sandbox executeString called without initialization!");
        return false;
    }

    // Reset instruction counter before each execution
    m_lastError.clear();
    m_lastConditionPhase = ConditionPhase::None;
    if (!checkRunAllowed("executeString", scriptCode.size())) {
        m_lastConditionPhase = ConditionPhase::Runtime;
        return false;
    }
    resetInstructionCounter();

    int loadStatus = luaL_loadstring(m_luaState, scriptCode.c_str());
    if (loadStatus != LUA_OK) {
        std::string err = takeLuaError(m_luaState);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        m_lastConditionPhase = ConditionPhase::Syntax;
        ROWL_LOG_ERROR("Lua Script Syntax Error: " + err);
        return false;
    }

    // Protected call (lua_pcall) prevents script crashes from killing engine process
    // B7 (#24): wall-clock deadline around the run (heavy C-call scripts can
    // burn minutes of wall time under the 10M-instruction ceiling — the hook
    // only polls between VM instructions).
    armWallDeadline();
    int callStatus = lua_pcall(m_luaState, 0, 0, 0);
    m_deadlineArmed = false;
    if (callStatus != LUA_OK) {
        std::string err = takeLuaError(m_luaState);
        lua_pop(m_luaState, 1);
        m_lastError = err;
        m_lastConditionPhase = ConditionPhase::Runtime;
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
    m_tripStreak = 0;
    return true;
}

bool LuaSandbox::loadModule(const std::string& moduleId, const std::string& scriptCode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lastError.clear();
    m_lastConditionPhase = ConditionPhase::None;
    if (!m_initialized || !m_luaState || moduleId.empty() ||
        moduleId.size() > kMaxModuleIdBytes || scriptCode.empty()) {
        m_lastError = "Invalid Lua component module input";
        m_lastConditionPhase = ConditionPhase::Runtime;
        return false;
    }

    // Replacing a module is deliberate (e.g. editor hot scene update), but a
    // newly created scene cannot allocate an unbounded number of environments.
    const bool replacesExisting = m_modules.contains(moduleId);
    if (!replacesExisting && m_modules.size() >= kMaxLoadedModules) {
        m_lastError = "Lua component module limit exceeded (max 128)";
        m_lastConditionPhase = ConditionPhase::Runtime;
        ROWL_LOG_ERROR(m_lastError);
        return false;
    }
    if (!checkRunAllowed("loadModule", scriptCode.size())) {
        m_lastConditionPhase = ConditionPhase::Runtime;
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

    resetInstructionCounter();
    const int loadStatus = luaL_loadbufferx(m_luaState, scriptCode.data(), scriptCode.size(),
                                            moduleId.c_str(), "t");
    if (loadStatus != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        m_lastConditionPhase = ConditionPhase::Syntax;
        ROWL_LOG_ERROR("Lua component syntax error in '" + moduleId + "': " + m_lastError);
        lua_pop(m_luaState, 2); // error, environment
        return false;
    }

    // The first upvalue of a Lua chunk is _ENV. Setting it before pcall makes
    // globals declared by this source private to the component.
    lua_pushvalue(m_luaState, environmentIndex);
    if (lua_setupvalue(m_luaState, -2, 1) == nullptr) {
        m_lastError = "Lua component could not bind its isolated environment";
        m_lastConditionPhase = ConditionPhase::Runtime;
        lua_pop(m_luaState, 2); // chunk, environment
        return false;
    }
    // B7 (#24): wall-clock deadline around the chunk run (see executeString).
    // D6 (#158): pin the verified bridge into the fresh env BEFORE the chunk
    // runs — the chunk itself could rawset-plant an impostor and consume it
    // in the same run, a TOCTOU the post-load sweep cannot reach.
    pinVerifiedRowlIntoEnv(environmentIndex);
    armWallDeadline();
    const int modulePcallStatus = lua_pcall(m_luaState, 0, 0, 0);
    m_deadlineArmed = false;
    if (modulePcallStatus != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        m_lastConditionPhase = ConditionPhase::Runtime;
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
    m_tripStreak = 0;
    return true;
}

bool LuaSandbox::callOptionalModuleFunction(const std::string& moduleId,
                                            const std::string& functionName,
                                            double deltaTime) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_lastError.clear();
    m_lastConditionPhase = ConditionPhase::None;
    if (!m_initialized || !m_luaState || functionName.empty()) {
        m_lastError = "Lua sandbox is unavailable";
        m_lastConditionPhase = ConditionPhase::Runtime;
        return false;
    }
    // B7 (#26): the poison gate stands BEFORE module lookup — a poisoned
    // session refuses even a missing callback (fail-closed beats the
    // successful-no-op contract once the session is known-bad).
    if (!checkCallbackAllowed(("module callback " + moduleId + "." + functionName).c_str())) return false;
    const auto module = m_modules.find(moduleId);
    if (module == m_modules.end()) {
        m_lastError = "Lua component module is not loaded";
        m_lastConditionPhase = ConditionPhase::Runtime;
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
        m_lastConditionPhase = ConditionPhase::Runtime;
        lua_pop(m_luaState, 2);
        ROWL_LOG_WARN("Lua component lifecycle callback is not a function: " + moduleId + "." + functionName);
        return false;
    }
    lua_pushnumber(m_luaState, deltaTime);
    resetInstructionCounter();
    // D6 (#158): pre-pcall scrub+pin — evict any env-local `rowl` impostor
    // and resolve this callback against the verified registry bridge, so a
    // rawset plant inside THIS pcall is swallowed by the guarded rawset and
    // can no longer divert rowl.* mid-callback. The post-pcall sweep below
    // stays as belt-and-braces.
    scrubAndPinModuleEnvRowl(moduleId);
    // B7 (#24): wall-clock deadline around the run; disarmed the moment the
    // pcall returns so bookkeeping never bills the next callback.
    armWallDeadline();
    const int pcallStatus = lua_pcall(m_luaState, 1, 0, 0);
    m_deadlineArmed = false;
    if (pcallStatus != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        m_lastError = rawError ? rawError : "unknown Lua error";
        m_lastConditionPhase = ConditionPhase::Runtime;
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
    m_tripStreak = 0;
    return true;
}

bool LuaSandbox::unloadModule(const std::string& moduleId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const auto module = m_modules.find(moduleId);
    if (module == m_modules.end()) return false;
    if (m_luaState) luaL_unref(m_luaState, LUA_REGISTRYINDEX, module->second);
    m_modules.erase(module);
    return true;
}

void LuaSandbox::clearModules() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_luaState) {
        for (const auto& [moduleId, reference] : m_modules) {
            (void)moduleId;
            luaL_unref(m_luaState, LUA_REGISTRYINDEX, reference);
        }
    }
    m_modules.clear();
}

std::size_t LuaSandbox::getModuleCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_modules.size();
}

bool LuaSandbox::callOptionalFunction(const std::string& functionName, double deltaTime) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_luaState || functionName.empty()) return false;
    // B7 (#26): poison gate before the no-op fast-path (see
    // callOptionalModuleFunction for the rationale).
    if (!checkCallbackAllowed(("callback " + functionName).c_str())) return false;

    // D07: ham-okuma (d07_rawGetGlobal) — koşulun ektiği _G __index kayıp
    // callback çözümünde ateşlenemez (no-op sözleşmesi script kodu
    // çalıştırmaz); mevcut callback'ler rawget ile birebir çözülür.
    // B7 (#28-class): pushstring kota-pinned durumda ayırabilir — RecoveryScope
    // rezervi altında (getVariable emsali). :850 getGlobalNumber KAPSAM-DIŞI.
    const RecoveryScope recovery(this);
    d07_rawGetGlobal(m_luaState, functionName.c_str());
    if (lua_isnil(m_luaState, -1)) {
        lua_pop(m_luaState, 1);
        return true;
    }
    if (!lua_isfunction(m_luaState, -1)) {
        lua_pop(m_luaState, 1);
        m_lastError = "Lua lifecycle callback is not a function: " + functionName;
        m_lastConditionPhase = ConditionPhase::Runtime;
        ROWL_LOG_WARN("Lua lifecycle callback is not a function: " + functionName);
        return false;
    }

    lua_pushnumber(m_luaState, deltaTime);
    resetInstructionCounter();
    armWallDeadline();
    const int pcallStatus = lua_pcall(m_luaState, 1, 0, 0);
    m_deadlineArmed = false;
    if (pcallStatus != LUA_OK) {
        const char* rawError = lua_tostring(m_luaState, -1);
        const std::string error = rawError ? rawError : "unknown Lua error";
        lua_pop(m_luaState, 1);
        m_lastError = error;
        m_lastConditionPhase = ConditionPhase::Runtime;
        bindEngineApis();
        ROWL_LOG_ERROR("Lua lifecycle callback '" + functionName + "' failed: " + error);
        return false;
    }
    bindEngineApis();
    m_tripStreak = 0;
    return true;
}

void LuaSandbox::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Sandboxed Lua Environment...");

    // Clean up registry entries
    if (m_luaState) {
        clearModules();
        // B7 (#32): unhook BEFORE close. A count hook firing through
        // lua_close's internal GC steps would lua_error into teardown —
        // fatal, since no pcall frames remain below it.
        lua_sethook(m_luaState, nullptr, 0, 0);
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, SANDBOX_REGISTRY_KEY);
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_instruction_count");
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_bridge");
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_pristine_rawset");
        lua_pushnil(m_luaState);
        lua_setfield(m_luaState, LUA_REGISTRYINDEX, "_rowl_rawset_guarded");

        lua_close(m_luaState);
        m_luaState = nullptr;
    }

    m_scriptVariables.clear();
    m_initialGlobals.clear();
    // B7: per-session budgets die with the state (see initialize()).
    m_bytesAllocated = 0;
    m_variablesBytes = 0;
    m_tripStreak = 0;
    m_deadlineArmed = false;
    m_lastConditionPhase = ConditionPhase::None;
    m_limitTripped = false;
    m_initialized = false;
    ROWL_LOG_INFO("Lua Environment Shutdown Complete.");
}

} // namespace Rowl::Scripting
