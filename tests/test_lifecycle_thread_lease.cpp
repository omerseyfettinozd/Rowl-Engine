/**
 * test_lifecycle_thread_lease.cpp — D3: thread/lease kilitleri (B1d).
 *
 * Bulgular #102 #106 #115 #128 #133 #134 #138 (+ D6'dan emilen pin/lease/
 * dispatcher-çakışması #150 #151 #153 #155 #156 #157 ve #153'ün MEDIUM ikizi
 * #159 — B7 ikiz-birleştirme emsali):
 *  - canlı handle'a yabancı-thread Shutdown/Destroy sessiz no-op'tu ve
 *    INVALID_HANDLE'a gömülüyordu (#102): artık WRONG_THREAD (14) damgalar,
 *    motor dokunulmaz.
 *  - aux-map guard'ları yabancı-thread'i ölü sanıp entry'yi siliyordu
 *    (#150/#157): artık 14 döner, entry + lastError sağ kalır.
 *  - owner-thread ölünce handle kalıcı kilitleniyordu (#151): reclaim
 *    sahipliği + dispatch-pin'i devreder, pencere tablosu korunur.
 *  - VIDEO lease ilk-alanın thread'ine aittir; ikinci thread'in acquire'ı
 *    SDL'e dokunmadan reddedilir (#133).
 *  - dispatcher pin'i sentetik-ID'lerle test edilir: uygunluk, steal,
 *    tablo-boşalınca pin-temizliği (#115/#128/#153).
 *  - visibility-dışı Step dispatch-gate'i: owner ≠ pin iken Step 14 damgalar,
 *    ilerlemez (#155). Görünür pencere açılamazsa belge+skip (headless).
 *  - yabancı-thread çağrı tufanı + uçuş-sırasında Destroy: crash yok,
 *    final kodlar ölü/handle-ayrımında (#106/#134/#138).
 * Bilerek-boz: yabancı-Shutdown damgası kaldırılırsa S1; lease-affinity
 * kontrolü kaldırılırsa S4; Reclaim hep-INVALID_HANDLE dönerse S3; Step
 * gate'i kaldırılırsa S6 (görünür-pencereli koşuda) kırmızıya döner.
 * Fix-tur: Utf8 Foreign kolu kalkarsa S8a (R5); Init Foreign-damgası
 * kalkarsa S8b (R6) kırmızıya döner.
 */
#include "rowl_test_harness.hpp"

namespace {

void checkLease(bool condition, const char* what) {
    if (!condition) {
        rowlLockFail("lifecycle-thread-lease", what);
    }
}

void checkLeaseCode(const char* name, RowlEngineHandle handle, int32_t want) {
    const int32_t got = RowlEngine_GetLastResultCode(handle);
    if (got != want) {
        rowlLockFail("lifecycle-thread-lease",
                     std::string(name) + ": expected last-result " +
                         std::to_string(want) + ", got " + std::to_string(got));
    }
}

std::string leaseLastOp(RowlEngineHandle handle) {
    return std::string(RowlEngine_GetLastResultOperation(handle));
}

std::string leaseCallerText(RowlEngineHandle handle,
                            RowlEngine_ResultCode (*fn)(RowlEngineHandle, char*,
                                                        uint32_t, uint32_t*)) {
    char buf[1024];
    uint32_t required = 0;
    if (fn(handle, buf, sizeof(buf), &required) != ROWL_RESULT_OK) return "";
    return std::string(buf);
}

}  // namespace

void test_lifecycle_thread_lease() {
    TEST_SECTION("Lifecycle Thread/Lease (D3: B1d + D6 pin/lease)");

// ── S1: yabancı Shutdown/Destroy WRONG_THREAD damgalar, motor sağ kalır ──
    RowlEngineHandle h = RowlEngine_Create();
    checkLease(h != nullptr, "S1: Create returned null");
    checkLease(RowlEngine_Init(h, 320, 180, 0) == 1, "S1: Init failed");

    int32_t foreignShutdownCode = -1;
    std::string foreignShutdownOp;
    std::thread foreign([&] {
        RowlEngine_Shutdown(h);  // yabancı: damga, süpürme yok
        foreignShutdownCode = RowlEngine_GetLastResultCode(h);
        foreignShutdownOp = leaseLastOp(h);
        RowlEngine_Destroy(h);  // yabancı: damga, erase yok
    });
    foreign.join();
    checkLease(foreignShutdownCode == static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD),
               "S1: foreign Shutdown must stamp WRONG_THREAD, not INVALID_HANDLE");
    checkLease(foreignShutdownOp == "shutdown",
               "S1: foreign Shutdown must stamp op=shutdown");
    checkLease(RowlEngine_IsRunning(h) == 1,
               "S1: engine must stay running after foreign Shutdown/Destroy");
    // Damga paylaşımlı context'tedir: owner da 14 görür (son damga "destroy").
    checkLeaseCode("S1 owner sees shared stamp", h,
                   static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD));
    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);
    RowlEngine_Destroy(h);  // ölü-handle: sessiz no-op
    TEST_PASS("S1 wrong-thread stamp, engine untouched");

// ── S2: aux guard'lar yabancıda silmez — kod ayrımı + state sağkalımı ──
    RowlEngineHandle c = RowlEngine_Create();
    checkLease(c != nullptr, "S2: Create returned null");
    checkLease(RowlEngine_Init(c, 320, 180, 0) == 1, "S2: Init failed");
    // Owner entry'yi tohumlar: bozuk preset → PARSE_ERROR + lastError.
    checkLease(RowlEngine_RegisterCharacterPreset(c, "p1", "{oops") ==
                   ROWL_RESULT_PARSE_ERROR,
               "S2: malformed preset must be a parse error");
    checkLease(RowlEngine_LoadChapterIndexJson(c, "{oops") != ROWL_RESULT_OK,
               "S2: malformed chapter index must fail (seeds prefetch entry)");

    int32_t foreignListCode = -1;
    int foreignPumpRet = -1;
    std::thread auxForeign([&] {
        char buf[256];
        uint32_t required = 0;
        foreignListCode = static_cast<int32_t>(
            RowlEngine_GetCharacterPresetListJson(c, buf, sizeof(buf), &required));
        foreignPumpRet = RowlEngine_PumpPrefetch(c, 1.0f);  // int-taşıyıcı
    });
    auxForeign.join();
    // Ölü-ayrımı: 14 (canlı-yabancı), asla 1 (INVALID_HANDLE) değil.
    checkLease(foreignListCode == static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD),
               "S2: foreign preset-list must be WRONG_THREAD, entry kept");
    checkLease(foreignPumpRet == 0,
               "S2: foreign pump must fail closed with 0");
    // Yabancı pump damgası owner'a görünür (paylaşımlı context).
    checkLeaseCode("S2 owner sees foreign pump stamp", c,
                   static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD));
    // Silme-kanıtı: entry + lastError yabancı çağrıyı atlattı.
    const std::string charErr = leaseCallerText(c, RowlEngine_GetLastCharacterErrorUtf8);
    checkLease(charErr.find("expression JSON parse failed") != std::string::npos,
               "S2: foreign call must not erase character entry/lastError");
    char progressBuf[256];
    uint32_t progressRequired = 0;
    checkLease(RowlEngine_GetPrefetchProgressJson(c, progressBuf,
                                                 sizeof(progressBuf),
                                                 &progressRequired) == ROWL_RESULT_OK,
               "S2: foreign call must not erase prefetch entry");
    RowlEngine_Shutdown(c);
    RowlEngine_Destroy(c);
    TEST_PASS("S2 aux no-erase, code distinction, stamp sharing");

// ── S3: owner-ölümü → reclaim → temiz kapanış ──
    RowlEngineHandle doomed = nullptr;
    std::thread ownerThread([&] {
        doomed = RowlEngine_Create();
        if (doomed == nullptr) return;
        if (RowlEngine_Init(doomed, 320, 180, 0) != 1) {
            RowlEngine_Destroy(doomed);
            doomed = nullptr;
        }
        // Shutdown YOK: thread ölür, sahiplik öksüz kalır.
    });
    ownerThread.join();
    checkLease(doomed != nullptr, "S3: owner thread failed to init");
    checkLease(RowlEngine_IsRunning(doomed) == 0,
               "S3: orphaned handle must read not-running from another thread");
    checkLease(RowlEngine_ReclaimHandle(doomed) == ROWL_RESULT_OK,
               "S3: reclaim of orphaned handle must succeed");
    checkLease(RowlEngine_IsRunning(doomed) == 1,
               "S3: reclaimed handle must be live on this thread");
    checkLeaseCode("S3 reclaim stamps success", doomed,
                   static_cast<int32_t>(ROWL_RESULT_OK));
    RowlEngine_Shutdown(doomed);
    RowlEngine_Destroy(doomed);
    checkLease(RowlEngine_ReclaimHandle(doomed) == ROWL_RESULT_INVALID_HANDLE,
               "S3: reclaim of dead handle must stay INVALID_HANDLE");
    RowlEngine_Destroy(doomed);  // sessiz no-op
    TEST_PASS("S3 reclaim-after-owner-death, dead stays dead");

// ── S4: VIDEO lease ilk-alanındır; ikinci thread SDL'e dokunamaz ──
    RowlEngineHandle h1 = RowlEngine_Create();
    checkLease(h1 != nullptr, "S4: Create returned null");
    checkLease(RowlEngine_Init(h1, 320, 180, 0) == 1, "S4: main Init failed");

    int secondInitRet = -1;
    int32_t secondInitCode = -1;
    std::thread leaseRival([&] {
        RowlEngineHandle h2 = RowlEngine_Create();
        if (h2 == nullptr) return;
        secondInitRet = RowlEngine_Init(h2, 320, 180, 0);
        secondInitCode = RowlEngine_GetLastResultCode(h2);
        RowlEngine_Destroy(h2);  // kendi handle'ı: izinli, sessiz temizlik
    });
    leaseRival.join();
    checkLease(secondInitRet == 0,
               "S4: rival-thread Init must be denied while lease is held");
    checkLease(secondInitCode == static_cast<int32_t>(ROWL_RESULT_STATE_ERROR),
               "S4: denied Init must stamp StateError (fail-closed, audible)");
    RowlEngine_Shutdown(h1);
    RowlEngine_Destroy(h1);
    // Lease bırakıldı: aynı thread'den yeniden Init çalışır (owner-reset).
    RowlEngineHandle h3 = RowlEngine_Create();
    checkLease(h3 != nullptr, "S4: re-Create returned null");
    checkLease(RowlEngine_Init(h3, 320, 180, 0) == 1,
               "S4: Init after lease release must succeed");
    RowlEngine_Shutdown(h3);
    RowlEngine_Destroy(h3);
    TEST_PASS("S4 lease affinity, closed denial, release-reset");

// ── S5: dispatcher pin — uygunluk, steal, tablo-boşalınca temizlik ──
    checkLease(Rowl::Platform::SdlEventDispatcher::registerWindow(7001),
               "S5: main must register first synthetic window");
    bool rivalEligible = true;
    bool rivalIsDispatch = true;
    bool rivalRegistered = true;
    bool rivalStole = false;
    std::thread pinRival([&] {
        rivalIsDispatch = Rowl::Platform::SdlEventDispatcher::isDispatchThread();
        rivalEligible = Rowl::Platform::SdlEventDispatcher::isEligibleForRegister();
        rivalRegistered =
            Rowl::Platform::SdlEventDispatcher::registerWindow(7002);
        Rowl::Platform::SdlEventDispatcher::stealDispatchThread();
        rivalStole = Rowl::Platform::SdlEventDispatcher::isDispatchThread();
    });
    pinRival.join();
    checkLease(!rivalIsDispatch, "S5: rival must not own the pin");
    checkLease(!rivalEligible, "S5: rival must be ineligible while pinned");
    checkLease(!rivalRegistered, "S5: rival register must fail closed");
    checkLease(rivalStole, "S5: steal must transfer the pin to the rival");
    checkLease(!Rowl::Platform::SdlEventDispatcher::isDispatchThread(),
               "S5: main must observe the stolen pin");
    Rowl::Platform::SdlEventDispatcher::unregisterWindow(7001);
    // Tablo boşaldı → pin temizlenir (sonraki testlere sızamaz).
    checkLease(Rowl::Platform::SdlEventDispatcher::isEligibleForRegister(),
               "S5: empty table must clear the pin");
    checkLease(!Rowl::Platform::SdlEventDispatcher::isDispatchThread(),
               "S5: cleared pin must report no dispatch owner");
    TEST_PASS("S5 dispatcher eligibility, steal, empty-table cleanup");

// ── S6: görünür-Step dispatch-gate (görünür pencere yoksa belge+skip) ──
    RowlEngineHandle v = RowlEngine_Create();
    checkLease(v != nullptr, "S6: Create returned null");
    if (RowlEngine_InitStandalone(v, "D3-StepGate", 320, 180, 0) != 1) {
        std::cout << "  (S6 SKIP: no visible window on this driver; "
                     "gate covered by review + S5 pin mechanics)"
                  << std::endl;
        RowlEngine_Destroy(v);
        TEST_PASS("S6 step gate skipped (headless, documented)");
    } else {
        std::thread pinThief([&] {
            Rowl::Platform::SdlEventDispatcher::stealDispatchThread();
        });
        pinThief.join();
        checkLease(!Rowl::Platform::SdlEventDispatcher::isDispatchThread(),
                   "S6: pin must be stolen for the gate to fire");
        RowlEngine_Step(v, 0.016f);  // owner ama dispatch-değil → gate
        checkLeaseCode("S6 gated step stamps", v,
                       static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD));
        checkLease(leaseLastOp(v) == "step",
                   "S6: gated step must stamp op=step");
        Rowl::Platform::SdlEventDispatcher::stealDispatchThread();  // pin geri
        RowlEngine_Shutdown(v);
        RowlEngine_Destroy(v);  // unregister tabloyu boşaltır → pin temizlenir
        checkLease(Rowl::Platform::SdlEventDispatcher::isEligibleForRegister(),
                   "S6: pin must be clean after visible teardown");
        TEST_PASS("S6 visible step gate fires, no advance");
    }

// ── S7: tufan + uçuş-sırasında Destroy — crash yok, final kodlar ölü ──
    RowlEngineHandle t = RowlEngine_Create();
    checkLease(t != nullptr, "S7: Create returned null");
    checkLease(RowlEngine_Init(t, 320, 180, 0) == 1, "S7: Init failed");
    std::atomic<int> started{0};
    std::atomic<bool> stop{false};
    auto hammer = [&] {
        started.fetch_add(1);
        while (!stop.load()) {
            (void)RowlEngine_IsRunning(t);
            (void)RowlEngine_GetLastResultCode(t);
            (void)RowlEngine_GetLastResultOperation(t);
            RowlEngine_Step(t, 0.016f);  // yabancı: yalnızca damga
        }
    };
    std::thread pounders[4] = {std::thread(hammer), std::thread(hammer),
                               std::thread(hammer), std::thread(hammer)};
    while (started.load() < 4) std::this_thread::yield();
    RowlEngine_Destroy(t);  // tufan sürerken: map-erase + shared-sahiplik
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true);
    for (auto& p : pounders) p.join();
    checkLease(RowlEngine_IsRunning(t) == 0,
               "S7: destroyed handle must read not-running");
    checkLeaseCode("S7 destroyed handle reads dead", t,
                   static_cast<int32_t>(ROWL_RESULT_INVALID_HANDLE));
    RowlEngine_Destroy(t);  // sessiz no-op
    TEST_PASS("S7 hammer + mid-flight destroy, no crash, dead reads dead");

// ── S8: review-tur bulguları — Utf8 yabancı-okuması + yabancı Init damgası ──
    // S8a: Utf8 okuyucular plain okuyucularla aynı yabancı-okumayı sunmalı.
    RowlEngineHandle w = RowlEngine_Create();
    checkLease(w != nullptr, "S8: Create returned null");
    checkLease(RowlEngine_Init(w, 320, 180, 0) == 1, "S8: Init failed");
    std::thread stampForeign([&] { RowlEngine_Shutdown(w); });  // damga: shutdown/14
    stampForeign.join();
    const std::string ownerOp = leaseLastOp(w);
    checkLease(ownerOp == "shutdown", "S8: shared stamp op must be shutdown");
    int32_t utf8OpRc = -1, utf8MsgRc = -1, utf8TgtRc = -1;
    std::string utf8Op, utf8Msg, utf8Tgt;
    std::thread utf8Foreign([&] {
        char buf[1024];
        uint32_t required = 0;
        utf8OpRc = static_cast<int32_t>(
            RowlEngine_GetLastResultOperationUtf8(w, buf, sizeof(buf), &required));
        if (utf8OpRc == static_cast<int32_t>(ROWL_RESULT_OK)) utf8Op = buf;
        utf8MsgRc = static_cast<int32_t>(
            RowlEngine_GetLastResultMessageUtf8(w, buf, sizeof(buf), &required));
        if (utf8MsgRc == static_cast<int32_t>(ROWL_RESULT_OK)) utf8Msg = buf;
        utf8TgtRc = static_cast<int32_t>(
            RowlEngine_GetLastResultTargetUtf8(w, buf, sizeof(buf), &required));
        if (utf8TgtRc == static_cast<int32_t>(ROWL_RESULT_OK)) utf8Tgt = buf;
    });
    utf8Foreign.join();
    // Bilerek-boz R5: Utf8 Foreign kolu kalkarsa bu üç okuma INVALID_HANDLE
    // (1) döner ve utf8Op boş kalır — aşağıdaki üç check kırmızıya döner.
    checkLease(utf8OpRc == static_cast<int32_t>(ROWL_RESULT_OK),
               "S8: foreign Utf8 op read must succeed (R5 guard)");
    checkLease(utf8Op == ownerOp,
               "S8: foreign Utf8 op must carry the shared stamp");
    checkLease(utf8MsgRc == static_cast<int32_t>(ROWL_RESULT_OK) &&
                   utf8TgtRc == static_cast<int32_t>(ROWL_RESULT_OK),
               "S8: foreign Utf8 message/target reads must succeed (R5 guard)");
    RowlEngine_Shutdown(w);
    RowlEngine_Destroy(w);

    // S8b: canlı handle'a yabancı Init reddedilir + "init" damgalar.
    RowlEngineHandle u = RowlEngine_Create();
    checkLease(u != nullptr, "S8: Create returned null");
    checkLease(RowlEngine_Init(u, 320, 180, 0) == 1, "S8: Init failed");
    int foreignInitRet = -1;
    std::thread initForeign([&] { foreignInitRet = RowlEngine_Init(u, 320, 180, 0); });
    initForeign.join();
    checkLease(foreignInitRet == 0, "S8: foreign Init must be refused");
    // Bilerek-boz R6: Init Foreign-damgası kalkarsa kod 14 değil 0 kalır.
    checkLeaseCode("S8 foreign Init stamps", u,
                   static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD));
    checkLease(leaseLastOp(u) == "init",
               "S8: foreign Init must stamp op=init");
    RowlEngine_Shutdown(u);
    RowlEngine_Destroy(u);
    TEST_PASS("S8 utf8 foreign parity + foreign init stamp");
}
