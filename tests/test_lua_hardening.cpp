/**
 * test_lua_hardening.cpp — B7 Lua-sandbox hardening slice adversarial tests.
 *
 * Covers Bulgular lua-group remainders: variable-map budget + Lua-first
 * commit (#22), full-quarantine session boundary (#23/#33), env-shadow sweep
 * (#29/#39), setmetatable __gc guard (#32), string-metatable repair (#35),
 * callback poison gates (#26), Syntax-vs-Runtime phase mapping (#27),
 * consecutive-trip respin budget (#31), wall-clock deadline (#24),
 * reserve-key state choke-point (#25/#30), legacy-graph script teardown +
 * shutdown status clear (#38). #34/#37 mutex: concurrent-hammer lock.
 * #36 (__APPLE__ path) is compile-time platform-ifdef — not testable on
 * Linux; noted as residual.
 *
 * Every test pins CWD to an empty temp dir (RAII): the engine's physical
 * story fallback probes the process CWD, and the suite CWD (repo root)
 * carries a real graph that would mask misses and perturb timing.
 */
#include "rowl_test_harness.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/state/game_state.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct ScopedEmptyCwd {
    std::filesystem::path saved;
    std::filesystem::path dir;
    bool ok = false;
    explicit ScopedEmptyCwd(const std::string& tag) {
        std::error_code ec;
        saved = std::filesystem::current_path(ec);
        if (ec) return;
        dir = std::filesystem::temp_directory_path() / tag;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        if (ec) return;
        std::filesystem::current_path(dir, ec);
        ok = !ec;
    }
    ~ScopedEmptyCwd() {
        std::error_code ec;
        if (ok) std::filesystem::current_path(saved, ec);
        std::filesystem::remove_all(dir, ec);
    }
};

RowlEngineHandle makeBareEngine() {
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || !RowlEngine_Init(handle, 320, 180, 0)) {
        std::cerr << "B7 setup: bare init failed" << std::endl;
        exit(1);
    }
    return handle;
}

bool freshSandbox(Rowl::Scripting::LuaSandbox& sandbox) {
    if (!sandbox.initialize()) {
        std::cerr << "B7 setup: sandbox init failed" << std::endl;
        exit(1);
    }
    return true;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

void test_lua_hardening() {
    ScopedEmptyCwd cwd("rowl_b7_lua_hardening");
    if (!cwd.ok) {
        std::cerr << "B7 setup: CWD pin failed" << std::endl;
        exit(1);
    }

    // #25/#30: reserved bridge/stdlib names never enter saved state, on both
    // write paths. Pre-fix both stored the key (dual-reality: Lua side
    // rejected, state side kept).
    {
        auto initial = Rowl::State::GameState::createInitialState(1);
        auto next = Rowl::State::GameState::createNextState(initial, 2, "rowl", "evil");
        if (next->getVariable("rowl") != "") {
            std::cerr << "B7 #25: reserved key 'rowl' entered saved state" << std::endl;
            exit(1);
        }
        if (next->stepId != initial->stepId + 1 || next->activeNodeId != 2) {
            std::cerr << "B7 #25: dropping a reserved key must not drop the transition" << std::endl;
            exit(1);
        }
        auto bulk = Rowl::State::GameState::createNextStateWithVariables(
            initial, 3, {{"math", "evil"}, {"ok", "1"}});
        if (bulk->getVariable("math") != "" || bulk->getVariable("ok") != "1") {
            std::cerr << "B7 #30: bulk path must strip reserved keys and keep legit ones" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Reserve-Key State Choke-Point (#25/#30)");
    }

    // #22: oversized values are rejected WITHOUT touching the map (Lua-first
    // commit). Pre-fix the map was written unconditionally, so the rejected
    // value lingered host-side (split-brain).
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        sandbox.setVariable("big", std::string(65 * 1024 + 1, 'x'));
        if (sandbox.getVariable("big") != "") {
            std::cerr << "B7 #22: oversized value leaked into the variable map" << std::endl;
            exit(1);
        }
        sandbox.setVariable("fine", "1");
        if (sandbox.getVariable("fine") != "1") {
            std::cerr << "B7 #22: legit write after a rejection must succeed" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Oversized Variable Rejected Without Map Commit (#22)");
    }

    // #22: the 4 MiB map budget stops unbounded growth. Pre-fix every write
    // was accepted (64 x 64 KiB would all land).
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        const std::string chunk(64 * 1024, 'v');
        int accepted = 0;
        for (int i = 0; i < 80; ++i) {
            sandbox.setVariable("fill" + std::to_string(i), chunk);
            if (sandbox.getVariable("fill" + std::to_string(i)) == chunk) ++accepted;
            else break;
        }
        if (accepted >= 80) {
            std::cerr << "B7 #22: variable-map budget accepted unbounded growth" << std::endl;
            exit(1);
        }
        if (accepted < 60) {
            std::cerr << "B7 #22: budget tripped too early (accepted=" << accepted << ")" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Variable-Map Budget Stops Unbounded Growth (#22)");
    }

    // #32: setmetatable with __gc is rejected; legit uses still work.
    // Pre-fix the __gc table was accepted (finalizer smuggling).
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        if (sandbox.executeString("setmetatable({}, {__gc = function() end})")) {
            std::cerr << "B7 #32: __gc metamethod was accepted" << std::endl;
            exit(1);
        }
        if (!contains(sandbox.getLastError(), "__gc")) {
            std::cerr << "B7 #32: rejection must name __gc, got: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        // evaluateCondition wraps bare expressions in return (...), where a
        // local declaration is illegal — use the return-prefixed raw form.
        if (!sandbox.evaluateCondition("return (function() local t = setmetatable({}, {__index = {x = 7}}); return t.x == 7 end)()")) {
            std::cerr << "B7 #32: legit setmetatable broke: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Setmetatable __gc Guard (#32)");
    }

    // #35: string-metatable poison repaired by the quarantine. A direct field
    // store on a literal ("("a").upper = ...") just errors in stock Lua; the
    // real primitive is mutating the shared metatable through getmetatable.
    // Bilerek-boz finding: this path was ALREADY green pre-fix — old
    // repairGlobals re-ran luaopen_string, which installs a fresh string
    // metatable as a side effect (red run: no #35 line, only #23-setmetatable
    // + hammer-crash proved red). B7's repairStringMetatable pins the
    // guarantee explicitly instead of relying on that side effect.
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        if (!sandbox.executeString("getmetatable(\"\").__index = {upper = function(self) return 'PWNED' end}")) {
            std::cerr << "B7 #35 setup: poison script failed: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        if (!sandbox.loadModule("repairs", "marker = 1")) {
            std::cerr << "B7 #35 setup: module load failed: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        // Colon-call: dot-call passes no self, and the pristine upper(s)
        // requires its argument — the hijack ignored self, which is exactly
        // why the wrong form would false-pass pre-fix.
        if (!sandbox.evaluateCondition("return (\"a\"):upper() == \"A\"")) {
            std::cerr << "B7 #35: string metatable stayed hijacked: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 String-Metatable Poison Repaired (#35)");
    }

    // #29/#39: a global stdlib shadow planted by one script is nilled by the
    // next load's quarantine, so later module envs resolve the name cleanly.
    // Pre-fix only the dofile/load quartet was re-nilled — `os` survived.
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        if (!sandbox.executeString("os = {evil = true}")) {
            std::cerr << "B7 #29 setup: shadow plant failed" << std::endl;
            exit(1);
        }
        if (!sandbox.loadModule("quar", "marker = 2")) {
            std::cerr << "B7 #29 setup: module load failed: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        if (!sandbox.evaluateCondition("return os == nil")) {
            std::cerr << "B7 #29: global stdlib shadow survived quarantine" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Env-Shadow Quarantine (#29/#39)");
    }

    // #23/#33: clearVariables is a FULL quarantine, not a rebind. Poison the
    // string metatable, shadow a stdlib global, and replace setmetatable —
    // the next session must start from a known-good environment. Pre-fix only
    // strays were swept, so all three survived the boundary.
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        sandbox.executeString("getmetatable(\"\").__index = {upper = function(self) return 'PWNED' end}");
        sandbox.executeString("debug = {evil = true}");
        sandbox.executeString("setmetatable = function(a, b) return a end");
        sandbox.clearVariables();
        if (!sandbox.evaluateCondition("return (\"a\"):upper() == \"A\"")) {
            std::cerr << "B7 #23: string-metatable poison survived the session boundary" << std::endl;
            exit(1);
        }
        if (!sandbox.evaluateCondition("return debug == nil")) {
            std::cerr << "B7 #23: stdlib shadow survived the session boundary" << std::endl;
            exit(1);
        }
        if (sandbox.executeString("setmetatable({}, {__gc = function() end})")) {
            std::cerr << "B7 #23: replaced setmetatable survived the session boundary" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Session Boundary Full Quarantine (#23/#33)");
    }

    // #27: Syntax-vs-Runtime phase mapping through the C ABI. A runtime throw
    // must report 9 (SCRIPT_RUNTIME_ERROR); a load-time break must report 8
    // (SCRIPT_SYNTAX_ERROR). Pre-fix BOTH reported 8 (single-branch sink).
    {
        RowlEngineHandle handle = makeBareEngine();
        if (RowlEngine_EvaluateCondition(handle, "nonexistent_fn_xyz()") != 0 ||
            RowlEngine_GetLastResultCode(handle) != ROWL_RESULT_SCRIPT_RUNTIME_ERROR) {
            std::cerr << "B7 #27: runtime throw did not map to code 9 (got "
                      << RowlEngine_GetLastResultCode(handle) << ")" << std::endl;
            exit(1);
        }
        if (RowlEngine_EvaluateCondition(handle, "(((") != 0 ||
            RowlEngine_GetLastResultCode(handle) != ROWL_RESULT_SCRIPT_SYNTAX_ERROR) {
            std::cerr << "B7 #27: syntax break did not map to code 8 (got "
                      << RowlEngine_GetLastResultCode(handle) << ")" << std::endl;
            exit(1);
        }
        if (RowlEngine_EvaluateCondition(handle, "1 + 1") != 1 ||
            RowlEngine_GetLastResultCode(handle) != ROWL_RESULT_OK) {
            std::cerr << "B7 #27: true condition must stay true with code 0" << std::endl;
            exit(1);
        }
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        TEST_PASS("B7 Condition Phase Mapping Syntax-8 Runtime-9 (#27)");
    }

    // #26 + H24: a poisoned session refuses even a missing callback
    // (fail-closed beats the successful-no-op contract once known-bad).
    // Pre-fix the missing callback returned true (no gate on the path).
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        if (sandbox.executeString("while true do end")) {
            std::cerr << "B7 #26 setup: infinite loop unexpectedly passed" << std::endl;
            exit(1);
        }
        if (sandbox.callOptionalFunction("definitely_missing_xyz")) {
            std::cerr << "B7 #26: poisoned session honored a callback" << std::endl;
            exit(1);
        }
        if (!contains(sandbox.getLastError(), "poisoned")) {
            std::cerr << "B7 #26: refusal must say poisoned, got: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Poisoned Session Refuses Callbacks (#26)");
    }

    // #31: in-run catch-and-respin escalates ([respin N/64]) and the streak
    // cap poisons the session — without hanging the run. Uncatchable-error is
    // impossible in single-threaded Lua, so the design contains instead of
    // preventing: 66 trips caught inside ONE run (only the first costs the
    // full 10M; later trips fire ~100K instructions after the previous one,
    // since the counter stays pinned above the ceiling). The marker scan runs
    // INSIDE the same script and reports out via rowl.var_set: by the time
    // the run completes the session is poisoned by design, so a post-hoc
    // evaluateCondition would be (correctly) refused by the #26 gate.
    // Pre-fix all 66 messages were identical (no streak, no cap).
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        const bool completed = sandbox.executeString(
            "trip_msgs = {}; for i = 1, 66 do\n"
            "  local ok, err = pcall(function() while true do end end)\n"
            "  if ok then break end\n"
            "  trip_msgs[#trip_msgs + 1] = err\n"
            "end\n"
            "local escalated = 'no'\n"
            "for i = 1, #trip_msgs do\n"
            "  if string.find(trip_msgs[i], 'respin 64/64', 1, true) then escalated = 'yes' end\n"
            "end\n"
            "rowl.var_set('b7_escalated', escalated)");
        if (!completed) {
            std::cerr << "B7 #31: finite-catch script did not complete: '"
                      << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        if (sandbox.getVariable("b7_escalated") != "yes") {
            std::cerr << "B7 #31: no trip message carried the streak-cap marker" << std::endl;
            exit(1);
        }
        // The cap poisoned the session: future callbacks refuse fail-closed.
        if (sandbox.callOptionalFunction("definitely_missing_xyz")) {
            std::cerr << "B7 #31: streak-capped session honored a callback" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Catch-And-Respin Budget: Escalation + Cap Poison (#31)");
    }

    // #24: the wall clock terminates heavy-C-call scripts that stay far under
    // the 10M-instruction ceiling. Each iteration burns ~2ms of allocator
    // time for ~30 VM instructions, so the hook (every 100K instructions)
    // polls the deadline regularly while the instruction counter barely
    // moves. Pre-fix this ran to completion; post-fix it fails at ~5s wall
    // with a wall-clock error.
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        const bool ok = sandbox.executeString(
            "local s = ''; for i = 1, 20000 do\n"
            "  s = string.rep('x', 1024 * 1024)\n"
            "  s = string.rep(s, 1)\n"
            "  local a, b, c, d, e = i + 1, i + 2, i + 3, i + 4, i + 5\n"
            "  s = s .. a .. b .. c .. d .. e\n"
            "end");
        if (ok) {
            std::cerr << "B7 #24: wall-budget script ran to completion" << std::endl;
            exit(1);
        }
        if (!contains(sandbox.getLastError(), "wall-clock")) {
            std::cerr << "B7 #24: failure must name the wall clock, got: '" << sandbox.getLastError() << "'" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Wall-Clock Deadline Terminates Heavy Scripts (#24)");
    }

    // #38: loading a legacy (component-free) graph tears down live scripts —
    // statuses clear and on_exit runs. Pre-fix the legacy branch skipped the
    // teardown, so statuses survived the graph switch.
    {
        RowlEngineHandle handle = makeBareEngine();
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"script","id":"legacy_probe","data":{"code":"function on_enter() rowl.var_set('legacy_probe_enter', 'yes') end; function on_exit() rowl.var_set('legacy_probe_exit', 'yes') end"}}
        ])");
        RowlEngine_Step(handle, 0.25f);
        if (std::string(RowlEngine_GetVariable(handle, "legacy_probe_enter")) != "yes" ||
            std::string(RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)) == "[]") {
            std::cerr << "B7 #38 setup: probe script did not activate" << std::endl;
            exit(1);
        }
        const std::string graphPath = (cwd.dir / "legacy_graph.json").string();
        {
            std::ofstream graph(graphPath, std::ios::binary);
            graph << R"({"format_version": 4, "start_node_id": 1, "nodes": [{"id": 1}]})";
        }
        RowlEngine_LoadStoryGraph(handle, graphPath.c_str());
        if (RowlEngine_GetLastStoryGraphError(handle)[0] != '\0') {
            std::cerr << "B7 #38 setup: legacy graph load failed: "
                      << RowlEngine_GetLastStoryGraphError(handle) << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)) != "[]") {
            std::cerr << "B7 #38: legacy graph load left script statuses live" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetVariable(handle, "legacy_probe_exit")) != "yes") {
            std::cerr << "B7 #38: legacy graph load skipped on_exit" << std::endl;
            exit(1);
        }
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        TEST_PASS("B7 Legacy Graph Load Tears Down Scripts (#38)");
    }

    // #34/#37: concurrent host-side hammer — threads pound variables,
    // conditions, and module calls on one sandbox. Pass = no crash, no
    // hang, coherent map. (Pre-fix this was a raw data race.)
    {
        Rowl::Scripting::LuaSandbox sandbox;
        freshSandbox(sandbox);
        sandbox.loadModule("hammer", "function on_update(dt) rowl.var_set('tick', tostring(dt)) end");
        std::atomic<bool> failed{false};
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&, t] {
                try {
                    for (int i = 0; i < 200; ++i) {
                        sandbox.setVariable("hammer_" + std::to_string(t), std::to_string(i));
                        sandbox.getVariable("hammer_" + std::to_string(t));
                        sandbox.evaluateCondition("return 1 + 1");
                        sandbox.callOptionalModuleFunction("hammer", "on_update", 0.016);
                    }
                } catch (...) {
                    failed.store(true);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        if (failed.load()) {
            std::cerr << "B7 #34: concurrent hammer threw" << std::endl;
            exit(1);
        }
        if (sandbox.getVariable("hammer_0") != "199" || sandbox.getVariable("hammer_3") != "199") {
            std::cerr << "B7 #34: map incoherent after concurrent hammer" << std::endl;
            exit(1);
        }
        TEST_PASS("B7 Concurrent Sandbox Hammer (#34/#37)");
    }
}
