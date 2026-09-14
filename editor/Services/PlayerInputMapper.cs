using System;
using System.Collections.Generic;
using Avalonia.Input;

namespace RowlEngine.Editor.Services;

/// <summary>Player-shell semantic commands (device-independent).</summary>
public enum PlayerInputCommand
{
    Advance,
    CancelBack,
    TogglePause,
    ToggleAuto,
    ToggleSkip,
    CycleTextScale,
    ToggleHighContrast,
    OpenBacklog,
    QuickSave,
    QuickLoad,
}

/// <summary>Pointer buttons the player shell distinguishes.</summary>
public enum PlayerPointerButton
{
    Left,
    Right,
    Middle,
}

/// <summary>
/// Gamepad buttons in positional (Xbox-style) naming so hosts can map any
/// controller layout onto them without the mapper knowing SDL/Avalonia ids.
/// </summary>
public enum PlayerGamepadButton
{
    South,
    East,
    West,
    North,
    Start,
    Back,
    LeftShoulder,
    RightShoulder,
}

/// <summary>Touch gestures (mirrors the native swipe-wins classification).</summary>
public enum PlayerTouchGesture
{
    Tap,
    SwipeForward,
    SwipeBack,
}

/// <summary>
/// Mutable per-host binding table pre-filled with the default map.
/// Hosts mutate an instance for remapping (future profile keymaps build on
/// this shape); <see cref="PlayerInputMapper"/> never mutates it.
/// </summary>
public sealed class PlayerInputBindings
{
    public Dictionary<Key, PlayerInputCommand> Keys { get; } = new();
    public Dictionary<PlayerPointerButton, PlayerInputCommand> Pointer { get; } = new();
    public Dictionary<PlayerGamepadButton, PlayerInputCommand> Gamepad { get; } = new();
    public Dictionary<PlayerTouchGesture, PlayerInputCommand> Touch { get; } = new();

    public PlayerInputBindings()
    {
        Reset();
    }

    /// <summary>Restores the default map (see PlayerInputMapper).</summary>
    public void Reset()
    {
        Keys.Clear();
        Keys[Key.Space] = PlayerInputCommand.Advance;
        Keys[Key.Enter] = PlayerInputCommand.Advance;
        Keys[Key.Back] = PlayerInputCommand.CancelBack;
        Keys[Key.Escape] = PlayerInputCommand.TogglePause;
        Keys[Key.P] = PlayerInputCommand.TogglePause;
        Keys[Key.A] = PlayerInputCommand.ToggleAuto;
        Keys[Key.S] = PlayerInputCommand.ToggleSkip;
        Keys[Key.T] = PlayerInputCommand.CycleTextScale;
        Keys[Key.H] = PlayerInputCommand.ToggleHighContrast;
        Keys[Key.B] = PlayerInputCommand.OpenBacklog;
        Keys[Key.F5] = PlayerInputCommand.QuickSave;
        Keys[Key.F9] = PlayerInputCommand.QuickLoad;

        Pointer.Clear();
        Pointer[PlayerPointerButton.Left] = PlayerInputCommand.Advance;
        Pointer[PlayerPointerButton.Right] = PlayerInputCommand.CancelBack;

        Gamepad.Clear();
        Gamepad[PlayerGamepadButton.South] = PlayerInputCommand.Advance;
        Gamepad[PlayerGamepadButton.East] = PlayerInputCommand.CancelBack;
        Gamepad[PlayerGamepadButton.Start] = PlayerInputCommand.TogglePause;
        Gamepad[PlayerGamepadButton.West] = PlayerInputCommand.ToggleAuto;
        Gamepad[PlayerGamepadButton.North] = PlayerInputCommand.ToggleSkip;
        Gamepad[PlayerGamepadButton.LeftShoulder] = PlayerInputCommand.OpenBacklog;
        Gamepad[PlayerGamepadButton.RightShoulder] = PlayerInputCommand.QuickSave;
        Gamepad[PlayerGamepadButton.Back] = PlayerInputCommand.QuickLoad;

        Touch.Clear();
        Touch[PlayerTouchGesture.Tap] = PlayerInputCommand.Advance;
        Touch[PlayerTouchGesture.SwipeForward] = PlayerInputCommand.Advance;
        Touch[PlayerTouchGesture.SwipeBack] = PlayerInputCommand.OpenBacklog;
    }

    /// <summary>Fresh table with defaults (mutating it affects nothing else).</summary>
    public static PlayerInputBindings Default() => new();
}

/// <summary>
/// Faz 2 Dilim 4 — device-independent semantic input mapping for the player
/// shell. Translates keyboard, pointer, gamepad and touch inputs into
/// <see cref="PlayerInputCommand"/> values; unmapped inputs yield null so
/// hosts can fall through to editor chords or ignore them.
/// Key chords with Ctrl/Shift/Alt are reserved for the editor and never
/// map (a null return), keeping player input and editor shortcuts disjoint.
/// Pure logic — the full matrix is unit-tested.
/// </summary>
public static class PlayerInputMapper
{
    public static PlayerInputCommand? MapKey(
        Key key, bool ctrl = false, bool shift = false, bool alt = false,
        PlayerInputBindings? bindings = null)
    {
        if (ctrl || shift || alt)
            return null;
        var table = bindings ?? PlayerInputBindings.Default();
        return table.Keys.TryGetValue(key, out var command) ? command : null;
    }

    public static PlayerInputCommand? MapPointer(
        PlayerPointerButton button, PlayerInputBindings? bindings = null)
    {
        var table = bindings ?? PlayerInputBindings.Default();
        return table.Pointer.TryGetValue(button, out var command) ? command : null;
    }

    public static PlayerInputCommand? MapGamepad(
        PlayerGamepadButton button, PlayerInputBindings? bindings = null)
    {
        var table = bindings ?? PlayerInputBindings.Default();
        return table.Gamepad.TryGetValue(button, out var command) ? command : null;
    }

    public static PlayerInputCommand? MapTouch(
        PlayerTouchGesture gesture, PlayerInputBindings? bindings = null)
    {
        var table = bindings ?? PlayerInputBindings.Default();
        return table.Touch.TryGetValue(gesture, out var command) ? command : null;
    }
}
