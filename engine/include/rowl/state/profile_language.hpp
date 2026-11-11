#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Rowl::State {

/// D05-1: the profile's `language` rule, header-only so the engine library,
/// the standalone player (which sees only exported C API symbols) and the
/// tests share ONE definition. The managed mirror is
/// editor/Services/PlayerProfile.cs NormalizeLanguage.
///
/// "" means "the player never chose" (the default of a new profile); the
/// project's own default locale applies then. A chosen value is a lowercase
/// locale tag: language `[a-z]{2,3}` followed by optional `-`/`_` separated
/// alphanumeric subtags (1-8 chars each), at most 16 characters in total.
/// `_` is stored as `-` ("pt_br" -> "pt-br"). Anything else is invalid and
/// normalizes to "" with `*valid` = false (callers raise the warning).
inline std::string normalizeProfileLanguage(std::string_view raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    std::size_t begin = 0;
    std::size_t end = raw.size();
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (begin < end && isSpace(raw[begin])) ++begin;
    while (end > begin && isSpace(raw[end - 1])) --end;
    std::string tag;
    tag.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '_') c = '-';
        tag.push_back(c);
    }
    if (tag.empty()) return tag;

    bool ok = tag.size() <= 16;
    std::size_t segment = 0;
    std::size_t segmentStart = 0;
    for (std::size_t i = 0; ok && i <= tag.size(); ++i) {
        if (i == tag.size() || tag[i] == '-') {
            const std::size_t length = i - segmentStart;
            if (segment == 0) ok = length >= 2 && length <= 3;
            else ok = length >= 1 && length <= 8;
            ++segment;
            segmentStart = i + 1;
        } else {
            const char c = tag[i];
            const bool alpha = c >= 'a' && c <= 'z';
            const bool digit = c >= '0' && c <= '9';
            ok = segment == 0 ? alpha : (alpha || digit);
        }
    }
    if (!ok) {
        if (valid) *valid = false;
        return {};
    }
    return tag;
}

/// What the player host should pass to SetLocale for the stored language:
/// "" when the player never chose (the project default stays) or when the
/// chosen language is already active, else the normalized tag. Whether the
/// game really supports it is SetLocale's verdict (unsupported -> the host
/// logs it and keeps the project default).
inline std::string profileLanguageToApply(std::string_view stored, std::string_view activeLocale) {
    const std::string tag = normalizeProfileLanguage(stored);
    return tag == activeLocale ? std::string{} : tag;
}

}  // namespace Rowl::State
