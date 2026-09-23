/**
 * c_api_stamp_helper.hpp — W8-f1 (3): InvalidArgument damga klonlarinin
 * ortak helper'i.
 *
 * Src-ici baslik: kurulmaz, public engine/include/rowl/c_api.h'e dokunmaz,
 * paylasilan lib'e sembol eklemez (inline + hidden). Kullanan TU once
 * c_api_internal.hpp'i include eder (invokeNoexcept + RuntimeErrorCode +
 * toEngineChecked gorunur), sonra bu basligi; c_api_internal.hpp PRISTINE
 * tutulur (dosya kapsami disi).
 *
 * Davranis-birebir refactor: op adi, mesaj metni, donus kodu cagri
 * noktasinda aynen kalir; yalnizca setError blogu teklesir. Claim-tipi
 * bagimsizdir: shared_ptr<Engine> (state/audio/embed/guards) ve
 * StoryWriteClaim (story) ayni arayuzu sunar (bool + operator->).
 */
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(hidden)
#endif

template <typename Claim>
inline void stampInvalidArgument(const Claim& claimed, const char* message,
                                 const char* op) noexcept {
    invokeNoexcept([&] {
        if (claimed) {
            if (auto* ctx = claimed->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                              message, op, "");
            }
        }
    });
}

// W8-f1 (2): context-only stamp asiri-yuku — story vektorune okuma/yazma
// yok, o yuzden story kapisi (g_storyGate) gerekmez. Checked-claim helper
// govdesinde kurulur (story-gate kilidinin taradigi TU'nun disinda), boylece
// c_api_story.cpp'de ciplak toEngineChecked cagrisi kalmaz ve gate-1 yesil
// kalir. Davranis-birebirdir: ayni op + mesaj + donus kodu; non-template
// oldugu icin ham handle cagrisinda template'ten onceliklidir.
inline void stampInvalidArgument(RowlEngineHandle handle, const char* message,
                                 const char* op) noexcept {
    stampInvalidArgument(toEngineChecked(handle), message, op);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
