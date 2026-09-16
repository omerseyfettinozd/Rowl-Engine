# Audio Streaming Contract (Faz 5 Dilim 1)

Editor-facing companion to `LONG_AUDIO_CONTRACT.md` (the 64 MiB advisory
contract). That document is the threshold authority and is NOT modified by
this slice; its §5 ("Streaming handoff") points here for implementation
detail. Sources of truth are the implementations named below; when this
document and code disagree, **code wins** and this document must be patched.

Locked decisions: additive-only C ABI; streaming applies only to BGM when
the header-probed duration exceeds the 64 MiB threshold (strict `>`, via
`long_audio_contract`); the short-audio full-decode path stays
byte-identical; `engine.cpp`, `window.cpp`, `MainWindowViewModel.cs` and
`EngineHost.cs` behavior beyond additive wrappers is untouched.

## 0. Naming note (ADIMLAR draft vs authoritative ABI)

Early slice notes used `IsAudioStreaming` / `GetAudioStreamInfoJson`.
The authoritative native names — already in `engine/include/rowl/c_api.h`,
`engine/src/c_api_audio.cpp`, `NativeBridge.cs`, `EngineHost.cs` and every
test — are:

- `RowlEngine_IsStreaming` / `NativeBridge.RowlEngine_IsStreaming` /
  `EngineHost.IsStreaming`
- `RowlEngine_GetStreamInfoJson` /
  `NativeBridge.RowlEngine_GetStreamInfoJson` / `EngineHost.StreamInfoJson`

No duplicate ABI was added; wherever older notes say `IsAudioStreaming`,
read `IsStreaming`, and wherever they say `GetAudioStreamInfoJson`, read
`GetStreamInfoJson`.

## 1. Formula (owner: `long_audio_contract.hpp`, advisory)

```
maxSeconds = 64 MiB / (sampleRateHz * channelCount * bytesPerSample)
```

- `64 MiB = 67,108,864 bytes`, shared with the decode path's
  `kMaxDecodedAudioBytes` enforcement cap. The decode TU remains the
  enforcement point; this contract is advisory (warn / route, never reject
  a decodable asset).
- `bytesPerSample` is decoded PCM bytes: WAV `bitsPerSample / 8`,
  OGG/Vorbis `2` (Vorbis decodes to S16 before float conversion).
- Degenerate inputs (zero rate/channels/bytes, non-finite math) yield
  `0.0` — unknown audio never claims a usable threshold.
- Reference vector: 44.1 kHz / stereo / 2 B → `380.43573696145125 s`.
  `duration == threshold` stays RAM (strict `>`); `threshold == 0` stays
  RAM; `duration == -1` (unknown header) stays RAM.

## 2. Probe (owner: `probeAudioHeaderDuration`, headers only)

- Duration comes from container headers only — WAV `fmt `/`data` chunk
  sizes, OGG Vorbis identification header (rate/channels) plus the maximum
  page granule position (`maxGranule / sampleRate`). No packet is decoded,
  no PCM is allocated.
- A `data` chunk that ADVERTISES more PCM than the budget is the signal,
  not an error: the probe trusts the claimed size so over-threshold intent
  is caught without a full decode (the decode path would reject the same
  asset at its 64 MiB cap).
- Unknown containers, truncated headers, missing granules →
  `known == false` / negative duration: fail closed, never warn, RAM path.
- `decideStream` is a pure wrapper over `probeAndAssessLongAudio` (no
  formula copy). `StreamDecision` + `StreamInfo`/`toJson` live in
  `engine/include/rowl/audio/audio_streaming.hpp` (hand-built string;
  non-finite doubles are emitted as JSON `null`, unknown duration as `-1`).

## 3. Routing (owner: `AudioEngine::playAudio`, BGM only)

- At `playAudio`/`playBgm` entry the BGM candidate is probed;
  over-threshold (`duration > threshold`, strict) + OGG + BGM channel opens
  the stream (`openBgmStream` via `OggStreamSource` over a VFS seekable
  istream, `ov_open_callbacks`/`ov_read`, S16LE → float, 32 KB chunk cap,
  no full decode, no thread). Anything else keeps the existing full-decode
  RAM path byte-identical.
- Over-threshold WAV falls through to RAM (decode cap rejects it
  fail-closed, previous intent preserved). Voice/Sfx/Ui always stay RAM.
- `pumpBgmStream` runs synchronously from `AudioEngine::update()` (gated on
  ~0.5 s queued device audio); suspend skips the pump (position kept);
  device rebuild requeues the ring window (path + granule frontier kept, no
  restart); silent fallback never streams.
- Seek exists only for internal loop-wrap + device-recovery restore (no
  C API). No prefetch thread this slice.

## 4. Ring buffer

- 4×4096 frames float, heap-allocated once (16384×8 floats ≈ 512 KB max),
  circular-overwrite history; `buffered_seconds` = min(decoded,
  capacity)/rate, display-only (never affects playback).
- RAM / unknown / no-BGM / dead handle report `buffered_seconds = 0.0`.
- `IsBgmStreamed` / `GetBgmStreamBufferedSeconds` are NOT separate APIs;
  they fold into the `buffered_seconds` field.

## 5. Bus map (dual-space table — code mirror, code wins)

PlayAudio int → engine branch:

| int | Branch | Playback |
|-----|--------|----------|
| 0 | Bgm | loop; only streamable branch |
| 1 | Voice | one-shot; always RAM |
| 2 | Sfx | one-shot; always RAM |
| 3 | Ambience | loop RAM (+ stream-ready handle, no streaming this slice) |
| 4 | Ui | one-shot on physical sfxStream, gain baked into samples; always RAM |
| other | Sfx fallback (existing else-branch) | — |

Telemetry bus ids (renumbering forbidden, `3 = Master` preserved):

| id | Bus |
|----|-----|
| 0 | Bgm |
| 1 | Voice |
| 2 | Sfx |
| 3 | Master |
| 4 | Ambience |
| 5 | Ui |

Mixer headers (`mixer_buses.hpp`: `BusId{Bgm,Voice,Sfx,Master}`,
`effectiveBgmGain() = master*bgm*duck`; `stream_mixer.hpp`: +Ambience/Ui)
are skeletons this slice — no mixing moves; `applyChannelGains` stays the
single gain source and its math is unchanged.

Volumes: setters clamp to `[0,1]`, non-finite input ignored (last valid
value kept); dead-handle getters return `0.0f`.

## 6. Observability (C API, capability `8192`)

- `ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING = UINT64_C(8192)`, OR-ed into
  `RowlEngine_GetCapabilities` (bits 1..4096 untouched).
- `RowlEngine_IsStreaming`: `1` while the live BGM decision is stream,
  `0` for memory / unknown / no-BGM / dead handle (fail closed).
- `RowlEngine_GetStreamInfoJson`: caller-buffer contract identical to
  `GetLocale` — NULL/0 size query; undersized buffer cleared +
  `BUFFER_TOO_SMALL` + `outRequiredSize`; dead handle `INVALID_HANDLE`;
  success `OK`.
- StreamInfo schema (single decision source, hand-built string):

```json
{"mode":"memory|stream|unknown","duration_seconds":594.43,
 "threshold_seconds":380.43,"threshold_bytes":67108864,
 "buffered_seconds":1.37,"reason":"under_threshold|over_threshold|no_bgm|unknown_header",
 "channel":0,"asset":"audio/boss_theme.ogg"}
```

  - `mode == stream ⟺ reason == over_threshold ⟺ duration > threshold`.
  - `duration_seconds == -1 + mode == unknown` = header unrecognized
    (fail closed, no warn). `threshold_bytes` always `67108864`.
  - Wire-format null rule: non-finite `duration_seconds` /
    `threshold_seconds` / `buffered_seconds` are emitted as JSON `null`,
    never bare `NaN`/`Infinity` (invalid JSON). The C# surface reads these
    fields as nullable doubles and maps explicit `null` (or a non-finite
    number, defensively) to `double.NaN`; missing/wrong-typed fields keep
    their fail-closed fallbacks (`duration -1`, `threshold/buffered 0.0`).
    `IsStream` is unchanged (`mode == stream` + strict finite
    `duration > threshold`), so null-wire never streams.
  - `channel` echoes the PlayAudio int (0–4, unknown ids fall to Sfx).

C# bridge (`NativeBridge.cs`, Cdecl, signatures 1:1): `IsStreaming`,
`GetStreamInfoJson` (caller-buffer), `GetBgmVolume`,
`SetAmbienceVolume`/`GetAmbienceVolume`, `SetUiVolume`/`GetUiVolume`.
`EngineHost` exposes them as `IsStreaming`, `StreamInfoJson`,
`BgmVolume`, `SetAmbienceVolume`/`AmbienceVolume`,
`SetUiVolume`/`UiVolume` (fail-closed defaults). Pure decision surface
without native calls: `Services/AudioStreamingService.cs`
(`Parse`/`IsDecisionConsistent`/`ReadIsStreaming`/`ReadSnapshot`/
`ReadBadgeText`/`AcceptVolume`/`ChannelName`); badge text single-sources
`AudioStreamingBadgeService.Describe` (bad JSON → hidden badge, `unknown`).

## 7. Editor surface (read-only)

- Badge: `Controls/AudioStreamingBadge` (stream-only green badge) driven by
  `AudioComponentViewModel.StreamMode`/`StreamBadgeText`/
  `IsStreamBadgeVisible`/`UpdateStreamingBadge`, polled inside the existing
  telemetry tick (≤ 4 Hz), tooltip carries duration/threshold/reason; no new
  commands, preview path unchanged. Not embedded in `MainWindow.axaml`;
  lives in the existing preview-panel child slot via
  `AudioComponentView.axaml`.
- Linter: `ProjectLintService.CheckLongAudioStreaming`
  (`ProjectLintOptions.CheckLongAudio`, default true, severity warning,
  `IsError = false`, honors `MaxIssuesPerRule`). Over-64 MiB OGG →
  "takes the OGG streaming path"; over-64 MiB non-OGG → RAM-fallback
  warning with convert-to-OGG hint. Missing files stay silent (Validate
  owns them). `ProjectValidationService.cs` unchanged.

## 8. Fail-closed matrix

| Input | Result |
|-------|--------|
| Dead handle | `IsStreaming = 0`, `GetStreamInfoJson = INVALID_HANDLE`, volumes `0.0f` / setter no-op |
| No BGM | `mode = unknown/memory`, `reason = no_bgm`, `buffered = 0.0` |
| Corrupt/truncated header | `mode = unknown`, `duration = -1`, `reason = unknown_header`, RAM intent kept, no warn |
| `duration == threshold` | RAM (strict `>`) |
| Over-threshold WAV | RAM fallback (decode cap rejects), intent preserved |
| Voice/Sfx/Ui over-threshold | RAM (streaming is BGM-only) |
| Undersized caller buffer | cleared + `BUFFER_TOO_SMALL` + required size |
| Non-OGG/Opus/MP3/FLAC/WebP | rejected by `MediaFormatCatalog` (only OGG Vorbis + WAV) |
| Bad StreamInfo JSON in editor | hidden badge, `unknown` mode, no throw |

## 9. Test matrix (where each row is pinned)

- Native `tests/test_audio_streaming.cpp` (new TU; `test_audio_engine.cpp`
  and `test_audio_device_recovery.cpp` untouched; dummy driver + temp dir +
  virtual clock): routing edges (eşik/±1 ms, `-1`, `threshold = 0`);
  short-audio byte-identical (`memcmp` vs legacy decode, OGG + WAV);
  `OggStreamSource` mechanics (EOS total vs `ov_pcm_total`, seek-0 first
  chunk, probe consistency rate/ch/bps = 2); virtual-clock soak (0.1 s loop
  + 360× `update(1.0 s)`, DEVICE_REMOVED t120 + FORMAT_CHANGED t240,
  path + pcmPos preserved, RSS Δ ≤ 8 MiB); StreamInfo contract (8192 ad,
  size-query, `BUFFER_TOO_SMALL`, key set, dead handle, corrupt OGG
  `unknown_header`, 29/30/31 s edges, 100 MiB claim ≈ 594.43 s / ≈ 380.44 s
  threshold); channel-compat matrix (0/1/2 map, 3/4 round-trip + NaN
  ignore, ch 99 → Sfx, telemetry Master + 4/5).
- Editor `Tests/EditorAudioStreamingSliceTests.cs` (headless, sparse-file +
  JSON fixtures, no native): badge stream/memory/broken, lint OGG-over /
  WAV-over / missing + disabled, view-model mirror, options default true.
- Editor `Tests/EditorAudioStreamingServiceTests.cs` (this slice, headless,
  no native): StreamInfo parse round-trip, memory/unknown never-stream,
  broken-input fail-closed, strict-`>` edges (29/30/31 s), inconsistent-claim
  detection, null-handle/delegate-throw fail-closed, live-delegate mirror,
  volume clamp + non-finite ignore, channel fallback, null-wire
  (`Parse_NullNonFinite_MapsToNaN`: explicit `null` → `double.NaN`, never
  streams) and unknown-header (`StreamInfo_UnknownHeader_NeverStreams`:
  `mode == unknown` + `duration == -1` ⇒ `IsStream == false`).
- C API vectors `tests/test_c_api_contract.cpp` + `test_c_api_header.c`:
  8192 static assert + 7 symbols, null-handle vectors, caller-buffer
  size-query/undersized, schema keys, clamp/NaN.

## 10. Dilim 1 editor+docs scope notes

- Wire-format null rule: see §6 — non-finite doubles are JSON `null`, mapped
  to `double.NaN` in C# via nullable-double reads; `IsStream` unchanged.
- `EngineHost.cs` zero-diff: this slice leaves `EngineHost.cs` behaviorally
  untouched (already returned to zero-diff). There is no StreamInfo
  pass-through in the host; the single editor access path is
  `Services/AudioStreamingService.cs` (`Parse`/`TryParse`/`ReadSnapshot`)
  plus `AudioStreamingBadgeService.Describe` for badge text, both reading
  through `NativeBridge` delegates. No new host API was added.
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
