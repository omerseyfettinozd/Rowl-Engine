using System;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>Discrete Faz 2 player states (overlay-capable).</summary>
public enum PlayerState
{
    Title,
    Playing,
    Pause,
    Backlog,
    Save,
    Load,
    Preferences,
    ConfirmExit,
}

/// <summary>Player intents the state machine resolves into transitions.</summary>
public enum PlayerIntent
{
    NewGame,
    Continue,
    OpenPause,
    Resume,
    OpenBacklog,
    OpenSave,
    OpenLoad,
    OpenPreferences,
    RequestExitToTitle,
    RequestExitToApp,
    ConfirmExit,
    CancelExit,
    QuitToTitle,
    CloseOverlay,
}

/// <summary>Where a confirmed exit leads.</summary>
public enum PlayerExitTarget
{
    None,
    Title,
    Application,
}

/// <summary>Outcome of <see cref="PlayerStateMachine.Request"/>.</summary>
public sealed record PlayerTransition(
    bool Allowed,
    PlayerState From,
    PlayerState To,
    string? DenyReason);

/// <summary>
/// Faz 2 Dilim 3 — discrete player state machine for the visual-novel loop.
/// <list type="bullet">
/// <item>Base states are <see cref="PlayerState.Title"/> and
/// <see cref="PlayerState.Playing"/>; every other state is an overlay that
/// remembers where to return through a bounded stack.</item>
/// <item>Save/Load/Preferences/Backlog open from Pause (or from Playing for
/// Backlog/exit). Direct Playing → Save/Load/Preferences is denied with a
/// reason: the pause menu mediates them.</item>
/// <item>A pending choice never blocks a transition (pausing, reviewing the
/// backlog or saving mid-choice is legal); it halts the auto-play and skip
/// drivers instead. <see cref="SetChoicesPending"/> is the single source of
/// that flag.</item>
/// <item>Confirming an exit to the application sets
/// <see cref="ExitConfirmed"/>; the host performs the termination.</item>
/// </list>
/// Pure logic, no engine or UI dependency — the full matrix is unit-tested.
/// </summary>
public sealed class PlayerStateMachine
{
    private const int MaxOverlayDepth = 8;

    private readonly Stack<PlayerState> _returnStack = new();

    public PlayerState Current { get; private set; } = PlayerState.Title;

    /// <summary>Target of the pending <see cref="PlayerState.ConfirmExit"/>.</summary>
    public PlayerExitTarget ExitTarget { get; private set; } = PlayerExitTarget.None;

    /// <summary>True once the player confirmed quitting to the application.</summary>
    public bool ExitConfirmed { get; private set; }

    /// <summary>True while a choice awaits manual input (halts drivers, blocks no transition).</summary>
    public bool ChoicesPending { get; private set; }

    public void SetChoicesPending(bool pending) => ChoicesPending = pending;

    public PlayerTransition Request(PlayerIntent intent)
    {
        PlayerState from = Current;
        return from switch
        {
            PlayerState.Title => RequestFromTitle(intent, from),
            PlayerState.Playing => RequestFromPlaying(intent, from),
            PlayerState.Pause => RequestFromPause(intent, from),
            PlayerState.Backlog or PlayerState.Save or PlayerState.Load or PlayerState.Preferences =>
                RequestFromOverlay(intent, from),
            PlayerState.ConfirmExit => RequestFromConfirmExit(intent, from),
            _ => Deny(from, from, $"Unknown player state '{from}'."),
        };
    }

    private PlayerTransition RequestFromTitle(PlayerIntent intent, PlayerState from) =>
        intent switch
        {
            PlayerIntent.NewGame or PlayerIntent.Continue => StartPlaying(from),
            PlayerIntent.OpenLoad => PushOverlay(from, PlayerState.Load),
            PlayerIntent.OpenPreferences => PushOverlay(from, PlayerState.Preferences),
            PlayerIntent.RequestExitToApp => EnterConfirmExit(from, PlayerExitTarget.Application),
            _ => Deny(from, from, $"Intent '{intent}' is not available on the title screen."),
        };

    private PlayerTransition StartPlaying(PlayerState from)
    {
        _returnStack.Clear();
        return Move(from, PlayerState.Playing);
    }

    private PlayerTransition RequestFromPlaying(PlayerIntent intent, PlayerState from) =>
        intent switch
        {
            PlayerIntent.OpenPause => PushOverlay(from, PlayerState.Pause),
            PlayerIntent.OpenBacklog => PushOverlay(from, PlayerState.Backlog),
            PlayerIntent.RequestExitToTitle => EnterConfirmExit(from, PlayerExitTarget.Title),
            PlayerIntent.RequestExitToApp => EnterConfirmExit(from, PlayerExitTarget.Application),
            PlayerIntent.OpenSave or PlayerIntent.OpenLoad or PlayerIntent.OpenPreferences =>
                Deny(from, from, $"Intent '{intent}' needs the pause menu first."),
            _ => Deny(from, from, $"Intent '{intent}' is not available while playing."),
        };

    private PlayerTransition RequestFromPause(PlayerIntent intent, PlayerState from) =>
        intent switch
        {
            PlayerIntent.Resume or PlayerIntent.CloseOverlay => PopToReturner(from),
            PlayerIntent.OpenBacklog => PushOverlay(from, PlayerState.Backlog),
            PlayerIntent.OpenSave => PushOverlay(from, PlayerState.Save),
            PlayerIntent.OpenLoad => PushOverlay(from, PlayerState.Load),
            PlayerIntent.OpenPreferences => PushOverlay(from, PlayerState.Preferences),
            PlayerIntent.RequestExitToTitle => EnterConfirmExit(from, PlayerExitTarget.Title),
            PlayerIntent.RequestExitToApp => EnterConfirmExit(from, PlayerExitTarget.Application),
            PlayerIntent.QuitToTitle => QuitToTitle(from),
            _ => Deny(from, from, $"Intent '{intent}' is not available in the pause menu."),
        };

    private PlayerTransition RequestFromOverlay(PlayerIntent intent, PlayerState from) =>
        intent switch
        {
            PlayerIntent.Resume or PlayerIntent.CloseOverlay => PopToReturner(from),
            PlayerIntent.RequestExitToTitle => EnterConfirmExit(from, PlayerExitTarget.Title),
            PlayerIntent.RequestExitToApp => EnterConfirmExit(from, PlayerExitTarget.Application),
            PlayerIntent.QuitToTitle => QuitToTitle(from),
            _ => Deny(from, from, $"Intent '{intent}' is not available in '{from}'."),
        };

    private PlayerTransition RequestFromConfirmExit(PlayerIntent intent, PlayerState from) =>
        intent switch
        {
            PlayerIntent.ConfirmExit => ConfirmCurrentExit(from),
            PlayerIntent.CancelExit => PopToReturner(from),
            _ => Deny(from, from, $"Intent '{intent}' is not available while confirming exit."),
        };

    private PlayerTransition Move(PlayerState from, PlayerState to)
    {
        Current = to;
        if (to != PlayerState.ConfirmExit)
            ExitTarget = PlayerExitTarget.None;
        return new PlayerTransition(true, from, to, null);
    }

    private PlayerTransition PushOverlay(PlayerState from, PlayerState overlay)
    {
        if (_returnStack.Count >= MaxOverlayDepth)
            return Deny(from, from, "Overlay stack is full; close a panel first.");
        _returnStack.Push(from);
        return Move(from, overlay);
    }

    private PlayerTransition PopToReturner(PlayerState from)
    {
        if (_returnStack.Count == 0)
            return Deny(from, from, $"Nothing to return to from '{from}'.");
        PlayerState to = _returnStack.Pop();
        return Move(from, to);
    }

    private PlayerTransition EnterConfirmExit(PlayerState from, PlayerExitTarget target)
    {
        if (_returnStack.Count >= MaxOverlayDepth)
            return Deny(from, from, "Overlay stack is full; close a panel first.");
        _returnStack.Push(from);
        ExitTarget = target;
        Current = PlayerState.ConfirmExit;
        return new PlayerTransition(true, from, PlayerState.ConfirmExit, null);
    }

    private PlayerTransition ConfirmCurrentExit(PlayerState from)
    {
        if (ExitTarget == PlayerExitTarget.Application)
            ExitConfirmed = true;
        _returnStack.Clear();
        ExitTarget = PlayerExitTarget.None;
        Current = PlayerState.Title;
        return new PlayerTransition(true, from, PlayerState.Title, null);
    }

    private PlayerTransition QuitToTitle(PlayerState from)
    {
        _returnStack.Clear();
        return Move(from, PlayerState.Title);
    }

    private static PlayerTransition Deny(PlayerState from, PlayerState to, string reason) =>
        new(false, from, to, reason);
}
