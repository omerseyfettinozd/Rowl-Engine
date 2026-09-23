/**
 * test_w8a_p7_pixelbuffer_race.cpp — W8-a bulgu (7) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p7 (ctest -R w8a_p7). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: RowlEngine_GetPixelBufferEx checked-null yolunda (:69-72) out-param
 * sifirlamadan nullptr donuyor (olu dal :63-68 sifirlar). Bu yol yalniz
 * cross-thread destroy yarisinda tutar (isLiveHandle==Mine gecti, araya
 * Destroy girdi, toEngineChecked null): isLiveHandle/toEngineChecked ayni
 * thread'de arka-arkaya ayni kilidi alir, tek-thread'de ayrilamaz.
 *
 * Prob: sahip-thread (main) GetPixelBufferEx'i sentinel'li out-param'larla
 * cagirirken yikici-thread'ler token'i Destroy/Create ile dondurur. Yaris
 * penceresi tutarsa pre-fix kodu sentinel'leri aynen birakir (nullptr +
 * kirli out) -> RED. Post-fix sifirlar -> GREEN.
 *
 * KIRMIZI (pre-fix): en az bir kirli-null gozlemi -> exit 1.
 * YESIL (post-fix): tum null donuslerde out-param sifir.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

void p7Fail(const std::string& message) {
    rowlLockFail("w8a-p7-pixelbuffer", message);
}

constexpr uint32_t kSentinel = 0xDEADBEEFu;
constexpr int kIterations = 400000;
constexpr int kDestroyers = 3;

std::atomic<RowlEngineHandle> g_shared{nullptr};
std::atomic<bool> g_stop{false};

void destroyerLoop() {
    while (!g_stop.load(std::memory_order_relaxed)) {
        RowlEngineHandle fresh = RowlEngine_Create();
        if (fresh == nullptr) continue;  // baski altinda kacir, probu oldurme
        RowlEngineHandle old = g_shared.exchange(fresh, std::memory_order_acq_rel);
        if (old != nullptr) RowlEngine_Destroy(old);
    }
}

}  // namespace

int main() {
    TEST_SECTION("W8-a P7: GetPixelBufferEx checked-null out-param sifirlama");

    // Olu dal karakterizasyonu (pre/post ayni: nullptr + sifir out).
    {
        uint32_t w = kSentinel, h = kSentinel, p = kSentinel;
        const uint8_t* ret =
            RowlEngine_GetPixelBufferEx(nullptr, &w, &h, &p);
        if (ret != nullptr || w != 0 || h != 0 || p != 0) {
            p7Fail("olu dal nullptr+sifir kontratini tutmuyor");
        }
    }
    TEST_PASS("Olu dal nullptr + sifir out");

    // Yaris alani: paylasilan token ile basla.
    RowlEngineHandle seed = RowlEngine_Create();
    if (seed == nullptr) p7Fail("RowlEngine_Create null dondu");
    g_shared.store(seed, std::memory_order_release);

    std::vector<std::thread> destroyers;
    for (int i = 0; i < kDestroyers; ++i) destroyers.emplace_back(destroyerLoop);

    int dirtyNulls = 0;
    int totalNulls = 0;
    uint32_t firstW = 0, firstH = 0, firstP = 0;
    for (int i = 0; i < kIterations; ++i) {
        RowlEngineHandle t = g_shared.load(std::memory_order_acquire);
        if (t == nullptr) continue;
        uint32_t w = kSentinel, h = kSentinel, p = kSentinel;
        const uint8_t* ret = RowlEngine_GetPixelBufferEx(t, &w, &h, &p);
        if (ret == nullptr) {
            ++totalNulls;
            if (w == kSentinel || h == kSentinel || p == kSentinel) {
                if (dirtyNulls == 0) {
                    firstW = w;
                    firstH = h;
                    firstP = p;
                }
                ++dirtyNulls;
            }
        }
    }

    g_stop.store(true, std::memory_order_relaxed);
    for (auto& d : destroyers) d.join();
    RowlEngineHandle leftover = g_shared.exchange(nullptr);
    if (leftover != nullptr) RowlEngine_Destroy(leftover);

    std::cout << "  null donusler: " << totalNulls << ", kirli-null: " << dirtyNulls
              << std::endl;
    if (dirtyNulls > 0) {
        p7Fail("checked-null yolu out-param sifirlamadi (kirli-null=" +
               std::to_string(dirtyNulls) + " ilk=w:" + std::to_string(firstW) +
               " h:" + std::to_string(firstH) + " p:" + std::to_string(firstP) + ")");
    }
    TEST_PASS("Tum null donuslerde out-param sifir");

    std::cout << "W8A-P7 GREEN: GetPixelBufferEx checked-null sifirlama" << std::endl;
    TEST_PASS("W8-a P7 yesil");
    return 0;
}
