# Rowl Engine Productization Baseline

Last reviewed: 2026-09-14

This document is the source-of-truth entry gate for the productization roadmap.
It separates tested behavior from planned behavior and keeps release claims tied
to an explicit proof artifact. `IMPLEMENTATION_STATUS.md` remains the detailed
record of completed engineering work.

## Status vocabulary

- **Ready:** implemented and covered by a repeatable repository gate.
- **Partial:** a usable foundation exists, but the release-facing workflow is incomplete.
- **Missing:** no end-to-end product implementation exists.
- **External proof:** implementation or skeleton exists, but the required GUI/device gate has not run.

## Current capability inventory

This inventory's player rows describe the **exported native RowlGame** unless
a row explicitly names the editor. The Avalonia PlayerWindow is an editor-hosted
surface and is not included in the exported native package. See
[PLAYER_RELEASE_PARITY.md](PLAYER_RELEASE_PARITY.md) for the concrete matrix.

| Area | Status | Current proof or missing product behavior | Roadmap owner |
| --- | --- | --- | --- |
| Transactional story graph and navigation | Ready | Parser/runtime isolation, choice routing and invalid-load rollback are covered by native tests | Completed foundation |
| Save/load/rewind integrity | Ready | Versioned bounded reads, atomic writes, migration diagnostics and restore side-effect guards are covered | Completed foundation |
| Standalone pause and ten save slots | Ready | Native SDL pause overlay, volume/text controls, quick slots, rewind and confirmed exit exist; title/backlog/profile parity is tracked separately | Completed foundation |
| Player title/new/continue flow | Missing | Standalone starts directly in the story | Phase 2 |
| Standalone backlog | Partial | Bounded runtime history and editor backlog panel exist; exported RowlGame has no backlog surface. Voice replay is absent from both surfaces. | Phase 2 parity follow-up |
| Global auto and read-aware skip | Partial | Editor PlayerWindow has profile-driven auto/skip/read tracking; native player has per-dialogue `auto_advance` but no global read-aware workflow | Phase 2 parity follow-up |
| Persistent player profile | Partial | Versioned profile persistence exists in the editor-hosted player; exported RowlGame does not load/save the equivalent profile independently of story slots | Phase 2 parity follow-up |
| Localization | Ready | Locale manifest + `content_id` catalogs, BCP 47 chain-aware fallback and additive C ABI are covered by test_localization.cpp and the T1-T7 locale-cluster locks (test_locale_cluster.cpp) (native runtime scope; managed `NormalizeLocale`/`ValidateCatalog` deviations — see `LOCALIZATION_CONTRACT.md` §5) | Phase 3 |
| Unicode shaping and rich text | Partial | UTF-8 codepoint rendering/typewriter exist; shaping, BiDi, CJK breaks and inline style spans do not | Phase 3 |
| Large graph authoring | Partial | Pan/zoom, selection and basic first-match search exist; culling, minimap, result lists, groups and subgraphs do not | Phase 4 |
| Project linter | Partial | Topology and missing-asset checks exist; script, media, locale, glyph, duplicate-ID and portability checks do not | Phase 4 |
| Asset import contract | Partial | Files are copied and categorized; accepted MP3/WebP/FLAC paths do not match runtime decode support | Phase 1 |
| Long-form audio | Partial | WAV/OGG playback, DSP and BGM transitions exist; decoded tracks are retained as full PCM buffers | Phase 5 |
| Layered characters and expressions | Missing | Multiple character sprites exist, but no character definition or body/face/outfit/accessory slots exist | Phase 5 |
| Bounded texture cache and VFS streams | Ready | 64 MiB default LRU, bounded negative cache and package read streams are covered | Completed foundation |
| General shaders, post-processing and particles | Missing | Cinematic CPU/SDL effects and optional GPU-MSDF exist; no general material/effect pipeline exists | Phase 9 |
| C ABI diagnostics and ownership | Partial | Structured results, owner-thread guards and length-aware borrowed strings exist; base version/capability (RowlEngine_GetApiVersion c_api.h:138, RowlEngine_GetCapabilities c_api.h:142) and caller-buffer variants (RowlEngine_GetSaveDirectoryUtf8 c_api.h:151, RowlEngine_GetProfileDirectoryUtf8 c_api.h:156, plus GetLocale, GetChoiceLabelAtUtf8, GetStreamInfoJson and about 20 Utf8 caller-buffer variants) exist — native scope: version/capability run handleless, caller-buffer NULL/0 size-query and BUFFER_TOO_SMALL contract holds; C# scope: P/Invoke reads outRequiredSize NUL-included via caller-buffer and carries no borrowed-pointer lifetime; still missing: strict Checked coverage and the diagnosis dome | Phase 1/9 |
| Linux package | External proof | CI/headless/package smoke is green; interactive GUI/input/audio proof is pending | Beta gate |
| Windows package | External proof | CI build/package is green; GUI/input/audio/Unicode-save proof is pending | Beta gate |
| macOS/iOS/Android | External proof | Host/build skeletons exist without signed package or real-device proof | Phase 7 |
| Gallery, music room and achievements | Missing | No unlock profile, replay isolation or authoring surface exists | Phase 8 |

## Release acceptance matrix

| Capability | Closed beta | Desktop 1.0 | 1.x / platform expansion |
| --- | --- | --- | --- |
| Title, preferences, backlog, save/load | Required | Required | Required parity |
| Global auto and read-aware skip | Required | Required | Required parity |
| Persistent profile and stable content IDs | Required | Required | Required parity |
| Full shaping/BiDi/CJK and safe rich text | Required | Required | Required parity |
| Graph culling, minimap, search results and subgraphs | Required | Required | Required in desktop editors |
| Complete build-blocking linter | Required | Required | Required in desktop editors |
| Streaming audio and layered characters | Planned | Required | Required parity |
| Chapter-based lazy story loading | Planned | Required | Required parity |
| Linux/Windows real GUI and Unicode-save proof | Required | Required | Maintained |
| Linux/Windows installers and rollback | Planned | Required | Maintained |
| macOS/iOS/Android signed real-device proof | Not blocking | Not blocking | Required per platform release |
| Gallery, achievements and scene replay | Not blocking | Not blocking | Required for the designated 1.x release |
| General post-processing and particles | Not blocking | Not blocking | Capability-gated, benchmark-backed |

## Benchmark identity contract

New native and editor benchmark reports use schema version 2. A comparison is
valid only when all of these fields match:

- schema version and fixture identity;
- build type;
- operating system;
- process architecture (`x86_64` or `arm64` for supported releases);
- CPU model and logical CPU count;
- machine identity.

Schema-v1 reports remain readable and comparable only with other schema-v1
reports. They must never be compared to schema-v2 reports. Absolute release
thresholds may be set only after a reference-machine series exists for the
same build and fixture.

## Phase 0 remaining proof artifacts

- Product Golden Project v2 freezes stable dialogue `content_id` values, EN/TR
  catalogs, a packaged Unicode image path, a 60-second PCM audio fixture and a
  reproducible corrupt-image recipe. Its manifest validator checks catalog
  coverage/identity, Unicode path integrity and real decoded audio duration;
  runtime locale selection and corrupt-asset diagnostics remain assigned to
  their Faz 3 and Faz 1 owners respectively.
- Generate the deterministic 2,000-node/6,000-edge editor fixture with
  `tools/generate_editor_scale_fixture.py`; its identity/count/determinism
  contract is enforced by CTest. The schema-v2 editor series measures its real
  parse/hydration, ViewModel population, worst-case search, selection input,
  full serialization, atomic save and managed-memory paths under the dedicated
  `editor-productization-scale-v1` fixture identity.
- Record Linux and Windows reference-machine series; do not infer real GUI/device support from headless results.
- The `linux-arm64-compile` CI job runs on GitHub's native
  `ubuntu-24.04-arm` runner, compiles the core/player/native tests and rejects
  artifacts that are not ELF AArch64. This remains compile evidence only: it
  does not claim GUI, audio-device or physical-device validation and cannot
  block an otherwise proven first `x86_64` package solely for missing device
  coverage.
