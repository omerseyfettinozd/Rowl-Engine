/**
 * test_d06_probe_x.cpp — D06 ham-global sızıntı kilidi (evaluateCondition).
 *
 * Bağımsız ikili: rowl_d06_probe_x (ctest -R d06_probe_x).
 * D01 konvansiyonu: yalnızca public C API + dummy driver (SDL_AUDIODRIVER=dummy,
 * SDL_VIDEODRIVER=dummy). rowl_tests gövdesine gömülmez ki kırmızı-yeşil
 * döngüsü tüm süiti koşmadan saniyeler içinde kanıtlansın.
 *
 * Sızıntı (RED'i veren gözlem): koşulun _G'ye ektiği ham global geri
 * alınmıyor (lua_sandbox.hpp'deki belgeli residual). Somut senaryo:
 *   (function() d06_stray_x = 42; return true end)()
 * koşulu true döner, ama d06_stray_x _G'de kalır; sonraki
 *   d06_stray_x == nil
 * koşulu false döner. Aynı delik fonksiyon-değeri için de geçerli
 * (d06_stray_fn). Prob temizliği iddia eder:
 *   RED  (pre-fix):  "PROB-DIRTY" + exit 1
 *   GREEN (post-fix): "PROB-CLEAN" + exit 0
 *
 * KIRMIZI-YEŞİL SÖZLEŞMESİ: yeşilde bu gözlemlerden herhangi biri değişirse
 * fix TURU sayılır (max 3); kırmızıda commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <iostream>
#include <string>

#include "rowl/c_api.h"

int main() {
    TEST_SECTION("D06 probe-x (evaluateCondition ham-global sizintisi)");

    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) {
        std::cout << "PROB-DIRTY setup: RowlEngine_Create returned null" << std::endl;
        return 1;
    }
    if (RowlEngine_Init(h, 320, 180, 0) != 1) {
        std::cout << "PROB-DIRTY setup: RowlEngine_Init(320x180) failed" << std::endl;
        return 1;
    }

    // 1) Ham sayı globali eken koşul true dönmeli (kurulumun kendisi sağlam).
    if (RowlEngine_EvaluateCondition(h, "(function() d06_stray_x = 42; return true end)()") != 1) {
        std::cout << "PROB-DIRTY setup: planting condition did not return true" << std::endl;
        return 1;
    }
    // 2) Ham fonksiyon globali eken koşul true dönmeli.
    if (RowlEngine_EvaluateCondition(h, "(function() d06_stray_fn = function() return 1 end; return true end)()") != 1) {
        std::cout << "PROB-DIRTY setup: planting function condition did not return true" << std::endl;
        return 1;
    }

    // 3) Sızıntı gözlemi: ekilen adlar koşul çıkışında nil'lenmiş olmalı.
    const int numGone = RowlEngine_EvaluateCondition(h, "d06_stray_x == nil");
    const int fnGone = RowlEngine_EvaluateCondition(h, "d06_stray_fn == nil");
    std::cout << "  d06_stray_x == nil -> " << numGone
              << " d06_stray_fn == nil -> " << fnGone << std::endl;

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    if (numGone != 1 || fnGone != 1) {
        std::cout << "PROB-DIRTY d06 ham-global sizdi (num=" << numGone
                  << " fn=" << fnGone << ")" << std::endl;
        return 1;
    }
    std::cout << "PROB-CLEAN d06 ham-global sizintisi yok" << std::endl;
    return 0;
}
