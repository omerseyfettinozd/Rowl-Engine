# 🚀 PHASE 4 EXECUTION PLAN: SANDBOXED LUA & IMMUTABLE REWIND STATE

> **Phase Objective:** Embed a crash-proof Sandboxed Lua 5.4 runtime into the C++ engine, implement immutable game state with structural sharing for infinite rewind/backlog, and deploy the Dual-Path Audio Engine with DSP Filters.

---

## 🏗️ 1. DIRECTORY STRUCTURE (SCRIPTING, STATE & AUDIO)

```text
Node-Oyun-Motoru/
├── engine/
│   ├── include/rowl/
│   │   ├── scripting/
│   │   │   ├── lua_sandbox.hpp       # Lua state setup & security policy
│   │   │   └── script_manager.hpp    # Hot-reload & lifecycle hooks
│   │   ├── state/
│   │   │   ├── game_state.hpp        # Immutable GameState struct
│   │   │   └── persistent_map.hpp    # Structural-sharing hash map
│   │   └── audio/
│   │       ├── audio_engine.hpp      # Dual-path streaming/buffer system
│   │       └── dsp_filters.hpp       # Reverb, Low-pass, Telephone effects
│   └── src/
│       ├── scripting/
│       │   ├── lua_sandbox.cpp
│       │   └── script_manager.cpp
│       ├── state/
│       │   ├── game_state.cpp
│       │   └── persistent_map.cpp
│       └── audio/
│           ├── audio_engine.cpp
│           └── dsp_filters.cpp
├── mods/
│   └── example_minigame/
│       └── minigame.lua              # Community script example
└── data/
    └── scripts/
        └── test_script.lua
```

---

## 💻 2. SANDBOXED LUA 5.4 INTEGRATION BLUEPRINT

The engine embeds Lua 5.4 as a strict sandbox to allow modding without risking engine stability or mobile compatibility.

### A. Lua Sandbox Security Setup (`engine/src/scripting/lua_sandbox.cpp`)
```cpp
#include <lua.hpp>
#include <sol/sol.hpp>

namespace Rowl::Scripting {

    void initialize_lua_sandbox(sol::state& lua_state) {
        // 1. Load safe standard libraries only
        lua_state.open_libraries(
            sol::base::lua_version,
            sol::lib::base,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::utf8
        );

        // 2. Explicitly disable dangerous libraries
        lua_state["io"] = sol::lua_nil;
        lua_state["os"] = sol::lua_nil;
        lua_state["debug"] = sol::lua_nil;
        lua_state["package"] = sol::lua_nil;

        // 3. Set instruction count limit to prevent infinite loops
        lua_sandbox_set_instruction_limit(lua_state, 10000000);
    }

    // Expose safe engine APIs to Lua scripts
    void bind_engine_api(sol::state& lua_state) {
        lua_state.new_usertype<GameState>("GameState");
        lua_state.set_function("rowl_var_get", &GameState::get_variable);
        lua_state.set_function("rowl_play_sfx", &AudioEngine::play_sfx);
    }
}
```

---

## 🧠 3. IMMUTABLE STATE & STRUCTURAL SHARING BLUEPRINT

Using Persistent Data Structures (HAMT-style) for zero-cost rewind snapshots.

### A. Immutable GameState Structure (`engine/include/rowl/state/game_state.hpp`)
```cpp
#pragma once
#include <memory>
#include <unordered_map>
#include <string>

namespace Rowl::State {

    struct GameState {
        uint64_t step_id;
        uint64_t active_node_id;
        std::shared_ptr<const GameState> previous_state; // Immutable pointer chain

        // Persistent map for variables (structural sharing)
        struct VariableMap {
            std::unordered_map<std::string, std::string> data;
        };
        std::shared_ptr<const VariableMap> variables;

        // Factory method to create new state with updated variable
        static GameState create_next_state(const GameState& current, 
                                          const std::string& key, 
                                          const std::string& value);
    };
}
```

---

## 🔊 4. DUAL-PATH AUDIO ENGINE WITH DSP FILTERS

Streaming for large files, memory buffering for short SFX, with real-time DSP effects.

### A. Audio Engine Core (`engine/include/rowl/audio/audio_engine.hpp`)
```cpp
#pragma once
#include <string>
#include <unordered_map>
#include <memory>

namespace Rowl::Audio {

    enum class AudioType {
        STREAMING_BGM,
        MEMORY_SFX,
        VOICE_OVER
    };

    enum class DSPFilterType {
        NORMAL,
        CAVE_REVERB,
        TELEPHONE,
        UNDERWATER_LOWPASS
    };

    class AudioEngine {
    public:
        void play_audio(const std::string& asset_path, AudioType type, DSPFilterType filter = DSPFilterType::NORMAL);
        void set_bgm_volume(float volume);
        void apply_dsp_filter(DSPFilterType filter);
        void stop_all();
    private:
        // Streaming sources for BGM/Voice
        std::unordered_map<std::string, std::shared_ptr<class StreamingSource>> m_streaming_sources;
        // Memory buffers for SFX/UI clicks
        std::unordered_map<std::string, std::shared_ptr<class BufferSource>> m_buffer_sources;
    };
}
```

---

## ✅ PHASE 4 ACCEPTANCE CRITERIA
- [ ] Lua sandbox runs a test script that modifies a game variable without crashing the engine.
- [ ] Infinite rewind (1000+ steps) consumes less than 5MB RAM due to structural sharing.
- [ ] BGM streams from disk with zero initial load delay.
- [ ] SFX plays instantly with zero latency.
- [ ] Applying a Telephone DSP filter makes voice audio sound filtered in real-time.
- [ ] Script error (e.g., nil access) is caught gracefully and logged without engine crash.
