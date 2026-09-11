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

    lua.shutdown();
    if (lua.isInitialized()) exit(1);
    TEST_PASS("Lua Sandbox Clean Shutdown");
}
