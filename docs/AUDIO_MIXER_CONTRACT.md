# Audio Mixer Contract (Faz 5 Dilim 2 — native hat)

Companion to `AUDIO_STREAMING_CONTRACT.md` (Faz 5 Dilim 1). That document
is NOT modified by this slice; its threshold semantics, routing rules and
ring-buffer behavior are untouched. Sources of truth are the
implementations named below; when this document and code disagree,
**code wins** and this document must be patched.

Locked decisions: additive-only C ABI (new capability bit `16384`,
`ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY`, OR-ed into
`GetCapabilities`; bits 1..8192 intact); `StreamMixer` is the single gain
source inside `applyChannelGains()` with bit-identical math (`master*bus`,
duck only BGM); short-audio full-decode stays byte-identical;
`shouldStreamRoute` stays strict `>`; `engine.cpp`, `window.cpp`,
`AUDIO_STREAMING_CONTRACT.md`, `LONG_AUDIO_CONTRACT` and the 64 MiB formula
are untouched; C#/editor belongs to another agent.

## 0. Naming note (authoritative ABI)

The authoritative native names — in `engine/include/rowl/c_api.h`,
`engine/src/c_api_audio.cpp` and every test — are:

- `RowlEngine_SetFadeCurve` / `RowlEngine_GetFadeCurve` (0 = Linear,
  1 = EqualPower)
- `RowlEngine_SetSfxPoolDepth` / `RowlEngine_GetSfxPoolDepth` /
  `RowlEngine_GetSfxActiveVoices` / `RowlEngine_GetSfxActivePaths`
- `RowlEngine_PlayAmbienceBed` / `RowlEngine_StopAmbienceBed` /
  `RowlEngine_SetAmbienceBedVolume` / `RowlEngine_GetAmbienceBedVolume` /
  `RowlEngine_IsAmbienceBedPlaying`
- `RowlEngine_CrossfadeAmbienceTo` / `RowlEngine_IsAmbienceCrossfadeActive`
- `RowlEngine_GetBgmPumpStatsJson` / `RowlEngine_GetBgmPumpAvgMicroseconds`

No duplicate ABI was added; no legacy entry point changed signature.

## 1. Mixer (owner: `stream_mixer.hpp`, reader: `applyChannelGains`)

- `StreamMixer` (header-only) extends the Dilim-1 `mixer_buses.hpp`
  skeleton with Ambience + Ui buses (plus a second ambience bed, §4).
  `mixer_buses.hpp` itself stays as an unused skeleton with a note.
- `AudioEngine::applyChannelGains()` reads gains ONLY from the mixer.
  Member volumes (`m_masterVolume`, `m_bgmVolume`, …) are kept
  bidirectional-synced by every setter, so legacy getters and telemetry
  formulas are unaffected.
- Math is bit-identical to the Dilim-1 chain: `master*bus` everywhere,
  duck (`master*bgm*duckFactor`) only on BGM. Ui reads the independent
  `master*ui` bus (`gainFor(Ui)`) for both its stream gain and its
  telemetry; the Ui sample-gain bake in the decode path is removed, so Ui
  is applied exactly once and the Sfx bus no longer gates UI.
- BGM transition base gain is `mixer.gainFor(Bgm)`; the per-frame
  outgoing/incoming scale comes from §3.

## 2. SFX polyphony (owner: `sfx_polyphony.hpp` + pool streams)

- `SfxVoicePool` (header-only, SDL-free state): default depth 8, max 16,
  `setDepth` clamps to [1,16]. Empty slot wins (lowest index); when full,
  the smallest sequence number is stolen (steal-oldest). Depth 1 is the
  legacy single-voice behavior (new sound replaces the old one).
- Each slot owns a physical `SDL_AudioStream` (`m_sfxPoolStreams`, same
  `master*sfx` gain). `playAudio`/`playAudioInt` entries are unchanged:
  the slot is picked BEFORE the device queue and the PCM is committed
  AFTER a successful queue, so queue failure stays atomic (no ghost voice).
- `shutdown`, `reopenDeviceStreams`, `setOutputSuspended` and `stopAll`
  cover every pool stream. After a device rebuild, playing voices are
  re-queued from their saved offset (no restart); the float format stored
  per voice is reused.
- Telemetry (`m_telemetrySfx`) reads the pool sum: per-voice peak/RMS at
  `master*sfx` gain, clamped to [0,1]. Ui one-shot keeps its own stream
  (`m_uiStream`) and its own telemetry.
- `playVoiceBlip(..., Sfx)` also routes into the pool (asset blips keep
  their decoded format; procedural blips are mono 48 kHz float).

## 3. Fade curves (owner: `fade_curves.hpp`, header-only pure math)

- `FadeCurve::Linear` (0, default) reproduces the legacy formulas
  op-for-op and is therefore bit-identical: crossfade `out = 1-p`,
  `in = p`; `Fade`-kind `out = max(0,1-2p)`, `in = max(0,2p-1)`.
- `FadeCurve::EqualPower` (1): `out = cos(p*π/2)`, `in = sin(p*π/2)`,
  with endpoint snaps (`p<=0 → 1/0`, `p>=1 → 0/1`) because `cos(π/2)` is
  not exactly zero in float; `p=0.5` yields √2/2 on both legs.
- Out-of-range and NaN progress snap to the nearest endpoint
  (fail-closed, never NaN gain). Curves are used by the BGM transition
  (`Fade` kind uses the halved `fadeKind*` ramps) and the ambience
  bed-to-bed crossfade (full `fadeCurve*` ramps).

## 4. Ambience beds (owner: `AudioEngine` bed state + crossfade state)

- Two independent beds: BedA = 0 (the legacy single-bed path: same
  members, same flags, same `playAudio(Ambience)` feed), BedB = 1 (new
  stream + PCM + offset + path + volume). Each bed has its own
  path + volume + playing state; single-bed use is unchanged.
- Bed-to-bed crossfade: `crossfadeAmbienceTo(asset, duration, curve)`
  decodes first (failure returns false, nothing changes), then fades the
  sounding bed out on the other bed while fading the new asset in, driven
  from `update()` with per-frame stream gains. Completion stops the
  from-bed and restores base gains via `applyChannelGains()`.
- `duration <= 0` (or non-finite) is an instant switch — identical to the
  legacy in-place replace of the sounding bed (silent bed cleared).
- Bed effective gain: `master*bedVolume` times the crossfade scale
  (`1.0` when no crossfade is active). Bed float formats are stored at
  queue time so a device rebuild re-queues both beds exactly.

## 5. Pump observability (owner: `pumpBgmStream` timing window)

- `pumpBgmStream` records a `steady_clock` microsecond sample per call
  into a last-64 ring (`m_pumpWindow`) plus count / last / max. Guard and
  suspend early-returns record NO sample (suspend never advances decode).
- Read surface (C++ + C API): sample count, last, average over the filled
  window, max, and a `{"count","last_us","avg_us","max_us"}` JSON string
  via the standard caller-buffer contract.
- Observability only: there is NO fail gate on pump cost. Comparisons are
  meaningful only on the same device (documented, not enforced).

## 6. Observability (C API, capability `16384`)

- `ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY == UINT64_C(16384)`,
  OR-ed into the `GetCapabilities` mask; bits 1..8192 are intact.
- JSON getters (`GetSfxActivePaths`, `GetBgmPumpStatsJson`) follow the
  `GetLocale`-style caller-buffer contract (NULL/0 size query, undersized
  buffer clears + `BUFFER_TOO_SMALL` + `outRequiredSize`, dead handle
  `INVALID_HANDLE`). Scalar setters clamp to [0,1] and ignore non-finite
  input (last valid value kept); dead-handle getters return 0/0.0f;
  invalid bed returns false / no-op / 0.0f / 0.

## 7. Editor surface (C# gerçekliği — fix turu 1)

- `NativeBridge`: 15 yeni girdinin 1:1 Cdecl aynası (fade-curve,
  pool-depth, bed play/crossfade, pump JSON dahil) +
  `SetAmbienceVolume`/`SetUiVolume` (Dilim 1 bus'ları).
- `AudioMixerService`: saf karar katmanı (parse/clamp/read helpers,
  pump JSON, rozet metni); native çağrı YOKTUR, throw YOKTUR.
- Mixer masası: prod `PlayerViewModel.ApplyVolumes` 6 bus'ı
  (`master/bgm/voice/sfx/ambience/ui`) + global config'i
  (`MixerFadeCurve`, `SfxPoolDepth` — profil tek kaynak,
  Linear/8 fail-closed) `IPlayerEngine` dikişinden native'e yazar.
  `EngineHostPlayerAdapter` ambience/ui/eğri/derinliği canlı handle
  üzerinden `NativeBridge`'e forward eder (`EngineHost` diffsiz;
  ölü handle no-op, clamp, non-finite ignore).
- Per-node mixer knob'ları (node `fade_curve`, `sfx_pool_depth`,
  `ambience_bed_b*`, crossfade) KALDIRILDI: runtime'a bağlanamayan
  yüzey bırakılmadı. Eski proje anahtarları Deserialize'da sessizce
  yoksayılır; kullanılmayan ambience dosyaları normal unused-asset
  kuralına tabidir. Polyphony-depth linter kuralı kaldırıldı
  (derinlik artık profil kaynağında, yazımda clamp'lenir).
- Pump okuması salt-gözlemdir (`UpdateMixerBadge` rozet yansıması);
  yoklama kadansını çağrıcı seçer.
- Bu bölümdeki her cümle C# koduyla birebir örtüşür (code wins).

## 8. Fail-closed matrix

| Input | Result |
|---|---|
| Null/dead handle (any new entry) | 0 / 0.0f / `INVALID_HANDLE` / no-op |
| Invalid `bed` (≠ 0/1) | false / no-op / 0.0f / 0 / `""` |
| Invalid curve int (≠ 0/1) | ignored, last valid curve kept |
| Pool depth outside [1,16] | clamped ([min 1, max 16]) |
| Non-finite volume/duration | ignored (volume) / instant switch (duration) |
| Missing/undecodable bed asset | false, prior beds untouched |
| Queue failure (device) | pool slot untouched (atomic pick/commit) |
| NaN/out-of-range fade progress | endpoint snap, never NaN gain |
| Pump without stream / under suspend | no sample recorded |

## 9. Test matrix (where each row is pinned)

All in `tests/test_audio_mixer.cpp` (`void test_audio_mixer()`), wired
into `tests/CMakeLists.txt` + harness + runner:

- Mixer parity: default gains 1.0; `master*bus` per bus (exact binary
  values); duck only BGM incl. restore; member↔mixer sync; NaN ignored.
- Polyphony: pool unit test (default 8 / max 16 / clamp / steal-oldest /
  depth-1 replace / stopAll) + engine depth round-trip and clamps.
- Curves: Linear bit-identical to legacy ops; EqualPower endpoints
  (p=0/0.5/1 → 1/0, √2/2, 0/1); NaN/overflow snaps; engine default Linear.
- Beds: invalid-bed fail-closed; BedA legacy-flag mirror; independent
  volumes + clamp + NaN; timed crossfade completion (Linear + EqualPower);
  instant switch = legacy replace; missing asset false; `playAudio`
  legacy feed unchanged.
- C API: 16384 advertised (+ value assert) with bits 1..8192 intact;
  full null-handle vector; curve/bed/depth round-trips + clamps;
  caller-buffer size/undersized/exact vectors for paths + pump JSON;
  pump schema keys; missing-asset fail-closed.
- Pump: fresh counters zero; no sample without stream / under suspend;
  stats JSON keys.
- Deliberate-break proofs (each: break → red test → revert): mixer duck
  drop (`stream_mixer.hpp`), steal-newest (`sfx_polyphony.hpp`), default
  curve flip (`audio_engine.cpp` `initialize`).

## 10. Dilim 2 scope notes (fix turu 1 sonrası)

- `mixer_buses.hpp` remains an unused skeleton (parity reference only).
- `engine.cpp` / `window.cpp`, `EngineHost.cs`, `MainWindowViewModel.cs`
  and `AUDIO_STREAMING_CONTRACT.md` have zero diff; Dilim-2 `engine/audio`
  + C API + tests are in scope.
- C# editor surface is LIVE (see §7): volumes + global mixer config
  reach native through `IPlayerEngine`; per-node mixer knobs removed.
- `AUDIO_STREAMING_CONTRACT.md`, `LONG_AUDIO_CONTRACT`, the 64 MiB
  formula and threshold semantics are untouched by this slice.
- Fade-curve/pool-depth apply fixed at the profile defaults (Linear/8) in
  v1 with no separate user UI; a later slice may add one (no dead surface).
