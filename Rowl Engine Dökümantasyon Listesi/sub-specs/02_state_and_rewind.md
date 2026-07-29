# 🔬 SUB-SPEC 02: STATE MANAGEMENT, STRUCTURAL SHARING & REWIND ENGINE

> **Subsystem Target:** Persistent Immutable Game State, Time-Travel Rewind, Backlog History, and Save/Load Serialization.

---

## 1. ARCHITECTURAL OVERVIEW

Visual Novels require exact, reproducible game states for choices, variables, character positions, audio states, and text backlog. Rowl Engine implements a **Persistent Immutable State Architecture** powered by **Structural Sharing** (inspired by Clojure / Immer HAMT - Hash Array Mapped Tries).

```
State @ Step 10:  [ Root V1 ] ---> { Node: #101, Vars: { Gold: 50, Key: true }, Sprites: [A, B] }
                                     |
State @ Step 11:  [ Root V2 ] -------+---> Shares Unchanged Vars & Sprites!
                                     +---> New Node Pointer: #102
```

### Key Benefits:
- **Zero-Copy History Snapshots:** Taking a state snapshot at every dialogue step costs nearly zero extra RAM.
- **Instant Rewind (Time Travel):** Players can rewind dialogue line-by-line or step back through choices instantly by swapping state pointers.
- **Crash Prevention:** Game state is immutable; execution functions return new state roots rather than mutating memory in-place.

---

## 2. GAME STATE STRUCTURE (`GameState`)

A single immutable state snapshot consists of four core data domains:

```cpp
struct GameState {
    uint64_t step_id;                    // Monotonically increasing step counter
    uint64_t active_node_id;             // Current active scene/dialogue node
    uint32_t text_typewriter_index;      // Typewriter progress within active node
    
    // Persistent Immutable Map for Game Variables (Flags, Numbers, Strings)
    PersistentMap<std::string, Value> variables;
    
    // Active Visual Layers
    PersistentVector<CharacterSlot> active_characters;
    BackgroundState active_background;
    
    // Active Audio Channels
    AudioChannelState bgm_channel;
    AudioChannelState voice_channel;
    
    // Choice & Branching History
    PersistentVector<ChoiceRecord> choice_history;
};
```

---

## 3. STRUCTURAL SHARING & HAMT MECHANICS

- **Structural Sharing (Persistent Data Structures):** When moving from Step N to Step N+1 (e.g., advancing a dialogue line):
  1. The new `GameState` object is created.
  2. Unmodified components (e.g., `active_characters`, `variables`) re-use internal tree node pointers from Step N via `std::shared_ptr` or reference counting.
  3. Only modified nodes in the radix tree (HAMT) are re-allocated.
- **Memory Footprint:** Storing 1,000 steps of rewind history in RAM consumes less than **2 MB** of memory, aligning perfectly with the *"Tost Makinesi"* zero-waste principle.

---

## 4. TIME-TRAVEL REWIND & BACKLOG SYSTEM

### Rewind Pipeline:
```
[ User Presses Rewind / Mouse Wheel Up ]
         │
         ▼
[ Decrement Step Pointer (Step N -> Step N-1) ]
         │
         ▼
[ Swap Active GameState Root Pointer ]
         │
         ▼
[ Re-evaluate Audio & Visual Layers (Diff & Apply) ]
```

- **Visual / Audio Diffing:** When swapping to a past state root, the engine calculates a diff between `CurrentState` and `TargetState`:
  - If BGM track changed between steps, cross-fade to previous track.
  - If character portrait changed expression, trigger smooth transition.
  - If variable changed, UI updates flags instantly.

### Dialogue Backlog (History Log):
- Every step appends a lightweight entry to the `BacklogBuffer`:
  - `speaker_name`
  - `dialogue_text`
  - `voice_audio_id`
  - `snapshot_step_id`
- Clicking any line in the Backlog UI allows the player to jump directly back to that exact snapshot in time.

---

## 5. SAVE / LOAD SERIALIZATION (`.rowlsave`)

### Save File Structure (`save.json` or `.rowlsave` Binary):
```json
{
  "header": {
    "magic": "ROWLSAVE",
    "version": 1,
    "timestamp": 1721900000,
    "playtime_seconds": 3420,
    "game_version": "1.0.2"
  },
  "snapshot": {
    "step_id": 412,
    "active_node_id": 8812,
    "variables": {
      "has_key": true,
      "affection_character_a": 85
    },
    "history_hash": "a8f9b2..."
  },
  "checksum": "sha256_checksum_value"
}
```

- **Security & Integrity:** Save files are protected with SHA-256 checksums to detect file tampering.
- **Fast Load:** Loading a save file initializes the root `GameState` in less than **1 millisecond**.
