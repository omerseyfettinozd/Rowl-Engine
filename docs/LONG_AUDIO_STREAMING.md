# OGG Streaming Core + Mixer Bus Skeleton (Faz 5 Dilim 1)

Companion to `LONG_AUDIO_CONTRACT.md` (the 64 MiB advisory contract). Sources
of truth are the implementations named below; when this document and code
disagree, code wins and this document must be patched.

Locked decisions: additive-only changes; streaming applies only to BGM when
the header-probed duration exceeds the 64 MiB threshold (strict `>`, via
`long_audio_contract`); the short-audio full-decode path stays byte-identical;
`engine.cpp`, `window.cpp`, `MainWindowViewModel.cs` and `EngineHost.cs`
(public surface beyond additive wrappers) are untouched in behavior.

## 1. Streaming core (pull-model, no threads)

Owner: `engine/include/rowl/audio/ogg_stream_source.hpp` +
`engine/src/audio/ogg_stream_source.cpp`.

- `OggStreamSource` decodes OGG/Vorbis incrementally over a VFS seekable
  istream (`ov_open_callbacks` / `ov_read`), S16LE to float (`/32768`),
  fixed 32 KB chunk cap. No full decode, no thread, no large allocations.
- Limits mirror `audio_engine.cpp` (64 MiB encoded clamp).
- Corrupt / truncated inputs fail closed (`false` + error string, exception
  free); `seekPcmFrame` exists only for internal loop-wrap and
  device-recovery restore (no C API).

## 2. Routing (single decision source)

Owner: `AudioEngine::playAudio` + `engine/include/rowl/audio/audio_streaming.hpp`.

- At `playAudio`/`playBgm` entry the BGM candidate is probed with
  `probeAudioHeaderDuration`; `makeStreamInfoFromHeader` wraps the contract
  (no formula copies) and `decideStream` wraps `probeAndAssessLongAudio`.
- Over-threshold (`duration > threshold`, strict) + OGG + BGM channel opens
  the stream (`openBgmStream`); anything else keeps the existing full-decode
  RAM path byte-identical.
- Over-threshold WAV falls through to RAM (the decode cap rejects it,
  fail-closed, previous intent preserved).
- Ring buffer: 4x4096 frames float, heap-allocated once
  (16384x8 floats ≈ 512 KB max), circular-overwrite history;
  `buffered_seconds` = min(decoded, capacity)/rate, display-only.
- `pumpBgmStream` is called synchronously from `AudioEngine::update()`
  (gated on ~0.5 s of queued device audio); suspend skips the pump
  (position preserved); device rebuild requeues the ring window (path +
  granule frontier preserved, no restart); silent fallback never streams.

## 3. Channel map and volume matrix

- `PlayAudio` int map: 0=Bgm, 1=Voice, 2=Sfx, 3=Ambience (loop RAM, own
  stream), 4=Ui (one-shot, physical sfxStream, gain baked into samples),
  else to Sfx. Snapshot channel fidelity is kept through `playAudioInt`.
- Telemetry buses: 3=Master (preserved), 4=Ambience, 5=Ui.
- Volumes: `[0,1]` clamp + non-finite ignore, last valid value kept;
  dead-handle getters return `0.0f`.

## 4. Observability (C API)

Capability: `ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING = 8192`.

- `RowlEngine_IsStreaming`: 1 while the live BGM decision is stream,
  0 for memory / unknown / no-BGM / dead handle.
- `RowlEngine_GetStreamInfoJson`: caller-buffer contract (same as
  `GetLocale`); schema keys `mode/duration_seconds/threshold_seconds/
  threshold_bytes/buffered_seconds/reason/channel/asset`; non-finite doubles
  are emitted as JSON `null` (never bare `NaN`/`Infinity`), unknown duration
  as `-1`. Editor maps explicit `null` to `double.NaN` via nullable-double
  reads; `IsStream` stays `mode == stream` + strict finite
  `duration > threshold`, so null-wire never streams.
- `RowlEngine_GetBgmVolume`, `RowlEngine_SetAmbienceVolume` /
  `RowlEngine_GetAmbienceVolume`, `RowlEngine_SetUiVolume` /
  `RowlEngine_GetUiVolume`.

## 5. Mixer bus skeletons (no mixing this slice)

- `engine/include/rowl/audio/mixer_buses.hpp`: 4-bus header-only skeleton
  (`BusId{Bgm,Voice,Sfx,Master}`, `effectiveBgmGain()=master*bgm*duck`).
- `engine/include/rowl/audio/stream_mixer.hpp`: 6-bus extension
  (+Ambience/Ui) with `gainFor`/`applyChain`.
- Both are unused by the engine this slice; `applyChannelGains` stays the
  single gain source.

## 6. Editor surface

- `NativeBridge.cs` + `EngineHost.cs`: 7 P/Invokes and wrappers
  (`IsStreaming`, `StreamInfoJson`, `BgmVolume`, ambience/UI volumes).
  `EngineHost.cs` is zero-diff this slice (already returned to zero-diff):
  no StreamInfo pass-through lives in the host; the single editor access
  path is `Services/AudioStreamingService.cs` (`Parse`/`TryParse`/
  `ReadSnapshot`) plus `AudioStreamingBadgeService.Describe`, both reading
  through `NativeBridge` delegates.
- `ProjectLintService.CheckLongAudioStreaming`
  (`ProjectLintOptions.CheckLongAudio`, default true): BGM tracks over
  64 MiB on disk warn (OGG streams, other containers fall back to RAM).
- `AudioStreamingBadgeService` (pure JSON to badge description) +
  `Controls/AudioStreamingBadge` (stream-only green badge) +
  `AudioComponentViewModel` badge props (`StreamMode`,
  `StreamBadgeText`, `IsStreamBadgeVisible`, `UpdateStreamingBadge`).

## 7. Tests

- `tests/test_audio_streaming.cpp` (new TU, 6 groups): threshold pins,
  header-probe edges (29/30/31 s, 100 MiB claim ≈ 594.43 s vs ≈ 380.44 s
  threshold), StreamInfo schema, engine routing (granule-patched OGG),
  C API observability, robustness (virtual-clock soak, suspend, device
  rebuild, fail-closed).
- `editor/Tests/EditorAudioStreamingSliceTests.cs` (xUnit): badge
  service, lint rule (sparse-file fixtures), view-model mirror.
- `editor/Tests/EditorAudioStreamingServiceTests.cs` (xUnit, headless, no
  native): null-wire (`Parse_NullNonFinite_MapsToNaN`: explicit `null` →
  `double.NaN`, never streams) and unknown-header
  (`StreamInfo_UnknownHeader_NeverStreams`: `mode == unknown` +
  `duration == -1` ⇒ `IsStream == false`).
- Granule-patch technique: overwrite the last OggS page granule of the
  small real OGG fixture with a huge value so the probe reports
  over-threshold while the decoder plays real packets then EOS (no
  encoder needed).

## 8. Dilim 1 editor+docs scope notes

- Wire-format null rule: see §4 — non-finite doubles are JSON `null`, mapped
  to `double.NaN` in C# via nullable-double reads; `IsStream` unchanged
  (`mode == stream` + strict finite `duration > threshold`).
- `EngineHost.cs` zero-diff: see §6 — host untouched, no pass-through; single
  access via `AudioStreamingService` / `BadgeService` over `NativeBridge`.
- Benchmark machine-key known limitation: `tools/compare_benchmarks.py` is
  unchanged in this slice; it still rejects reports with different
  OS/machine/CPU/build-type or fixture identity and only reports percentage
  deltas for like-for-like Release builds on the same device. Cross-machine
  comparison staying blocked is a known limitation, not a regression.
- Pump-cost measurement deferred: `pumpBgmStream` cost measurement moves to
  Dilim 2 (mixer) scope; this slice pins only functional behavior (sync pump
  from `update()`, suspend skip, rebuild requeue) with no perf claim.
- Mobile intent-state desktop scope: streaming intent stays pure header math
  with no OS/device calls, so it blocks no future mobile path, but mobile
  remains `evidence-blocked` per `PLATFORM_SUPPORT.md` (host skeletons only,
  no build/package/device proof). No mobile sufficiency is claimed here.
