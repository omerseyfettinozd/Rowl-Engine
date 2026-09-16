# Crash Log Contract (Faz 6 Dilim 1 — `rowl_player`)

Fail-closed native crash logging for the standalone desktop player.
Player flow is never disturbed: every failure mode degrades to "no log".

## Covered signals and hooks (POSIX)

| Source      | Log `reason:` value | Process exit after logging   |
|-------------|---------------------|------------------------------|
| `SIGSEGV`   | `SIGSEGV`           | Re-raised with `SIG_DFL` (dies by signal) |
| `SIGABRT`   | `SIGABRT`           | Re-raised with `SIG_DFL` (dies by signal) |
| `SIGFPE`    | `SIGFPE`            | Re-raised with `SIG_DFL` (dies by signal) |
| `SIGILL`    | `SIGILL`            | Re-raised with `SIG_DFL` (dies by signal) |
| `std::terminate` | `terminate`    | `_exit(134)`                 |

`SA_RESETHAND` is set, so a crash inside the handler itself falls back to the
default disposition instead of recursing.

## Handler constraints

Inside the crash path only async-signal-safe calls run: `open` / `write` /
`close` / `getpid` / `raise` / `signal` / `_exit`, plus the signal-mask
helpers `sigemptyset` / `sigaddset` / `sigprocmask` (and `sigaction` at
install time; `errno` is read only for the `EINTR` retry inside `writeAll`),
plus reads of static
buffers filled ahead of time. In particular the handler never calls `malloc`,
`printf` (or any stdio), `fopen`, `snprintf`, `new`, or any engine C API
(`RowlEngine_GetLast*` included — none of them are signal-safe).

Enforcement: `tests/test_crash_log.cpp` scans
`engine/src/platform/crash_handler.cpp` and
`engine/include/rowl/platform/crash_handler.hpp` for the tokens
`malloc | printf | fopen | snprintf | new␣` and fails the suite on any hit.

## Snapshot decision

`RowlEngine_GetLast*` is called **only from normal control flow**, never from
the crash path. `main()` refreshes the snapshot at safe points via
`RowlCrash_RefreshSnapshot()` — currently after project-directory setup and
after story-graph load — copying result code, operation and target into
fixed-size static storage. The handler then echoes only that pre-captured
data plus the argv echo captured at install time.

Rationale: calling the C API from a signal handler would risk heap use,
locks and thread-locals mid-crash (undefined behaviour, possibly no log at
all). A bounded-stale snapshot plus an honest `note:` line is strictly more
reliable, and it is testable: the test plants sentinel values through the
public refresh API and asserts they land in the child's log.

## Log directory and file names

- Directory: `crash-logs/` under the player's working directory. `main()`
  tries `create_directories` first thing (before argument parsing); if that
  fails, install is skipped silently.
- Name: `crash-<pid>-<seq>.log` (`<seq>` starts at 0 per process and only
  advances on name collision). No timestamps — deterministic and sortable.
- Files are created with `O_CREAT | O_EXCL` (mode `0644`); a missing
  directory exhausts retries silently.

## Log format (`rowl-crash-log v1`)

```text
rowl-crash-log v1
reason: SIGSEGV
pid: 12345
argv: rowl_player --project demos/first_light
result-code: 0
result-operation: load_story_graph
result-target: Assets/json/full_story_graph.json
note: snapshot reflects the last safe-point refresh
```

- `result-code / result-operation / result-target`: last refreshed
  `RowlEngine_GetLastResultCode / Operation / Target`; empty when no engine
  existed or no refresh ran yet ("no-engine" case = empty fields, never a
  crash inside the logger). No handle value is ever written.
- `argv`: command line echoed at install time, truncated to 1023 bytes,
  newlines flattened to spaces.

## Platform matrix

| Platform | Behaviour |
|----------|-----------|
| Linux / macOS (POSIX) | Full: signal handlers + terminate hook + log files. |
| Windows | Fail-closed stub: `RowlCrash_Install` is a no-op returning `false`, all other calls are safe no-ops, no files are written. A native SEH/VEH equivalent is out of scope for this slice. |
| Mobile / cross (`ROWL_BUILD_PLAYER=OFF`) | Player (and thus the handler) is not built; engine library is unaffected. |

## Tests (`tests/test_crash_log.cpp`, runs inside `rowl_tests`)

1. `grep-gate` — forbidden-token scan of both crash_handler files.
2. Parent install/uninstall round-trip (flag set/cleared).
3. Forked child raises `SIGSEGV` → parent asserts death by `SIGSEGV`,
   exact file `crash-<pid>-0.log`, and all sentinel fields. Temp-dir
   isolated, 10 s `waitpid` timeout.
4. Forked child calls `std::terminate` → parent asserts exit 134 and a
   `reason: terminate` log.
5. Windows build: stub no-op assertions instead of 2–4.

## Non-goals / invariants

- No C ABI change: `c_api.h` untouched, no new capability bits; the handler
  is a C++-linkage helper exported with the existing `ROWL_API` macro.
- Zero diff in `engine.cpp`, `window.cpp`, `MainWindowViewModel`,
  `EngineHost`.
- The editor is untouched.
