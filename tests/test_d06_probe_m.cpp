/**
 * test_d06_probe_m.cpp — D06 kontrol kolu (D4 guard yeşili).
 *
 * Bağımsız ikili: rowl_d06_probe_m (ctest -R d06_probe_m).
 * D01 konvansiyonu: yalnızca public C API + dummy driver. D06 düzeltmesi
 * D4'ün ConditionPurityGuard davranışını bozmamalı; bu kol fix-öncesi YEŞİL,
 * fix-sonrası da YEŞİL kalmalı. Kırmızıya dönüş = D06 regresyonu.
 *
 * Kontrol edilen D4 sözleşmesi:
 *   (a) koşul içinden rowl.var_set ile yazılan ad koşul çıkışında geri
 *       alınır (harita + global);
 *   (b) salt-okunur koşul sessiz kalır ve mevcut değişkeni bozmaz.
 */
#include "rowl_test_harness.hpp"

#include <iostream>
#include <string>

#include "rowl/c_api.h"

namespace {

void mFail(const std::string& message) {
    rowlLockFail("d06_probe_m", message);
}

std::string getVar(RowlEngineHandle h, const char* key) {
    const char* v = RowlEngine_GetVariable(h, key);
    return v ? std::string(v) : std::string("<null>");
}

}  // namespace

int main() {
    TEST_SECTION("D06 probe-m (kontrol: D4 condition purity guard yesili)");

    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) mFail("RowlEngine_Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        mFail("RowlEngine_Init(320x180) must succeed under dummy drivers");

    // (a) rowl.var_set yan-etkisi geri alınmalı.
    if (RowlEngine_EvaluateCondition(h, "(function() rowl.var_set('d06_m_ctl','zzz'); return true end)()") != 1)
        mFail("parcel condition with rowl.var_set must return true");
    if (!getVar(h, "d06_m_ctl").empty())
        mFail("D4 guard regressed: var_set side-effect survived (map)");
    if (RowlEngine_EvaluateCondition(h, "d06_m_ctl == nil") != 1)
        mFail("D4 guard regressed: var_set side-effect survived (global)");

    // (b) salt-okunur koşul mevcut değişkeni bozmamalı. Not: SetVariable
    // sayısal metni sayı olarak commit'ler (parseSandboxNumber), o yüzden
    // karşılaştırma sayı-sayı yapılır (sayı == 'metin' Lua'da false'tur).
    RowlEngine_SetVariable(h, "d06_m_ro", "7");
    if (RowlEngine_EvaluateCondition(h, "d06_m_ro == 7") != 1)
        mFail("read-only condition on existing variable must be true");
    if (getVar(h, "d06_m_ro") != "7")
        mFail("read-only condition mutated the variable map");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("d06 kontrol kolu yesil (D4 guard paritesi)");
    return 0;
}
