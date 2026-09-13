/**
 * test_lua_sandbox.cpp — Lua sandbox limits and lifecycle isolation.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_lua_sandbox() {
    TEST_SECTION("Lua 5.4 Sandbox & Security Subsystem");

    Rowl::Scripting::LuaSandbox lua;
    if (!lua.initialize() || !lua.isInitialized()) {
        std::cerr << "Lua init failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua Sandbox Initialization");

    // Safe execution
    if (!lua.executeString("x = 10 + 20; y = math.sqrt(100);")) {
        std::cerr << "Lua math exec failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Standard Math & Basic Arithmetic Execution");

    // Engine bridge variables
    lua.executeString("rowl.var_set('affinity_evelyn', '95')");
    std::string val = lua.getVariable("affinity_evelyn");
    if (val != "95") {
        std::cerr << "Lua var bridge mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Engine Variable Bridge (rowl.var_set / getVariable)");

    // Blacklist check: os, io, debug must be nil
    if (!lua.executeString("if os ~= nil then error('os library is not sandboxed!') end")) exit(1);
    if (!lua.executeString("if io ~= nil then error('io library is not sandboxed!') end")) exit(1);
    if (!lua.executeString("if debug ~= nil then error('debug library is not sandboxed!') end")) exit(1);
    TEST_PASS("Security Sandbox Isolation (os, io, debug blacklisted)");

    if (!lua.executeString(
            "if dofile ~= nil or loadfile ~= nil or load ~= nil or collectgarbage ~= nil then "
            "error('base library escape hatch is exposed') end")) {
        std::cerr << "Lua base library escape hatch remained available" << std::endl;
        exit(1);
    }
    if (!lua.executeString("rowl = 'overwritten'") ||
        !lua.executeString("rowl.var_set('bridge_integrity', 'ok')") ||
        lua.getVariable("bridge_integrity") != "ok") {
        std::cerr << "Lua bridge was not restored after script global mutation" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua File/Runtime Load APIs Blocked and Bridge Restored");

    if (!lua.executeString("function on_enter(dt) rowl.var_set('entered', tostring(dt)) end") ||
        !lua.callOptionalFunction("on_enter", 0.25) || lua.getVariable("entered") != "0.25" ||
        !lua.callOptionalFunction("missing_callback")) {
        std::cerr << "Lua lifecycle callback dispatch failed" << std::endl;
        exit(1);
    }
    if (!lua.executeString("function on_exit() error('isolated lifecycle error') end") ||
        lua.callOptionalFunction("on_exit")) {
        std::cerr << "Lua lifecycle error isolation failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Optional Lua Lifecycle Callback Dispatch and Error Isolation");

    // Component modules keep their callbacks and globals separate. This lets a
    // node own multiple script components without source order deciding which
    // on_update/on_exit function survives.
    if (!lua.loadModule("first", R"(
        private_value = "first"
        function on_enter() rowl.var_set("module_first_enter", private_value) end
        function on_update(dt) rowl.var_set("module_first_update", tostring(dt)) end
        function on_exit() rowl.var_set("module_exit_order", "first") end
    )") ||
        !lua.loadModule("second", R"(
        private_value = "second"
        function on_enter() rowl.var_set("module_second_enter", private_value) end
        function on_update(dt) rowl.var_set("module_second_update", tostring(dt * 2)) end
        function on_exit() rowl.var_set("module_exit_order", "second") end
    )") || lua.getModuleCount() != 2 ||
        !lua.callOptionalModuleFunction("first", "on_enter") ||
        !lua.callOptionalModuleFunction("second", "on_enter") ||
        lua.getVariable("module_first_enter") != "first" ||
        lua.getVariable("module_second_enter") != "second" ||
        !lua.callOptionalModuleFunction("first", "on_update", 0.25) ||
        !lua.callOptionalModuleFunction("second", "on_update", 0.25) ||
        lua.getVariable("module_first_update") != "0.25" ||
        lua.getVariable("module_second_update") != "0.5") {
        std::cerr << "Lua component module isolation or lifecycle dispatch failed" << std::endl;
        exit(1);
    }
    if (!lua.loadModule("guarded", R"(
        _G.rowl = "component-local overwrite"
        if getmetatable(_G) ~= false then error("component environment is mutable") end
        function on_enter() rowl.var_set("module_guarded", "ok") end
    )") || !lua.callOptionalModuleFunction("guarded", "on_enter") ||
        lua.getVariable("module_guarded") != "ok" || lua.getModuleCount() != 3 ||
        !lua.unloadModule("second") || lua.getModuleCount() != 2 ||
        lua.callOptionalModuleFunction("second", "on_enter")) {
        std::cerr << "Lua component module boundary or unload failed" << std::endl;
        exit(1);
    }
    lua.clearModules();
    if (lua.getModuleCount() != 0) {
        std::cerr << "Lua component module cleanup failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Isolated Lua Component Modules, Lifecycle Dispatch, and Cleanup");

    // Infinite loop protection (Instruction counter hook)
    if (lua.executeString("while true do local a = 1 end")) {
        std::cerr << "Lua infinite loop was not blocked!" << std::endl;
        exit(1);
    }
    TEST_PASS("Infinite Loop Defense (10M Instruction Limit Hook)");

    // Lua Condition Evaluation
    lua.setGlobalNumber("player_gold", 75.0);
    if (!lua.evaluateCondition("player_gold >= 50")) {
        std::cerr << "Lua condition player_gold >= 50 failed" << std::endl;
        exit(1);
    }
    if (lua.evaluateCondition("player_gold > 100")) {
        std::cerr << "Lua condition player_gold > 100 failed" << std::endl;
        exit(1);
    }
    if (!lua.evaluateCondition("player_gold == 75 and 10 > 5")) {
        std::cerr << "Lua compound condition failed" << std::endl;
        exit(1);
    }
    if (!lua.evaluateCondition("true") || lua.evaluateCondition("false")) {
        std::cerr << "Lua boolean literal condition failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Dynamic Expression & Condition Evaluation (evaluateCondition)");

    lua.clearVariables();
    if (!lua.getVariable("player_gold").empty() || !lua.evaluateCondition("player_gold == nil")) {
        std::cerr << "Lua variable reset left stale globals behind" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua Variable Reset Clears Script Globals");

    // MS-3a: a never-initialized (dead) sandbox fails closed — every condition
    // is false and an error is recorded, even for the "true" literal.
    {
        Rowl::Scripting::LuaSandbox dead;
        if (dead.evaluateCondition("x > 5")) {
            std::cerr << "MS-3: dead sandbox opened a branch (fail-open)" << std::endl;
            exit(1);
        }
        if (dead.getLastError().empty()) {
            std::cerr << "MS-3: dead sandbox recorded no error" << std::endl;
            exit(1);
        }
        if (dead.evaluateCondition("true") || dead.evaluateCondition("") ||
            dead.getLastError().empty()) {
            std::cerr << "MS-3: dead sandbox passed a literal/empty condition" << std::endl;
            exit(1);
        }
        if (dead.executeString("x = 1") || dead.getLastError().empty()) {
            std::cerr << "MS-3: dead sandbox executed a script" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("MS-3 Fail-Closed Evaluation on Uninitialized Sandbox");

    // MS-3b: the `rowl` bridge and system tables cannot be overwritten — neither
    // through direct script assignment nor through the variable API.
    if (!lua.executeString("rowl = nil") ||
        !lua.executeString("rowl.var_set('ms3_bridge_after_nil', 'ok')") ||
        lua.getVariable("ms3_bridge_after_nil") != "ok") {
        std::cerr << "MS-3: `rowl = nil` broke the engine bridge" << std::endl;
        exit(1);
    }
    if (!lua.executeString("rowl = 123") ||
        !lua.executeString("rowl.var_set('ms3_bridge_after_num', 'ok')") ||
        lua.getVariable("ms3_bridge_after_num") != "ok") {
        std::cerr << "MS-3: `rowl = 123` broke the engine bridge" << std::endl;
        exit(1);
    }
    lua.setVariable("rowl", "direct-overwrite");
    lua.setVariable("math", "clobbered");
    lua.setGlobalNumber("rowl", 1.0);
    if (!lua.getVariable("rowl").empty() || !lua.getVariable("math").empty()) {
        std::cerr << "MS-3: reserved name accepted by the variable API" << std::endl;
        exit(1);
    }
    if (!lua.executeString("if math.sqrt(16) ~= 4 then error('math was clobbered') end") ||
        !lua.executeString("rowl.var_set('ms3_bridge_after_api', 'ok')") ||
        lua.getVariable("ms3_bridge_after_api") != "ok") {
        std::cerr << "MS-3: reserved-name write damaged the sandbox" << std::endl;
        exit(1);
    }
    TEST_PASS("MS-3 Reserved `rowl`/System-Table Protection");

    // MS-3b (numbers): '.' is the only decimal separator regardless of process
    // locale; a comma value stays a verbatim string.
    lua.setVariable("ms3_locale_dot", "3.5");
    if (!lua.evaluateCondition("ms3_locale_dot == 3.5")) {
        std::cerr << "MS-3: dotted decimal was not parsed as a number" << std::endl;
        exit(1);
    }
    lua.setVariable("ms3_locale_comma", "3,14");
    if (lua.getVariable("ms3_locale_comma") != "3,14" ||
        !lua.evaluateCondition("ms3_locale_comma == '3,14'")) {
        std::cerr << "MS-3: comma value was locale-parsed instead of kept verbatim" << std::endl;
        exit(1);
    }
    TEST_PASS("MS-3 Locale-Independent Number Parsing");

    // MS-3c: script-created globals must not leak across session boundaries.
    if (!lua.executeString("ms3_stray_global = 98765; function ms3_stray_fn() return 1 end")) {
        std::cerr << "MS-3: setup script failed" << std::endl;
        exit(1);
    }
    lua.setVariable("ms3_tracked", "session-a");
    lua.clearVariables();
    if (!lua.getVariable("ms3_stray_global").empty() || !lua.getVariable("ms3_tracked").empty()) {
        std::cerr << "MS-3: script globals leaked across clearVariables()" << std::endl;
        exit(1);
    }
    if (!lua.evaluateCondition("ms3_stray_global == nil") ||
        !lua.evaluateCondition("ms3_stray_fn == nil")) {
        std::cerr << "MS-3: stray script global survived the session boundary" << std::endl;
        exit(1);
    }
    lua.setVariable("ms3_next_session", "b");
    if (lua.getVariable("ms3_next_session") != "b" ||
        !lua.executeString("rowl.var_set('ms3_bridge_next', 'alive')") ||
        lua.getVariable("ms3_bridge_next") != "alive") {
        std::cerr << "MS-3: sandbox unusable after session reset" << std::endl;
        exit(1);
    }
    lua.clearVariables();
    TEST_PASS("MS-3 Session Isolation Without Global Leaks");

    lua.shutdown();
    if (lua.isInitialized()) exit(1);
    // A shut-down sandbox is dead: conditions fail closed with an error.
    if (lua.evaluateCondition("x > 5") || lua.getLastError().empty()) {
        std::cerr << "MS-3: post-shutdown sandbox did not fail closed" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua Sandbox Clean Shutdown");
}
