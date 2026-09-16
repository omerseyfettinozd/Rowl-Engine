# Long-Audio Threshold Contract (Faz 4.5 Dilim 3)

Canonical reference for the long-audio advisory contract. Sources of truth
are the implementations named below; when this document and code disagree,
code wins and this document must be patched.

Locked decisions: ZERO touch on the audio decode/playback path
(`engine/src/audio/audio_engine.cpp` is read-only for this slice);
capability bit 2048; Linux + Windows first, mobile path not blocked
(the contract is pure header math with no OS or device calls).

## 1. Formula and 64 MiB basis

```
maxSeconds = 64 MiB / (sampleRateHz * channelCount * bytesPerSample)
```

- `64 MiB = 67,108,864 bytes` (`kLongAudioBudgetBytes`), shared with the
  decode path's `kMaxDecodedAudioBytes` enforcement cap. The decode TU
  remains the enforcement point; this contract is advisory (warn, never
  reject).
- `bytesPerSample` is decoded PCM bytes: WAV `bitsPerSample / 8`,
  OGG/Vorbis `2` (the engine decodes Vorbis to S16 before float
  conversion).
- Degenerate inputs (zero rate/channels/bytes, non-finite math) yield
  `0.0` — unknown audio never claims a usable threshold.
- Reference vectors (pinned by `test_audio_engine.cpp` literals):

| sampleRate | channels | bytesPerSample | maxSeconds       |
|------------|----------|----------------|------------------|
| 44100      | 2        | 2              | 380.435736961... |
| 48000      | 2        | 2              | 349.525333333... |
| 44100      | 1        | 2              | 760.871473922... |
| 48000      | 8        | 4              | 43.6906666666... |
| 8000       | 1        | 1              | 8388.608          |

## 2. Header-probe behavior

Owner: `engine/include/rowl/audio/long_audio_contract.hpp`
(`probeAudioHeaderDuration`, WAV/OGG dispatched by magic).

- Duration comes from container headers only — WAV `fmt `/`data` chunk
  sizes, OGG Vorbis identification header (rate/channels) plus the maximum
  page granule position. No packet is ever decoded, no PCM is allocated.
- A `data` chunk that ADVERTISES more PCM than the budget is the signal,
  not an error: the probe trusts the claimed size so over-threshold intent
  is caught without a full decode (the decode TU would reject the same
  asset at its 64 MiB cap).
- Unknown containers, truncated headers, and missing granules yield
  `known == false` / negative duration: fail closed, never warn.
- OGG duration is `maxGranule / sampleRate` (Vorbis mapping: granule =
  total PCM samples); pages whose granule is `0xFFFF…` carry no ending
  packet and are skipped.

## 3. Warning semantics

Owner: `assessLongAudio` (log warn + `LongAudioAssessment`).

- Warns if and only if `duration > threshold` (strict). The 29 s / 31 s
  boundary test pins this: against a 30 s threshold, 29 s and exactly
  30 s stay silent, 31 s warns.
- The warning is `ROWL_LOG_WARN` with asset path (or `<buffer>`), probed
  duration, and computed threshold; the returned struct carries the same
  verdict for machine consumers.
- Unknown durations and degenerate thresholds stay silent (fail closed).
- Capability: `ROWL_ENGINE_CAPABILITY_LONG_AUDIO_CONTRACT = 2048`,
  OR-ed into `RowlEngine_GetCapabilities`; no C ABI signature changed.

## 4. Soak strategy (CI-sane)

Owner: `tests/test_audio_engine.cpp` (existing TU, no new TU).

- A `>= 5`-minute loop runs on a VIRTUAL clock: 360 × `update(1.0f)` on
  a tiny real looped BGM, so CI pays milliseconds, not minutes, while the
  loop-feed, telemetry windows, and stream state traverse the same code
  as wall-clock playback. Documented in-code at the test site.
- Mid-soak perturbations: `AUDIO_DEVICE_REMOVED` at simulated t+120 s
  (asserts playback intent + device resume) and
  `AUDIO_DEVICE_FORMAT_CHANGED` at t+240 s (asserts BGM continuation).
  Device-switch routing itself stays covered by
  `test_audio_device_recovery.cpp` and is not duplicated here.
- RSS stability: process RSS sampled before/after the loop must not grow
  beyond an 8 MiB slack (POSIX `getrusage`; Windows
  `GetProcessWorkingSetSize`, kernel32 only so no extra link dep).

## 5. Streaming handoff (Faz 5 Dilim 1)

The contract above is unchanged (code wins on any disagreement) and now
feeds one more consumer: the OGG streaming router. `decideStream` is a
pure wrapper over `probeAndAssessLongAudio` (no formula copy); only a BGM
asset whose header-probed duration is strictly over threshold takes the
streaming path, and only when it is OGG. Over-threshold WAV keeps the RAM
path (the decode cap rejects it fail-closed). Details, the C API
observability surface (`ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING = 8192`,
`IsStreaming`, `GetStreamInfoJson`, BGM/Ambience/Ui volumes) and the
mixer bus skeletons live in `docs/LONG_AUDIO_STREAMING.md`.

Wire-format note: non-finite StreamInfo doubles are emitted as JSON `null`
(never bare `NaN`/`Infinity`); the editor maps explicit `null` to
`double.NaN` via nullable-double reads and `IsStream` stays `mode == stream`
+ strict finite `duration > threshold`, so null-wire never streams.
