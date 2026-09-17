/**
 * locale_independent_parse.hpp
 *
 * Strict ASCII floating-point parsing that never consults the global C/C++
 * locale: only '.' is a decimal separator, so a comma-decimal locale (e.g.
 * tr_TR) can never change what a value means.
 *
 * Why this exists: std::from_chars(double) is stubbed out (deleted) in the
 * libc++ shipped with current Apple Clang, so the previous from_chars call
 * sites broke the macOS compile gate while GCC/libstdc++ and MSVC stayed
 * green. The conversion below is validated by a strict manual grammar scan
 * first, then converted with strtod_l/_strtod_l under an explicit "C"
 * numeric locale — portable across libstdc++, libc++ and MSVC.
 *
 * Grammar (validated before conversion, full consumption required):
 *   [+-]? digits [. digits]? ([eE] [+-]? digits)?
 * Whitespace, "nan"/"inf" spellings, hex floats and trailing garbage are
 * rejected. At least one mantissa digit is required.
 */

#pragma once

#include <string>

#if defined(_WIN32)
#include <locale.h>
#else
#include <locale.h>
#include <stdlib.h>
#endif

namespace Rowl::Util {

inline bool parseAsciiDouble(const char* first, const char* last, double& out) {
    if (first == nullptr || first >= last) return false;

    // Strict grammar scan: every byte must belong to the number.
    const char* p = first;
    if (*p == '+' || *p == '-') ++p;
    bool digits = false;
    while (p != last && *p >= '0' && *p <= '9') {
        ++p;
        digits = true;
    }
    if (p != last && *p == '.') {
        ++p;
        while (p != last && *p >= '0' && *p <= '9') {
            ++p;
            digits = true;
        }
    }
    if (!digits) return false;
    if (p != last && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p != last && (*p == '+' || *p == '-')) ++p;
        bool expDigits = false;
        while (p != last && *p >= '0' && *p <= '9') {
            ++p;
            expDigits = true;
        }
        if (!expDigits) return false;
    }
    if (p != last) return false;

    // Grammar is clean, so the "C" locale conversion below consumes fully.
    // strtod_l needs NUL termination; cold paths only (script vars, markup
    // attributes), the one short allocation is acceptable.
    const std::string buffer(first, last);
#if defined(_WIN32)
    _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
    if (cLocale == nullptr) return false;
    char* end = nullptr;
    const double value = _strtod_l(buffer.c_str(), &end, cLocale);
    _free_locale(cLocale);
#else
    locale_t cLocale = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(0));
    if (cLocale == static_cast<locale_t>(0)) return false;
    char* end = nullptr;
    const double value = strtod_l(buffer.c_str(), &end, cLocale);
    freelocale(cLocale);
#endif
    if (end != buffer.c_str() + buffer.size()) return false;
    out = value;
    return true;
}

} // namespace Rowl::Util
