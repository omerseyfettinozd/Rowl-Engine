# 🔬 SUB-SPEC 04: SANDBOXED LUA SCRIPTING & EXTENSIBILITY ENGINE

> **Subsystem Target:** Embedded Sandboxed Lua 5.4 Runtime, C++ API Binding Layer, Security Boundaries, Custom Mini-Game Hooks, and Error Isolation.

---

## 1. ARCHITECTURAL OVERVIEW

To enable advanced creators and community modders to build custom gameplay mechanics (mini-games, puzzles, complex inventory systems, dynamic affection formulas) without sacrificing mobile compatibility or engine stability, Rowl Engine embeds a lightweight **Lua 5.4** scripting environment.

```
+-------------------------------------------------------------------+
|                         C++ ENGINE CORE                           |
|                                                                   |
|   +-----------------------------------------------------------+   |
|   |                 SANDBOXED LUA ENVIRONMENT                 |   |
|   |                                                           |   |
|   |   - Restricted Standard Libs (math, string, table)        |   |
|   |   - Blacklisted Dangerous APIs (io, os, debug)            |   |
|   |   - Engine API Namespace (`rowl.*`)                       |   |
|   |   - Custom Mini-Game & Node Hooks                         |   |
|   +-----------------------------------------------------------+   |
|                                                                   |
|   - Protected Execution (`lua_pcall` / Protected Mode)            |
|   - Crash Isolation (Script error != Engine Crash)                |
+-------------------------------------------------------------------+
```

---

## 2. SANDBOX SECURITY & RESOURCE BOUNDARIES

To ensure community mods cannot compromise system security, access unauthorized files, or crash the application, the Lua state is strictly sandboxed.

### Library Whitelist & Blacklist:
- **WHITELISTED (Safe):** `math`, `string`, `table`, `utf8`, `coroutine`.
- **BLACKLISTED (Blocked):** 
  - `io.*` (Direct disk access blocked; file reads must pass through VFS).
  - `os.execute`, `os.remove`, `os.rename` (OS shell command execution blocked).
  - `debug.*` (C stack inspection blocked).
  - `package.*` / `require` (Dynamic library loading restricted to VFS relative paths).

### Instruction Count Limit (Infinite Loop Protection):
- A debug count hook (`lua_sethook`) monitors script execution steps.
- If a script exceeds **10,000,000 instructions** in a single frame tick (indicating an infinite loop), execution is forcefully interrupted, an error log is raised, and the engine skips the script gracefully.

---

## 3. C++ ENGINE API BINDINGS (`rowl.*` NAMESPACE)

Lua scripts interact with the C++ engine core via clean, type-safe bindings (using Sol2 / native C-API):

```lua
-- Example: Custom Mini-Game or Choice Event Script in Lua

-- 1. Variable Operations
local gold = rowl.var.get_number("player_gold")
if gold >= 100 then
    rowl.var.set_number("player_gold", gold - 100)
    rowl.var.set_bool("has_sword", true)
    
    -- 2. Audio Control
    rowl.audio.play_sfx("coins_sound.wav")
    
    -- 3. Visual Scene Effects
    rowl.scene.screen_shake(0.5, 10.0) -- duration, intensity
    rowl.scene.show_toast("Item Purchased: Legendary Sword!")
    
    -- 4. Branching Jump
    rowl.flow.jump_to_node(1045)
else
    rowl.ui.show_dialogue("Vendor", "You don't have enough gold, stranger!")
end
```

---

## 4. MINI-GAME HOOKS & CUSTOM NODE EXECUTION

For complex custom gameplay (e.g., a lock-picking mini-game or card battle):

1. **Custom Lua Node:** The Creator assigns a `.lua` script file to an Action/Logic Node in the Editor.
2. **Lifecycle Callbacks:** The Lua script implements optional lifecycle functions:
   ```lua
   function on_enter(params)
       -- Called when the node is activated
   end
   
   function on_update(dt)
       -- Called every frame tick (for mini-game physics/drawing)
   end
   
   function on_exit()
       -- Called when transitioning out of the node
   end
   ```
3. **Rendering Hooks:** Lua can call primitive draw functions (`rowl.draw.sprite()`, `rowl.draw.rect()`, `rowl.draw.text()`) to render custom UI elements over the game canvas.

---

## 5. CRASH ISOLATION & ERROR OVERLAY

If a Lua script contains syntax errors or throws a runtime exception:

```
[ Lua Runtime Exception Triggered ]
               │
               ▼
   [ Caught by C++ `lua_pcall` ]
               │
               ▼
[ Non-Fatal Engine Recovery ]
  ├── 1. Print red error log to Editor IPC / Console Log
  ├── 2. Display non-intrusive Dev Toast: "Lua Error in Node #102: attempt to index nil"
  └── 3. Fallback: Skip script execution and advance safely to the next dialogue step.
```

**Result:** The game **never crashes** to desktop or closes unexpectedly on mobile due to a script error.
