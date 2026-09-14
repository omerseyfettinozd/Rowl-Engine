using Avalonia.Input;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 2 Dilim 4 — semantic input mapping matrix.</summary>
public sealed class EditorPlayerLoopSlice4Tests
{
    [Fact]
    public void Mapper_KeyboardCoversAllCommands()
    {
        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapKey(Key.Space));
        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapKey(Key.Enter));
        Assert.Equal(PlayerInputCommand.CancelBack, PlayerInputMapper.MapKey(Key.Back));
        Assert.Equal(PlayerInputCommand.TogglePause, PlayerInputMapper.MapKey(Key.Escape));
        Assert.Equal(PlayerInputCommand.TogglePause, PlayerInputMapper.MapKey(Key.P));
        Assert.Equal(PlayerInputCommand.ToggleAuto, PlayerInputMapper.MapKey(Key.A));
        Assert.Equal(PlayerInputCommand.ToggleSkip, PlayerInputMapper.MapKey(Key.S));
        Assert.Equal(PlayerInputCommand.OpenBacklog, PlayerInputMapper.MapKey(Key.B));
        Assert.Equal(PlayerInputCommand.QuickSave, PlayerInputMapper.MapKey(Key.F5));
        Assert.Equal(PlayerInputCommand.QuickLoad, PlayerInputMapper.MapKey(Key.F9));
    }

    [Fact]
    public void Mapper_PointerGamepadTouchCoverCommands()
    {
        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapPointer(PlayerPointerButton.Left));
        Assert.Equal(PlayerInputCommand.CancelBack, PlayerInputMapper.MapPointer(PlayerPointerButton.Right));
        Assert.Null(PlayerInputMapper.MapPointer(PlayerPointerButton.Middle));

        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapGamepad(PlayerGamepadButton.South));
        Assert.Equal(PlayerInputCommand.CancelBack, PlayerInputMapper.MapGamepad(PlayerGamepadButton.East));
        Assert.Equal(PlayerInputCommand.TogglePause, PlayerInputMapper.MapGamepad(PlayerGamepadButton.Start));
        Assert.Equal(PlayerInputCommand.ToggleAuto, PlayerInputMapper.MapGamepad(PlayerGamepadButton.West));
        Assert.Equal(PlayerInputCommand.ToggleSkip, PlayerInputMapper.MapGamepad(PlayerGamepadButton.North));
        Assert.Equal(PlayerInputCommand.OpenBacklog, PlayerInputMapper.MapGamepad(PlayerGamepadButton.LeftShoulder));
        Assert.Equal(PlayerInputCommand.QuickSave, PlayerInputMapper.MapGamepad(PlayerGamepadButton.RightShoulder));
        Assert.Equal(PlayerInputCommand.QuickLoad, PlayerInputMapper.MapGamepad(PlayerGamepadButton.Back));

        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapTouch(PlayerTouchGesture.Tap));
        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapTouch(PlayerTouchGesture.SwipeForward));
        Assert.Equal(PlayerInputCommand.OpenBacklog, PlayerInputMapper.MapTouch(PlayerTouchGesture.SwipeBack));
    }

    [Fact]
    public void Mapper_UnknownInputsYieldNull()
    {
        Assert.Null(PlayerInputMapper.MapKey(Key.F1));
        Assert.Null(PlayerInputMapper.MapKey(Key.Q));
    }

    [Fact]
    public void Mapper_EditorChordsNeverMap()
    {
        Assert.Null(PlayerInputMapper.MapKey(Key.S, ctrl: true));
        Assert.Null(PlayerInputMapper.MapKey(Key.Space, shift: true));
        Assert.Null(PlayerInputMapper.MapKey(Key.F5, ctrl: true));
        Assert.Null(PlayerInputMapper.MapKey(Key.A, alt: true));
    }

    [Fact]
    public void Mapper_CustomBindingsOverrideDefaults()
    {
        var bindings = PlayerInputBindings.Default();
        bindings.Keys[Key.Space] = PlayerInputCommand.TogglePause;
        bindings.Pointer.Remove(PlayerPointerButton.Right);

        Assert.Equal(PlayerInputCommand.TogglePause, PlayerInputMapper.MapKey(Key.Space, bindings: bindings));
        Assert.Null(PlayerInputMapper.MapPointer(PlayerPointerButton.Right, bindings));
        // Untouched defaults still apply on the same table.
        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapKey(Key.Enter, bindings: bindings));

        var fresh = PlayerInputBindings.Default();
        Assert.Equal(PlayerInputCommand.Advance, PlayerInputMapper.MapKey(Key.Space, bindings: fresh));
    }
}
