/**
 * test_runtime_context_and_diagnostics.cpp — Runtime context and engine diagnostics.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_runtime_context_and_diagnostics() {
    TEST_SECTION("RuntimeContext & Structured Results Diagnostics");

    // 1. Independent Engine and RuntimeContext VFS Isolation
    {
        namespace fs = std::filesystem;
        const auto tempA = fs::temp_directory_path() / "rowl_vfs_iso_a";
        const auto tempB = fs::temp_directory_path() / "rowl_vfs_iso_b";
        fs::create_directories(tempA);
        fs::create_directories(tempB);

        {
            std::ofstream f(tempA / "alpha.txt");
            f << "file from isolated environment A";
        }
        {
            std::ofstream f(tempB / "beta.txt");
            f << "file from isolated environment B";
        }

        auto vfsA = std::make_shared<Rowl::VFS::VFSManager>();
        vfsA->mountDirectory("", tempA.string());

        auto vfsB = std::make_shared<Rowl::VFS::VFSManager>();
        vfsB->mountDirectory("", tempB.string());

        if (!vfsA->exists("alpha.txt") || vfsA->exists("beta.txt")) {
            std::cerr << "VFS A cross-contaminated with VFS B" << std::endl;
            exit(1);
        }
        if (!vfsB->exists("beta.txt") || vfsB->exists("alpha.txt")) {
            std::cerr << "VFS B cross-contaminated with VFS A" << std::endl;
            exit(1);
        }

        auto ctxA = std::make_shared<Rowl::Core::RuntimeContext>(vfsA);
        auto ctxB = std::make_shared<Rowl::Core::RuntimeContext>(vfsB);

        Rowl::Core::Engine engineA(ctxA);
        Rowl::Core::Engine engineB(ctxB);

        if (!engineA.getVfs()->exists("alpha.txt") || engineA.getVfs()->exists("beta.txt")) {
            std::cerr << "Engine A does not isolate its VFS" << std::endl;
            exit(1);
        }
        if (!engineB.getVfs()->exists("beta.txt") || engineB.getVfs()->exists("alpha.txt")) {
            std::cerr << "Engine B does not isolate its VFS" << std::endl;
            exit(1);
        }

        fs::remove_all(tempA);
        fs::remove_all(tempB);
        TEST_PASS("Independent Engine and RuntimeContext VFS Isolation");
    }

    // 2. Structured Diagnostics on Engine Save/Load/Graph/Script
    {
        namespace fs = std::filesystem;
        const auto saveDir = fs::temp_directory_path() / "rowl_diag_saves";
        fs::create_directories(saveDir);

        Rowl::Core::Engine engine;
        engine.setSaveDirectory(saveDir.string());
        engine.initialize({});

        // Save slot invalid bounds
        if (engine.saveGameSlot(-1)) {
            std::cerr << "saveGameSlot(-1) should have returned false" << std::endl;
            exit(1);
        }
        auto res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::InvalidArgument || res.operation != "save_game_slot") {
            std::cerr << "saveGameSlot(-1) did not set InvalidArgument result" << std::endl;
            exit(1);
        }

        // Save slot valid
        if (!engine.saveGameSlot(1)) {
            std::cerr << "saveGameSlot(1) failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk() || res.operation != "save_game_slot" || res.target != "1") {
            std::cerr << "saveGameSlot(1) did not set Success result" << std::endl;
            exit(1);
        }

        // Load slot nonexistent
        if (engine.loadGameSlot(99)) {
            std::cerr << "loadGameSlot(99) should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::FileNotFound || res.operation != "load_game_slot") {
            std::cerr << "loadGameSlot(99) did not set FileNotFound result" << std::endl;
            exit(1);
        }

        // Load slot valid
        if (!engine.loadGameSlot(1)) {
            std::cerr << "loadGameSlot(1) failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk() || res.operation != "load_game_slot") {
            std::cerr << "loadGameSlot(1) did not set Success result" << std::endl;
            exit(1);
        }

        // Delete slot valid
        if (!engine.deleteSaveSlot(1)) {
            std::cerr << "deleteSaveSlot(1) failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk() || res.operation != "delete_save_slot") {
            std::cerr << "deleteSaveSlot(1) did not set Success result" << std::endl;
            exit(1);
        }

        // Delete slot nonexistent
        if (engine.deleteSaveSlot(1)) {
            std::cerr << "deleteSaveSlot(1) second time should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::FileNotFound) {
            std::cerr << "deleteSaveSlot(1) second time did not set FileNotFound result" << std::endl;
            exit(1);
        }

        // Story Graph missing file
        if (engine.loadStoryGraphFromPath("/nonexistent_rowl_graph_path.json")) {
            std::cerr << "loadStoryGraphFromPath on nonexistent file should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::FileNotFound) {
            std::cerr << "loadStoryGraphFromPath missing file did not set FileNotFound" << std::endl;
            exit(1);
        }

        // Story Graph corrupt file
        const auto corruptGraph = fs::temp_directory_path() / "corrupt_graph.json";
        {
            std::ofstream f(corruptGraph);
            f << "{ invalid json content !!! }";
        }
        if (engine.loadStoryGraphFromPath(corruptGraph.string())) {
            std::cerr << "loadStoryGraphFromPath on corrupt file should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ParseError) {
            std::cerr << "loadStoryGraphFromPath corrupt file did not set ParseError" << std::endl;
            exit(1);
        }
        fs::remove(corruptGraph);

        // Story Graph semantic validation error (empty nodes array)
        const auto invalidGraph = fs::temp_directory_path() / "invalid_graph.json";
        {
            std::ofstream f(invalidGraph);
            f << "{\"nodes\": []}";
        }
        if (engine.loadStoryGraphFromPath(invalidGraph.string())) {
            std::cerr << "loadStoryGraphFromPath on empty nodes should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ValidationError) {
            std::cerr << "loadStoryGraphFromPath semantic error did not set ValidationError (got " << res.rawCode() << ")" << std::endl;
            exit(1);
        }
        fs::remove(invalidGraph);

        // Scripting invalid syntax
        if (engine.executeScript("this is definitely not lua syntax @#$!")) {
            std::cerr << "executeScript with bad syntax should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ScriptRuntimeError) {
            std::cerr << "executeScript did not set ScriptRuntimeError" << std::endl;
            exit(1);
        }

        // Scripting valid
        if (!engine.executeScript("rowl.var_set('diag_flag', 'confirmed')")) {
            std::cerr << "executeScript valid failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk()) {
            std::cerr << "executeScript valid did not set Ok" << std::endl;
            exit(1);
        }

        // Condition syntax error
        if (engine.evaluateCondition("bad condition @#$!")) {
            std::cerr << "evaluateCondition bad syntax should return false" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ScriptSyntaxError) {
            std::cerr << "evaluateCondition bad syntax did not set ScriptSyntaxError" << std::endl;
            exit(1);
        }

        // Valid condition evaluating to false must NOT be contaminated by previous errors
        if (engine.evaluateCondition("1 == 2")) {
            std::cerr << "evaluateCondition(1 == 2) should return false" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk()) {
            std::cerr << "evaluateCondition(1 == 2) contaminated with error: " << res.message << std::endl;
            exit(1);
        }

        engine.shutdown();
        fs::remove_all(saveDir);
        TEST_PASS("Engine Structured Results for Save, Load, Graph, and Scripting");
    }

    // 3. C-API Structured Diagnostics & Null Handle Safety
    {
        // Null handle queries must be safe and return InvalidHandle
        if (RowlEngine_GetLastResultCode(nullptr) != 1) {
            std::cerr << "RowlEngine_GetLastResultCode(nullptr) should return 1" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetLastResultOperation(nullptr)) != "none") {
            std::cerr << "RowlEngine_GetLastResultOperation(nullptr) failed" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetLastResultMessage(nullptr)).empty()) {
            std::cerr << "RowlEngine_GetLastResultMessage(nullptr) is empty" << std::endl;
            exit(1);
        }
        RowlEngine_ClearLastResult(nullptr); // Must be a safe no-op

        // Live handle operations
        RowlEngineHandle h = RowlEngine_Create();
        if (!h) {
            std::cerr << "RowlEngine_Create failed in diagnostic test" << std::endl;
            exit(1);
        }
        if (!RowlEngine_Init(h, 320, 180, 0)) {
            std::cerr << "RowlEngine_Init failed in diagnostic test" << std::endl;
            exit(1);
        }

        // Null string arguments to live handle must set InvalidArgument
        if (RowlEngine_ExecuteScript(h, nullptr) != 0) {
            std::cerr << "RowlEngine_ExecuteScript(h, nullptr) should return 0" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetLastResultCode(h) != 2) { // InvalidArgument = 2
            std::cerr << "Expected InvalidArgument (2) on null script string, got: " << RowlEngine_GetLastResultCode(h) << std::endl;
            exit(1);
        }

        if (RowlEngine_LoadStoryGraphFromVfs(h, nullptr) != 0) {
            std::cerr << "RowlEngine_LoadStoryGraphFromVfs(h, nullptr) should return 0" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetLastResultCode(h) != 2) { // InvalidArgument = 2
            std::cerr << "Expected InvalidArgument (2) on null VFS path, got: " << RowlEngine_GetLastResultCode(h) << std::endl;
            exit(1);
        }

        // Invalid save slot via C API
        int saveRes = RowlEngine_SaveGameSlot(h, -7);
        if (saveRes != 0) {
            std::cerr << "RowlEngine_SaveGameSlot(h, -7) should return 0" << std::endl;
            exit(1);
        }
        int32_t code = RowlEngine_GetLastResultCode(h);
        if (code != 2) { // InvalidArgument = 2
            std::cerr << "Expected code 2 (InvalidArgument), got: " << code << std::endl;
            exit(1);
        }
        const char* op = RowlEngine_GetLastResultOperation(h);
        if (!op || std::string(op) != "save_game_slot") {
            std::cerr << "Expected operation save_game_slot, got: " << (op ? op : "null") << std::endl;
            exit(1);
        }
        const char* tgt = RowlEngine_GetLastResultTarget(h);
        if (!tgt || std::string(tgt) != "-7") {
            std::cerr << "Expected target -7, got: " << (tgt ? tgt : "null") << std::endl;
            exit(1);
        }
        const char* msg = RowlEngine_GetLastResultMessage(h);
        if (!msg || std::string(msg).find("Invalid save slot") == std::string::npos) {
            std::cerr << "Expected message containing 'Invalid save slot', got: " << (msg ? msg : "null") << std::endl;
            exit(1);
        }

        // Clear diagnostic result
        RowlEngine_ClearLastResult(h);
        if (RowlEngine_GetLastResultCode(h) != 0) {
            std::cerr << "RowlEngine_ClearLastResult did not reset code to 0" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetLastResultMessage(h)) != "Success") {
            std::cerr << "RowlEngine_ClearLastResult did not reset message to Success" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(h);
        TEST_PASS("C-API Structured Diagnostic Query Functions & Null Handle Safety");
    }
}
