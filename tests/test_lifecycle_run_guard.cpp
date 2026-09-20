/**
 * test_lifecycle_run_guard.cpp — Bulgu #14 (HIGH): offscreen Run fail-closed.
 *
 * Offscreen handle'da RowlEngine_Run kesilemeyen sonsuz donguye girerdi:
 * initializeOffscreen m_eventWindowId setlemez (window.cpp), pollEvents
 * erken doner, SDL_EVENT_QUIT kayitsiz oldugu icin hicbir offscreen
 * kuyruga yonlendirilmez (sdl_event_dispatcher.cpp routeEvent yalnizca
 * kayitli pencerelere dagitir), lifecycleState() hep Active doner, yabanci
 * Shutdown/Destroy yalnizca WRONG_THREAD damgalar. Owner thread Run'da
 * bloklu kaldigindan durdurma yolu yoktu.
 *
 * S1 (RED prob): owner thread'de Create+Init+Run yapan runner'a timeout'lu
 * join — pre-fix hicbir zaman donmez (10 sn timeout'ta kirmizi). Post-fix:
 * Run aninda doner, StateError(11)/op=run damgalar, motor calisir ve Step
 * ile surulebilir kalir, temiz Shutdown+Destroy olur.
 * Bilerek-boz: Engine::run'daki isOffscreen guard'i kalkarsa S1 timeout'a
 * duser ve kirmiziya doner.
 */
#include "rowl_test_harness.hpp"

#include <future>

namespace {

void checkRunGuard(bool condition, const char* what) {
    if (!condition) {
        rowlLockFail("lifecycle-run-guard", what);
    }
}

}  // namespace

void test_lifecycle_run_guard() {
    TEST_SECTION("Lifecycle Run Guard (#14: offscreen Run fail-closed)");

// ── S1: offscreen Run owner thread'de takilmadan donmeli ──
    std::promise<std::string> verdict;
    std::future<std::string> done = verdict.get_future();
    std::thread runner([&] {
        RowlEngineHandle h = RowlEngine_Create();
        if (h == nullptr) {
            verdict.set_value("create-null");
            return;
        }
        // Runner thread Init'i de yapar: hem owner hem video-lease sahibi
        // olur (D3 #133: baska thread lease tutarken Init reddedilir).
        if (RowlEngine_Init(h, 320, 180, 0) != 1) {
            verdict.set_value("init-failed");
            RowlEngine_Destroy(h);
            return;
        }
        // RED nokta: pre-fix bu cagri offscreen'da asla donmezdi.
        RowlEngine_Run(h);
        const int32_t code =
            static_cast<int32_t>(RowlEngine_GetLastResultCode(h));
        const std::string op =
            RowlEngine_GetLastResultOperation(h) ? RowlEngine_GetLastResultOperation(h) : "";
        std::string outcome = "ok";
        if (code != static_cast<int32_t>(ROWL_RESULT_STATE_ERROR)) {
            outcome = "code:" + std::to_string(code);
        } else if (op != "run") {
            outcome = "op:" + op;
        } else if (RowlEngine_IsRunning(h) != 1) {
            // Fail-closed donus shutdown cagirmaz: handle sag kalmali.
            outcome = "not-running";
        } else {
            RowlEngine_Step(h, 0.016f);  // hala surulebilir olmali
            if (RowlEngine_IsRunning(h) != 1) outcome = "step-killed";
        }
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        verdict.set_value(outcome);
    });
    // std::thread'de timeout'lu join yok: future ile bekle, timeout'ta
    // detach edip kirmiziya dus (suite'i kilitleme).
    if (done.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        runner.detach();
        rowlLockFail("lifecycle-run-guard",
                     "S1: offscreen Run did not return within 10s "
                     "(unstoppable loop, #14)");
    }
    runner.join();
    const std::string outcome = done.get();
    checkRunGuard(outcome == "ok",
                 ("S1: offscreen Run must return fail-closed "
                  "(StateError/op=run, engine live+steppable), got: " +
                  outcome)
                     .c_str());
    TEST_PASS("S1 offscreen Run fail-closed, engine live and steppable");
}
