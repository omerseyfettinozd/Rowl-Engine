using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 2 Dilim 3 — player state machine, auto-play timing and the
/// continuous skip driver.
/// </summary>
public sealed class EditorPlayerLoopSlice3Tests
{
    // ── State machine ────────────────────────────────────────────

    [Fact]
    public void Machine_TitleEntersPlaying()
    {
        var machine = new PlayerStateMachine();
        Assert.Equal(PlayerState.Title, machine.Current);

        var denied = machine.Request(PlayerIntent.OpenPause);
        Assert.False(denied.Allowed);
        Assert.NotNull(denied.DenyReason);
        Assert.Equal(PlayerState.Title, machine.Current);

        Assert.True(machine.Request(PlayerIntent.NewGame).Allowed);
        Assert.Equal(PlayerState.Playing, machine.Current);
    }

    [Fact]
    public void Machine_PauseMediatesOverlays()
    {
        var machine = new PlayerStateMachine();
        machine.Request(PlayerIntent.Continue);

        var direct = machine.Request(PlayerIntent.OpenSave);
        Assert.False(direct.Allowed);
        Assert.Contains("pause", direct.DenyReason, StringComparison.OrdinalIgnoreCase);

        Assert.True(machine.Request(PlayerIntent.OpenPause).Allowed);
        Assert.True(machine.Request(PlayerIntent.OpenSave).Allowed);
        Assert.Equal(PlayerState.Save, machine.Current);
        Assert.True(machine.Request(PlayerIntent.CloseOverlay).Allowed);
        Assert.Equal(PlayerState.Pause, machine.Current);
        Assert.True(machine.Request(PlayerIntent.Resume).Allowed);
        Assert.Equal(PlayerState.Playing, machine.Current);
    }

    [Fact]
    public void Machine_BacklogReturnsToInvoker()
    {
        var machine = new PlayerStateMachine();
        machine.Request(PlayerIntent.NewGame);
        machine.Request(PlayerIntent.OpenBacklog);
        Assert.Equal(PlayerState.Backlog, machine.Current);
        machine.Request(PlayerIntent.CloseOverlay);
        Assert.Equal(PlayerState.Playing, machine.Current);

        machine.Request(PlayerIntent.OpenPause);
        machine.Request(PlayerIntent.OpenBacklog);
        machine.Request(PlayerIntent.CloseOverlay);
        Assert.Equal(PlayerState.Pause, machine.Current);
    }

    [Fact]
    public void Machine_ExitFlows()
    {
        var machine = new PlayerStateMachine();
        machine.Request(PlayerIntent.NewGame);
        machine.Request(PlayerIntent.OpenPause);
        machine.Request(PlayerIntent.RequestExitToTitle);
        Assert.Equal(PlayerState.ConfirmExit, machine.Current);

        Assert.True(machine.Request(PlayerIntent.CancelExit).Allowed);
        Assert.Equal(PlayerState.Pause, machine.Current);

        machine.Request(PlayerIntent.RequestExitToApp);
        var confirm = machine.Request(PlayerIntent.ConfirmExit);
        Assert.True(confirm.Allowed);
        Assert.Equal(PlayerState.Title, machine.Current);
        Assert.True(machine.ExitConfirmed);
    }

    [Fact]
    public void Machine_QuitToTitleClearsOverlays()
    {
        var machine = new PlayerStateMachine();
        machine.Request(PlayerIntent.NewGame);
        machine.Request(PlayerIntent.OpenPause);
        machine.Request(PlayerIntent.OpenPreferences);
        Assert.True(machine.Request(PlayerIntent.QuitToTitle).Allowed);
        Assert.Equal(PlayerState.Title, machine.Current);
        // Stale overlay returns are gone: nothing to close on title.
        Assert.False(machine.Request(PlayerIntent.CloseOverlay).Allowed);
    }

    [Fact]
    public void Machine_TitleOpensLoadPreferencesAndExit()
    {
        var machine = new PlayerStateMachine();
        Assert.True(machine.Request(PlayerIntent.OpenLoad).Allowed);
        Assert.Equal(PlayerState.Load, machine.Current);
        Assert.True(machine.Request(PlayerIntent.CloseOverlay).Allowed);
        Assert.Equal(PlayerState.Title, machine.Current);

        Assert.True(machine.Request(PlayerIntent.OpenPreferences).Allowed);
        Assert.Equal(PlayerState.Preferences, machine.Current);
        Assert.True(machine.Request(PlayerIntent.CloseOverlay).Allowed);

        Assert.True(machine.Request(PlayerIntent.RequestExitToApp).Allowed);
        Assert.Equal(PlayerState.ConfirmExit, machine.Current);
        Assert.True(machine.Request(PlayerIntent.ConfirmExit).Allowed);
        Assert.True(machine.ExitConfirmed);
    }

    [Fact]
    public void Machine_ChoicesPendingBlocksNoTransition()
    {
        var machine = new PlayerStateMachine();
        machine.Request(PlayerIntent.NewGame);
        machine.SetChoicesPending(true);
        Assert.True(machine.Request(PlayerIntent.OpenPause).Allowed);
        Assert.True(machine.Request(PlayerIntent.OpenSave).Allowed);
        machine.Request(PlayerIntent.CloseOverlay);
        Assert.Equal(PlayerState.Pause, machine.Current);
        Assert.True(machine.ChoicesPending);
    }

    // ── Auto-play driver ─────────────────────────────────────────

    [Fact]
    public void AutoPlay_FiresAfterReadingTime()
    {
        var profile = new PlayerProfile { AutoAdvanceDelay = 2.0f, TextSpeedMultiplier = 1.0f };
        var driver = new AutoPlayDriver();
        Assert.False(driver.Tick(10.0f, lineComplete: true, hasChoices: false));

        driver.SetEnabled(true);
        driver.BeginLine(100, 0.0f, profile);
        float expected = 2.0f + (100 * AutoPlayDriver.PerCharSeconds);
        Assert.Equal(expected, driver.CurrentWaitSeconds, precision: 4);

        Assert.False(driver.Tick(expected - 0.1f, lineComplete: true, hasChoices: false));
        Assert.True(driver.Tick(0.2f, lineComplete: true, hasChoices: false));
        // Firing resets the clock for the next line.
        Assert.Equal(0.0f, driver.ElapsedSeconds);
    }

    [Fact]
    public void AutoPlay_VoiceDurationWinsWhenLonger()
    {
        var profile = new PlayerProfile { AutoAdvanceDelay = 1.0f };
        var driver = new AutoPlayDriver();
        driver.SetEnabled(true);
        driver.BeginLine(10, 12.0f, profile);
        Assert.Equal(12.0f, driver.CurrentWaitSeconds, precision: 4);
        Assert.False(driver.Tick(11.9f, lineComplete: true, hasChoices: false));
        Assert.True(driver.Tick(0.2f, lineComplete: true, hasChoices: false));
    }

    [Fact]
    public void AutoPlay_HaltsOnChoicesAndIncompleteLines()
    {
        var profile = new PlayerProfile();
        var driver = new AutoPlayDriver();
        driver.SetEnabled(true);
        driver.BeginLine(50, 0.0f, profile);

        Assert.False(driver.Tick(100.0f, lineComplete: false, hasChoices: false));
        Assert.False(driver.Tick(100.0f, lineComplete: true, hasChoices: true));
        Assert.Equal(0.0f, driver.ElapsedSeconds);

        driver.NotifyManualAdvance();
        driver.SetEnabled(false);
        Assert.False(driver.Tick(100.0f, lineComplete: true, hasChoices: false));
    }

    [Fact]
    public void AutoPlay_LongerTextAndSlowerSpeedWaitLonger()
    {
        var fast = new PlayerProfile { AutoAdvanceDelay = 0.0f, TextSpeedMultiplier = 2.0f };
        var slow = new PlayerProfile { AutoAdvanceDelay = 0.0f, TextSpeedMultiplier = 0.5f };
        var driver = new AutoPlayDriver();
        driver.SetEnabled(true);
        driver.BeginLine(100, 0.0f, fast);
        float fastWait = driver.CurrentWaitSeconds;
        driver.BeginLine(100, 0.0f, slow);
        Assert.True(slow.TextSpeedMultiplier < fast.TextSpeedMultiplier);
        Assert.True(driver.CurrentWaitSeconds > fastWait);
        driver.BeginLine(200, 0.0f, fast);
        Assert.True(driver.CurrentWaitSeconds > fastWait);
    }

    // ── Continuous skip driver ───────────────────────────────────

    [Fact]
    public void SkipDriver_OffShortCircuitsWithoutAdvancing()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(
                new PlayerProfile { SkipMode = PlayerSkipMode.Off }, directory);
            var driver = new SkipDriver();
            int advances = 0;
            Assert.False(driver.Tick(loop, () => new[] { "anything" }, () => false, _ => advances++, out string? error));
            Assert.Null(error);
            Assert.Equal(0, advances);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void SkipDriver_StepsWhileReadAndStopsOnUnread()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(
                new PlayerProfile { SkipMode = PlayerSkipMode.ReadOnly }, directory);
            var driver = new SkipDriver();
            const string first = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
            const string second = "11111111-2222-3333-4444-555555555555";
            loop.NotePresented(new[] { first });
            var presented = new List<string?> { first };
            int advances = 0;

            Assert.True(driver.Tick(loop, () => presented, () => false, _ => advances++, out _));
            Assert.Equal(1, advances);

            presented = new List<string?> { second };
            Assert.False(driver.Tick(loop, () => presented, () => false, _ => advances++, out _));
            Assert.Equal(1, advances);

            presented = new List<string?> { first };
            Assert.False(driver.Tick(loop, () => presented, () => true, _ => advances++, out _));
            Assert.Equal(1, advances);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    private static string NewTempDirectory()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlSlice3_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return directory;
    }
}
