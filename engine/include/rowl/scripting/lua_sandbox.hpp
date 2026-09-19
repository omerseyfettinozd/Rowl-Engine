#pragma once

#include <chrono>
#include <string>
#include <memory>
#include <mutex>
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
    std::size_t getModuleCount() const;
    /// Calls a global lifecycle callback when it exists. Missing callbacks are successful no-ops.
    bool callOptionalFunction(const std::string& functionName, double deltaTime = 0.0);
    void shutdown();

    void setVariable(const std::string& key, const std::string& value);
    std::string getVariable(const std::string& key) const;
    void setGlobalNumber(const std::string& key, double value);
    double getGlobalNumber(const std::string& key, double defaultValue = 0.0) const;

    bool evaluateCondition(const std::string& conditionExpr);

    /// D4 (#45): condition side-effect purity. evaluateCondition() snapshots
    /// the variable map on entry; the guard's dtor rolls back anything the
    /// condition wrote via rowl.var_set (added keys erased + global nilled,
    /// modified keys replayed through setVariable()). Read-only conditions
    /// hit the == fast-path and stay silent. A-scope only: the variable map
    /// and its globals. Raw non-map Lua globals a condition plants directly
    /// (e.g. `x = 1`) are NOT rolled back — documented residual; conditions
    /// are authored content, not hostile code.
    struct ConditionPurityGuard {
        explicit ConditionPurityGuard(LuaSandbox* owner, const std::string& expr)
            : m_owner(owner), m_expr(expr),
              m_snapshot(owner->m_scriptVariables),
              m_bytes(owner->m_variablesBytes) {}
        ~ConditionPurityGuard() {
            m_owner->rollbackConditionVariables(m_snapshot, m_bytes,
                                                m_expr.c_str());
        }
        ConditionPurityGuard(const ConditionPurityGuard&) = delete;
        ConditionPurityGuard& operator=(const ConditionPurityGuard&) = delete;
    private:
        LuaSandbox* m_owner;
        std::string m_expr;
        std::unordered_map<std::string, std::string> m_snapshot;
        std::size_t m_bytes;
    };

    /// The most recent module compile or lifecycle error. This is intentionally
    /// diagnostic-only: callers must still use the boolean return value as the
    /// authority for an operation's success.
    const std::string& getLastError() const { return m_lastError; }

    /// B7 (#27): which failure phase produced the current m_lastError.
    /// Syntax = load-time (ROWL_SCRIPT_SYNTAX_ERROR), Runtime = everything
    /// else (ROWL_SCRIPT_RUNTIME_ERROR). Diagnostic-only alongside
    /// getLastError(); the engine wrapper maps it to ResultCode.
    enum class ConditionPhase { None, Syntax, Runtime };
    ConditionPhase getLastConditionPhase() const { return m_lastConditionPhase; }

    const std::unordered_map<std::string, std::string>& getAllVariables() const { return m_scriptVariables; }
    void clearVariables();

    bool isInitialized() const { return m_initialized; }

    /// Marks the instruction budget as exhausted. Called by the instruction
    /// hook when a script trips the limit; entry points refuse further runs
    /// until clearVariables() resets the session. Internal (hook access).
    void tripInstructionLimit() { m_limitTripped = true; }
    /// B7 (#31): records one instruction-limit trip, returns the new
    /// consecutive-trip streak. The hook's own counter reset writes single
    /// trips back, so only a tight catch-and-respin tightens the streak.
    /// Internal (hook access).
    unsigned noteInstructionTrip();
    /// B7: poisons the session with a reason (fail-closed until
    /// clearVariables()). Internal (hook access).
    void poisonSession(const std::string& reason);
    /// B7 (#24): polls the armed wall-clock deadline. Returns false (with
    /// outError set) when the in-flight callback exceeded its budget.
    /// Disarmed deadline = always true. Internal (hook access).
    bool checkWallDeadline(std::string& outError);

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
    /// B7 (#29/#39): full environment quarantine shared by repairGlobals()
    /// and repairStringMetatable(): restores the string metatable, reinstalls
    /// the setmetatable guard, and sweeps the extended shadow list. Runs under
    /// RecoveryScope — never throws out.
    void quarantineEnvironment();
    /// B7 (#35): restores the string metatable's __index to the pristine
    /// string library. A hostile `("").upper = f` shadows stdlib string
    /// methods process-wide; repairGlobals() replaces the table but the
    /// metatable survives, so it is restored explicitly here.
    void repairStringMetatable();
    /// B7 (#32): installs the __gc-rejecting setmetatable wrapper over the
    /// pristine base setmetatable. Idempotent; called by repairGlobals() and
    /// clearVariables(). Always runs under RecoveryScope.
    void installSetmetatableGuard();
    /// B7 (#24): fail-closed callback gates. checkCallbackAllowed() refuses
    /// poisoned sessions without consuming wall-clock time; armWallDeadline()
    /// stamps a 5s deadline before a script callback runs; checkWallDeadline()
    /// is polled by the instruction hook so a pcall-trapped infinite loop
    /// (B7 #31: uncatchable-error is impossible in single-threaded Lua)
    /// still terminates on wall time.
    bool checkCallbackAllowed(const char* what);
    void armWallDeadline();
    /// D4 (#45): surgical restore of the variable map after a condition ran.
    /// Map-identical snapshots return via fast-path; otherwise added keys are
    /// erased (map + global under RecoveryScope) and modified keys are
    /// replayed through setVariable(), with m_variablesBytes restored exactly.
    /// Never throws out (rollback runs on every evaluateCondition exit path).
    void rollbackConditionVariables(
        const std::unordered_map<std::string, std::string>& snapshot,
        std::size_t snapshotBytes, const char* exprForLog);

    lua_State* m_luaState = nullptr;
    // B7 (#34/#37): every public entry point locks this. The Lua C API is
    // single-threaded-unsafe; the recursive form lets host-side nested calls
    // (e.g. setVariable from a rowl.var bridge callback) re-enter safely.
    mutable std::recursive_mutex m_mutex;
    std::unordered_map<std::string, std::string> m_scriptVariables;
    std::unordered_map<std::string, int> m_modules;
    std::unordered_set<std::string> m_initialGlobals;
    std::string m_lastError;
    // B7 (#27): failure phase for the current m_lastError (None = no failure).
    ConditionPhase m_lastConditionPhase = ConditionPhase::None;
    bool m_initialized = false;
    // A1: instruction-limit poison (H24) + allocation quota (H25) state.
    bool m_limitTripped = false;
    std::size_t m_bytesAllocated = 0;
    // #28: true while post-pcall recovery runs (see RecoveryScope).
    bool m_inRecovery = false;
    // B7 (#22): host-side variable-map budget. Both a hostile script stuffing
    // the map via rowl.var.set and a chatty host caller feed this counter;
    // setVariable() rejects past the cap instead of growing unboundedly.
    std::size_t m_variablesBytes = 0;
    // B7 (#31): consecutive trip streak. A single trip is written back by the
    // hook's own counter reset; only a tight catch-and-respin tightens this.
    // Hitting the streak cap poisons the session (fail-closed); the message
    // rotates per streak so a finite-catch script records the escalation.
    unsigned m_tripStreak = 0;
    // B7 (#24): monotonic wall-clock deadline for the in-flight callback.
    // Default-constructed (disarmed) until armWallDeadline() stamps it.
    std::chrono::steady_clock::time_point m_entryDeadline{};
    bool m_deadlineArmed = false;
};

} // namespace Rowl::Scripting
