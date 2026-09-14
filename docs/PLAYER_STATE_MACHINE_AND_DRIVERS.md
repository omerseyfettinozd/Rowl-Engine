# Player State Machine, Auto-Play & Skip Drivers (Faz 2 Dilim 3)

Sources of truth are the implementations named below; when this document
and code disagree, code wins and this document must be patched.

## 1. PlayerStateMachine (`editor/Services/PlayerStateMachine.cs`)

Discrete states: `Title`, `Playing`, `Pause`, `Backlog`, `Save`, `Load`,
`Preferences`, `ConfirmExit`. Title and Playing are base states; the rest
are overlays that return through a bounded stack (depth 8).

Transition rules (`Request(intent)` → allowed/denied + reason):

- Title → Playing via NewGame/Continue only.
- Playing → Pause / Backlog / ConfirmExit. Direct Playing → Save/Load/
  Preferences is denied ("needs the pause menu first").
- Pause → resume Playing, open any overlay, ConfirmExit, or QuitToTitle.
- Any overlay → close back to its invoker, ConfirmExit, or QuitToTitle
  (QuitToTitle clears the whole stack).
- ConfirmExit → confirm (Title; sets `ExitConfirmed` when the target is
  the application, for the host to terminate) or cancel (back to invoker).
- A pending choice (`SetChoicesPending`) blocks **no** transition:
  pausing, reviewing the backlog or saving mid-choice is legal. It halts
  the auto-play and skip drivers instead, so the choice itself can never
  be fast-forwarded. This is the deliberate reading of the
  "pause-during-choice" rule.

## 2. AutoPlayDriver (`editor/Services/AutoPlayDriver.cs`)

Per-line wait: `AutoAdvanceDelay + chars × 0.04s / TextSpeedMultiplier`,
clamped to [0.8s, 30s], then raised to at least the voice duration.
Constants are starting points pending playtest calibration.

- `BeginLine(chars, voiceSeconds, profile)` arms the clock; `Tick(dt,
  lineComplete, hasChoices)` returns true once when the wait elapses and
  resets for the next line.
- The clock runs only for a complete line with no pending choice;
  otherwise it freezes without accumulating.
- `NotifyManualAdvance` restarts the clock (manual input advances now;
  auto resumes timing the next line). `SetEnabled(false)` stops it.
- This is the player-level auto mode. The native per-line `auto_advance`
  component flag (engine Step) is a separate, older mechanism and is
  untouched by this slice.

## 3. SkipDriver (`editor/Services/SkipDriver.cs`)

One `Tick` = at most one `PlayerLoopService.TrySkipStep` (Dilim 2 gate:
Off never steps; ReadOnly steps only over recorded ids; choices stop).
A per-frame Playing loop drives it; the loop itself belongs to the
Playing-state host shell (later dilim), same boundary as Dilim 2.

## 4. Non-goals of this slice

- No UI bindings (Title/Pause/overlay screens bind later).
- No native or C ABI change (drivers are managed; the engine contract
  from Dilimler 1–2 is reused as-is).
- No continuous driver wiring into `EngineHost.OnTick` (editor Play Mode
  must not auto-advance previews or pollute the player profile).

## 5. Tests

xUnit `EditorPlayerLoopSlice3Tests` (12): title gating, pause-mediated
overlays, backlog return targets, exit confirm/cancel/app/title flows,
stack clearing, choice-pending non-blocking, auto fire time, voice
dominance, choice/incomplete halt, manual reset, text-length/speed
scaling, skip off short-circuit, read/unread/choice skip matrix.
