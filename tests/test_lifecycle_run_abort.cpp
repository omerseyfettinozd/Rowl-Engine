/**
 * test_lifecycle_run_abort.cpp — Bulgu #123 [HIGH]: run-loop istisnada
 * terminal shutdown + fail-loud abort.
 *
 * Zemin: Engine::run() trysizdi — step() icinden kacan istisna donguyu
 * yirtar, shutdown() atlanir, void invokeNoexcept yutar (sinyal yok),
 * m_isRunning true kalirdi (limbo: IsRunning=1); sonraki Run/Step yarim-kare
 * + ilerlemis saatle devam ederdi. Ayrica playtime step basinda artardi.
 *
 * S1 (RED): standalone handle'da seam kurulu ilk step firlatir.
 *   pre-fix: RowlEngine_Run sessiz doner (yutulmus), IsRunning=1 (limbo),
 *     last-result bayat init-OK, pencere acik (shutdown atlandi).
 *   post-fix: catch -> isRunning=false + StateError/op=run + terminal
 *     shutdown (pencere birakilir), kaza-karesi saate islemez.
 * S2 (RED): ayni seam RowlEngine_Step'te — Step yolu sessiz kalir (bilinen
 *   daralma: yutma ABI sinirinda) ama kaza-karesi saate isleyemez.
 *   pre-fix: playtime 0.016 ilerler. post-fix: 0.0 kalir.
 * Bilerek-boz: Engine::run'daki try/catch kalkarsa S1 limbo-imzasinda
 * (IsRunning=1) kirmiziya duser; playtime tasma alta alinirsa S2 kizarir.
 * Gorunur pencere acilamayan surucude SKIP (S6 emsali, dokumante).
 */
#include "rowl_test_harness.hpp"

namespace {

void checkRunAbort(bool condition, const char* what) {
    if (!condition) {
        rowlLockFail("lifecycle-run-abort", what);
    }
}

int32_t runAbortCode(RowlEngineHandle h) {
    return static_cast<int32_t>(RowlEngine_GetLastResultCode(h));
}

std::string runAbortOp(RowlEngineHandle h) {
    const char* op = RowlEngine_GetLastResultOperation(h);
    return op ? op : "";
}

}  // namespace

void test_lifecycle_run_abort() {
    TEST_SECTION("Lifecycle Run Abort (#123: exception aborts run fail-closed)");

    RowlEngineHandle h = RowlEngine_Create();
    checkRunAbort(h != nullptr, "S1: Create returned null");
    if (RowlEngine_InitStandalone(h, "123-RunAbort", 320, 180, 0) != 1) {
        std::cout << "  (S1 SKIP: no visible window on this driver; "
                     "abort covered by review + seam mechanics)"
                  << std::endl;
        RowlEngine_Destroy(h);
        TEST_PASS("S1+S2 run abort skipped (headless, documented)");
        return;
    }
    Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(h);
    checkRunAbort(engine != nullptr, "S1: test bridge returned null");

// ── S2: kaza-karesi saate isleyemez (Step yolu) ──
    engine->setPlayState(true);
    engine->testArmStepThrow(1);  // siradaki step firlatir
    RowlEngine_Step(h, 0.016f);   // yutulur (ABI siniri); saat gozlenir
    engine->testArmStepThrow(0);
    checkRunAbort(engine->testPlaytimeSeconds() == 0.0,
                  "S2: aborted step must not advance playtime "
                  "(advanced clock on half-frame, #123)");
    TEST_PASS("S2 aborted step leaves playtime at zero");

// ── S1: Run limbo yerine fail-closed abort ──
    engine->testArmStepThrow(1);  // run-loop'un ilk step'i firlatir
    RowlEngine_Run(h);            // pre-fix: sessiz doner, IsRunning=1
    engine->testArmStepThrow(0);
    checkRunAbort(RowlEngine_IsRunning(h) == 0,
                  "S1: run must not stay running after injected step throw "
                  "(limbo IsRunning=1, #123)");
    checkRunAbort(runAbortCode(h) == static_cast<int32_t>(ROWL_RESULT_STATE_ERROR),
                  "S1: aborted run must stamp StateError (fail-loud, #123)");
    checkRunAbort(runAbortOp(h) == "run",
                  "S1: aborted run must stamp op=run");
    checkRunAbort(engine->getWindow() == nullptr,
                  "S1: terminal shutdown must release the window "
                  "(skipped shutdown, #123)");
    checkRunAbort(engine->testPlaytimeSeconds() == 0.0,
                  "S1: aborted run must not advance playtime");
    RowlEngine_Shutdown(h);  // terminal shutdown sonrasi idempotent
    RowlEngine_Destroy(h);
    TEST_PASS("S1 run-loop exception -> fail-closed abort (terminal shutdown)");
}
