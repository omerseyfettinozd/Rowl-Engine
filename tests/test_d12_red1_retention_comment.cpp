/**
 * test_d12_red1_retention_comment.cpp — D12 yorum-celiski RED kilidi (RED-1).
 *
 * Bagimsiz ikili: rowl_d12_red1_retention_comment (ctest -R d12_red1).
 * D01 konvansiyonu: paylasilan RowlEngineCore'a baglanir; rowl_tests
 * govdesine gomulmez ki kirmizi-yesil dongusu tum suiti kosmadan
 * saniyeler icinde kanitlansin.
 *
 * Statik prob: engine/include/rowl/c_api.h metnini okur.
 *   KIRMIZI (pre-fix): "retained until process exit" cumlesi mevcutsa
 *   exit 1 — c_api.h:29-31, c_api_internal.hpp:36-41 ile celisir
 *   (gercek: free-list recycle + generation bump + ABA defense).
 *   YESIL (post-fix): recycle + generation wording mevcutsa exit 0.
 *
 * KIRMIZI-YESIL SOZLESMESI: celiski exit 1 / hizali exit 0.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

void red1Fail(const std::string& message) {
    rowlLockFail("d12-red1-retention-comment", message);
}

std::string readHeader() {
    // ctest WORKING_DIRECTORY CMAKE_SOURCE_DIR'dir; yedek olarak derleme
    // anindaki kaynak kokune gore cozulen mutlak yol denenir.
    const char* candidates[] = {
        "engine/include/rowl/c_api.h",
#ifdef ROWL_D12_SOURCE_DIR
        ROWL_D12_SOURCE_DIR "/engine/include/rowl/c_api.h",
#endif
        nullptr,
    };
    for (const char** p = candidates; *p != nullptr; ++p) {
        std::ifstream in(*p);
        if (!in) continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        if (!text.empty()) return text;
    }
    red1Fail("c_api.h okunamadi (aday yollar tukendi)");
    return {};
}

}  // namespace

int main() {
    TEST_SECTION("D12 RED-1: retention yorumu celiski probu");

    const std::string text = readHeader();

    // Pre-fix celiski imzasi: sonsuza-dek tutma iddiasi.
    if (text.find("retained until process exit") != std::string::npos) {
        std::cout << "D12-RED1 RED: c_api.h 'retained until process exit' "
                     "cumlesi internal.hpp (free-list recycle + generation) "
                     "ile celisiyor"
                  << std::endl;
        return 1;
    }

    // Post-fix hizalama imzasi: recycle + generation wording.
    const bool hasRecycle =
        text.find("recycl") != std::string::npos;
    const bool hasGeneration =
        text.find("generation") != std::string::npos;
    if (!hasRecycle || !hasGeneration) {
        std::cout << "D12-RED1 RED: hizli cumle eksik (recycle="
                  << (hasRecycle ? "var" : "yok")
                  << ", generation=" << (hasGeneration ? "var" : "yok") << ")"
                  << std::endl;
        return 1;
    }

    std::cout << "D12-RED1 GREEN: c_api.h recycle+generation wording hizali"
              << std::endl;
    TEST_PASS("D12 RED-1 yesil");
    return 0;
}
