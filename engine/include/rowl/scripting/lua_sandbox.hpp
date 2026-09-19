#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace Rowl::Scripting {

class LuaSandbox {
public:
    LuaSandbox();
    ~LuaSandbox();

    bool initialize();
    bool executeString(const std::string& scriptCode);
    /// Loads a component script into an isolated environment. Global callback
    /// names inside one component cannot overwrite another component's names.
    bool loadModule(const std::string& moduleId, const std::string& scriptCode);
    /// Calls a lifecycle callback from one loaded component module. Missing
    /// callbacks are successful no-ops, matching callOptionalFunction().
    bool callOptionalModuleFunction(const std::string& moduleId,
                                    const std::string& functionName,
                                    double deltaTime = 0.0);
    /// Removes a loaded component module and releases its Lua registry entry.
    bool unloadModule(const std::string& moduleId);
    void clearModules();
    std::size_t getModuleCount() const { return m_modules.size(); }
    /// Calls a global lifecycle callback when it exists. Missing callbacks are successful no-ops.
    bool callOptionalFunction(const std::string& functionName, double deltaTime = 0.0);
    void shutdown();

    void setVariable(const std::string& key, const std::string& value);
    std::string getVariable(const std::string& key) const;
    void setGlobalNumber(const std::string& key, double value);
    double getGlobalNumber(const std::string& key, double defaultValue = 0.0) const;

    bool evaluateCondition(const std::string& conditionExpr);

    /// The most recent module compile or lifecycle error. This is intentionally
    /// diagnostic-only: callers must still use the boolean return value as the
    /// authority for an operation's success.
    const std::string& getLastError() const { return m_lastError; }

    const std::unordered_map<std::string, std::string>& getAllVariables() const { return m_scriptVariables; }
    void clearVariables();

    bool isInitialized() const { return m_initialized; }

    /// Marks the instruction budget as exhausted. Called by the instruction
    /// hook when a script trips the limit; entry points refuse further runs
    /// until clearVariables() resets the session. Internal (hook access).
    void tripInstructionLimit() { m_limitTripped = true; }

    /// #28: RAII recovery scope. Post-pcall recovery (repairGlobals,
    /// bindEngineApis, sweepModuleEnvRowl, sweepStrayGlobals,
    /// resetInstructionCounter) allocates outside any pcall; when a hostile
    /// script pins the quota at its ceiling those allocations would raise a
    /// second Lua error through C++ frames (UB/crash). Inside the scope
    /// quotaAlloc grants a reserve no script-side allocation can consume, so
    /// recovery cannot OOM. Internal (recovery-path use only).
    struct RecoveryScope {
        explicit RecoveryScope(LuaSandbox* owner) : m_owner(owner) {
            m_saved = owner->m_inRecovery;
            owner->m_inRecovery = true;
        }
        ~RecoveryScope() { m_owner->m_inRecovery = m_saved; }
        RecoveryScope(const RecoveryScope&) = delete;
        RecoveryScope& operator=(const RecoveryScope&) = delete;
    private:
        LuaSandbox* m_owner;
        bool m_saved;
    };

private:
    static void* quotaAlloc(void* ud, void* ptr, size_t osize, size_t nsize);
    /// Fail-closed entry gate: refuses poisoned sessions and oversized code.
    bool checkRunAllowed(const char* what, std::size_t codeBytes);
    /// #28: was a file-static free function; made a member so entry
    /// accounting runs inside the recovery reserve (quota-pinned sessions
    /// must survive it — it executes outside any pcall).
    void resetInstructionCounter();
    void bindEngineApis();
    /// Records the global names owned by the sandbox itself (safe libraries,
    /// base functions, engine bridge). clearVariables() removes every other
    /// global so script-created names cannot leak across sessions.
    void snapshotInitialGlobals();
    /// Removes every global outside m_initialGlobals (lua_next-safe:
    /// collect-then-clear). Shared by clearVariables() and repairGlobals().
    void sweepStrayGlobals();
    /// Bridge and standard-library names a script must never overwrite via
    /// setVariable()/setGlobalNumber().
    static bool isReservedVariableName(const std::string& key);
    /// A1 (H26): removes an impostor `rowl` key a module planted in its own
    /// environment via rawset (which bypasses the __newindex guard). Must use
    /// rawset, never setfield — the guard silently swallows `rowl` writes.
    void sweepModuleEnvRowl(const std::string& moduleId);
    /// A1 (H31): replaces shared stdlib tables a script polluted in place
    /// (math/string/table via fresh requiref, base funcs via pristine
    /// re-registration), re-nils the blacklist, clears a hostile global-table
    /// metatable, and rebinds the bridge. Legitimate script globals, the
    /// variable map, and the H24 poison are preserved — repair is not a
    /// session boundary. Code-load paths only, never per-frame/condition.
    void repairGlobals();

    lua_State* m_luaState = nullptr;
    std::unordered_map<std::string, std::string> m_scriptVariables;
    std::unordered_map<std::string, int> m_modules;
    std::unordered_set<std::string> m_initialGlobals;
    std::string m_lastError;
    bool m_initialized = false;
    // A1: instruction-limit poison (H24) + allocation quota (H25) state.
    bool m_limitTripped = false;
    std::size_t m_bytesAllocated = 0;
    // #28: true while post-pcall recovery runs (see RecoveryScope).
    bool m_inRecovery = false;
};

} // namespace Rowl::Scripting
