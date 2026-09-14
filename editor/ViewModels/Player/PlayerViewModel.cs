using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels.Player;

/// <summary>One presented choice button (label + stable option id).</summary>
public sealed class PlayerChoiceOption
{
    public PlayerChoiceOption(string optionId, string label)
    {
        OptionId = optionId;
        Label = label;
    }

    public string OptionId { get; }
    public string Label { get; }
}

/// <summary>
/// Faz 2 Dilim 5 — integration brain of the standalone player shell.
/// Binds <see cref="PlayerStateMachine"/>, <see cref="PlayerLoopService"/>,
/// <see cref="AutoPlayDriver"/>, <see cref="SkipDriver"/> and
/// <see cref="PlayerInputMapper"/> onto an <see cref="IPlayerEngine"/> so
/// the whole visual-novel loop (title → play → pause → save/load →
/// backlog → exit) runs without editor buttons.
/// <list type="bullet">
/// <item>State transitions apply engine side effects: overlays pause the
/// sim, Playing resumes it.</item>
/// <item><see cref="Tick"/> drives the engine clock plus the skip/auto
/// drivers while Playing; menus and pending choices halt them.</item>
/// <item>A presented line counts as complete once its text is stable
/// across two ticks (no native typewriter query exists yet).</item>
/// </list>
/// Engine-free seams (fake <see cref="IPlayerEngine"/>) keep every flow
/// unit-tested.
/// </summary>
public sealed partial class PlayerViewModel : ViewModelBase
{
    private readonly IPlayerEngine _engine;
    private string _lastTickDialogue = string.Empty;
    private int _stableTicks;

    public PlayerViewModel(IPlayerEngine engine, PlayerLoopService loop)
    {
        _engine = engine ?? throw new ArgumentNullException(nameof(engine));
        Loop = loop ?? throw new ArgumentNullException(nameof(loop));
        Machine = new PlayerStateMachine();
        AutoDriver = new AutoPlayDriver();
        SkipDriver = new SkipDriver();
        Bindings = PlayerInputBindings.Default();
        Slots = new PlayerSaveSlotsViewModel(engine);
        ApplyVolumes();
    }

    public PlayerStateMachine Machine { get; }
    public PlayerLoopService Loop { get; }
    public AutoPlayDriver AutoDriver { get; }
    public SkipDriver SkipDriver { get; }
    public PlayerInputBindings Bindings { get; }
    public PlayerSaveSlotsViewModel Slots { get; }

    public PlayerProfile Profile => Loop.Profile;

    [ObservableProperty]
    private string _speaker = string.Empty;

    [ObservableProperty]
    private string _dialogue = string.Empty;

    [ObservableProperty]
    private string _lastError = string.Empty;

    [ObservableProperty]
    private bool _exitRequested;

    public ObservableCollection<PlayerChoiceOption> Choices { get; } = new();

    public IReadOnlyList<DialogueHistoryEntry> HistoryEntries => _engine.History;

    public bool IsTitle => Machine.Current == PlayerState.Title;
    public bool IsPlaying => Machine.Current == PlayerState.Playing;
    public bool IsPause => Machine.Current == PlayerState.Pause;
    public bool IsBacklog => Machine.Current == PlayerState.Backlog;
    public bool IsSave => Machine.Current == PlayerState.Save;
    public bool IsLoad => Machine.Current == PlayerState.Load;
    public bool IsPreferences => Machine.Current == PlayerState.Preferences;
    public bool IsConfirmExit => Machine.Current == PlayerState.ConfirmExit;
    public bool IsAutoOn => Profile.AutoEnabled;
    public string SkipModeLabel => Profile.SkipMode.ToString();
    public bool CanContinue => FindLatestSlot() is not null;

    public Array AvailableSkipModes => Enum.GetValues(typeof(PlayerSkipMode));
    public IReadOnlyList<string> AvailableLanguages => PlayerProfile.SupportedLanguages;

    [RelayCommand]
    public void SavePreferences()
    {
        ApplyVolumes();
        PersistProfile();
        OnPropertyChanged(nameof(IsAutoOn));
        OnPropertyChanged(nameof(SkipModeLabel));
    }

    // ── Top-level flows ──────────────────────────────────────────
    //
    // Value-returning flows stay plain methods (tests assert outcomes);
    // the *Command properties below adapt them for view binding.

    public IRelayCommand NewGameCommand => new RelayCommand(() => NewGame());
    public IRelayCommand ContinueCommand => new RelayCommand(() => Continue());
    public IRelayCommand RequestIntentCommand =>
        new RelayCommand<PlayerIntent>(intent => RequestIntent(intent));
    public IRelayCommand SaveToSlotCommand =>
        new RelayCommand<int>(index => SaveToSlot(index));
    public IRelayCommand LoadFromSlotCommand =>
        new RelayCommand<int>(index => LoadFromSlot(index));

    public bool NewGame()
    {
        if (!_engine.IsAvailable)
            return Fail("Engine is not available.");
        _engine.ResetToStartNode();
        _engine.SetPlayState(true);
        return EnterPlaying(PlayerIntent.NewGame);
    }

    public bool Continue()
    {
        if (!_engine.IsAvailable)
            return Fail("Engine is not available.");
        int? latest = FindLatestSlot();
        if (latest is null)
            return Fail("No save slot to continue from.");
        if (!_engine.LoadSlot(latest.Value))
            return Fail($"Save slot #{latest.Value} could not be loaded.");
        _engine.SetPlayState(true);
        return EnterPlaying(PlayerIntent.Continue);
    }

    public PlayerTransition RequestIntent(PlayerIntent intent)
    {
        var transition = Machine.Request(intent);
        if (!transition.Allowed)
        {
            LastError = transition.DenyReason ?? $"Intent '{intent}' was denied.";
            return transition;
        }
        ApplyStateSideEffects();
        RefreshPresentation();
        return transition;
    }

    // ── Semantic input ───────────────────────────────────────────

    public void HandleCommand(PlayerInputCommand command)
    {
        switch (command)
        {
            case PlayerInputCommand.Advance:
                if (IsPlaying)
                    OnAdvanceInput();
                break;
            case PlayerInputCommand.CancelBack:
                if (IsPlaying)
                    RequestIntent(PlayerIntent.RequestExitToTitle);
                else if (!IsTitle)
                    RequestIntent(PlayerIntent.CloseOverlay);
                break;
            case PlayerInputCommand.TogglePause:
                if (IsPlaying)
                    RequestIntent(PlayerIntent.OpenPause);
                else if (IsPause)
                    RequestIntent(PlayerIntent.Resume);
                break;
            case PlayerInputCommand.ToggleAuto:
                Profile.AutoEnabled = !Profile.AutoEnabled;
                AutoDriver.SetEnabled(Profile.AutoEnabled);
                PersistProfile();
                OnPropertyChanged(nameof(IsAutoOn));
                break;
            case PlayerInputCommand.ToggleSkip:
                Profile.SkipMode = Profile.SkipMode switch
                {
                    PlayerSkipMode.Off => PlayerSkipMode.ReadOnly,
                    PlayerSkipMode.ReadOnly => PlayerSkipMode.All,
                    _ => PlayerSkipMode.Off,
                };
                PersistProfile();
                OnPropertyChanged(nameof(SkipModeLabel));
                break;
            case PlayerInputCommand.OpenBacklog:
                if (IsPlaying || IsPause)
                    RequestIntent(PlayerIntent.OpenBacklog);
                break;
            case PlayerInputCommand.QuickSave:
                if (IsPlaying)
                    SaveToSlot(0);
                break;
            case PlayerInputCommand.QuickLoad:
                if (IsPlaying && _engine.HasSlot(0))
                    LoadFromSlot(0);
                break;
        }
    }

    // Button-friendly aliases over HandleCommand (one semantic per button).

    [RelayCommand]
    public void ToggleAuto() => HandleCommand(PlayerInputCommand.ToggleAuto);

    [RelayCommand]
    public void ToggleSkip() => HandleCommand(PlayerInputCommand.ToggleSkip);

    [RelayCommand]
    public void QuickSave() => HandleCommand(PlayerInputCommand.QuickSave);

    [RelayCommand]
    public void QuickLoad() => HandleCommand(PlayerInputCommand.QuickLoad);

    [RelayCommand]
    public void OpenPause() => HandleCommand(PlayerInputCommand.TogglePause);

    [RelayCommand]
    public void OpenPlayerBacklog() => HandleCommand(PlayerInputCommand.OpenBacklog);

    [RelayCommand]
    public void SlotsNextPage() => Slots.NextPage();

    [RelayCommand]
    public void SlotsPreviousPage() => Slots.PreviousPage();

    [RelayCommand]
    public void OnAdvanceInput()
    {
        if (_engine.HasChoices())
            return;
        string? error = Loop.AdvanceAndTrack(
            () => _engine.GetActiveDialogueContentIds(),
            index => _engine.AdvanceNode(index));
        if (error is not null)
            Fail(error);
        AutoDriver.NotifyManualAdvance();
        _stableTicks = 0;
        RefreshPresentation();
    }

    [RelayCommand]
    public void SelectChoice(string optionId)
    {
        var presented = _engine.GetActiveDialogueContentIds();
        if (!_engine.SelectChoice(optionId))
        {
            Fail($"Choice '{optionId}' was rejected.");
            return;
        }
        Loop.NotePresented(presented);
        string? error = Loop.Flush();
        if (error is not null)
            Fail(error);
        AutoDriver.NotifyManualAdvance();
        _stableTicks = 0;
        RefreshPresentation();
    }

    // ── Per-frame driver ─────────────────────────────────────────

    public void Tick(float dt)
    {
        if (Machine.Current != PlayerState.Playing || dt <= 0.0f)
            return;
        _engine.Step(dt);

        string text = _engine.Dialogue ?? string.Empty;
        if (!string.Equals(text, _lastTickDialogue, StringComparison.Ordinal))
        {
            _lastTickDialogue = text;
            _stableTicks = 0;
            AutoDriver.BeginLine(text.Length, 0.0f, Profile);
        }
        else
        {
            _stableTicks++;
        }

        bool hasChoices = _engine.HasChoices();
        Machine.SetChoicesPending(hasChoices);
        if (hasChoices)
        {
            RefreshPresentation();
            return;
        }

        if (Profile.SkipMode != PlayerSkipMode.Off)
        {
            if (SkipDriver.Tick(Loop,
                    () => _engine.GetActiveDialogueContentIds(),
                    () => _engine.HasChoices(),
                    index => _engine.AdvanceNode(index),
                    out string? skipError))
            {
                if (skipError is not null)
                    Fail(skipError);
                _stableTicks = 0;
                RefreshPresentation();
                return;
            }
        }

        if (Profile.AutoEnabled)
        {
            bool lineComplete = _stableTicks >= 1;
            if (AutoDriver.Tick(dt, lineComplete, hasChoices: false))
                OnAdvanceInput();
            else
                RefreshPresentation();
        }
        else
        {
            RefreshPresentation();
        }
    }

    // ── Slots & preferences ──────────────────────────────────────

    public bool SaveToSlot(int index)
    {
        if (!_engine.SaveSlot(index))
            return Fail($"Save slot #{index} could not be written.");
        Slots.Refresh();
        RefreshPresentation();
        return true;
    }

    public bool LoadFromSlot(int index)
    {
        if (!_engine.LoadSlot(index))
            return Fail($"Save slot #{index} could not be loaded.");
        _engine.SetPlayState(true);
        if (Machine.Current != PlayerState.Playing)
        {
            // Title/pause-initiated load: close the picker, then enter play.
            if (Machine.Current == PlayerState.Load)
                Machine.Request(PlayerIntent.CloseOverlay);
            return EnterPlaying(PlayerIntent.Continue);
        }
        _stableTicks = 0;
        RefreshPresentation();
        return true;
    }

    public void ApplyVolumes()
    {
        _engine.SetMasterVolume(Profile.MasterVolume);
        _engine.SetBgmVolume(Profile.BgmVolume);
        _engine.SetVoiceVolume(Profile.VoiceVolume);
        _engine.SetSfxVolume(Profile.SfxVolume);
    }

    public void PersistProfile()
    {
        string? error = Loop.Flush();
        if (error is null)
        {
            // Flush is a no-op without new reads; preferences still need a save.
            error = PlayerProfileStore.Save(Loop.ProfileDirectory, Profile);
        }
        if (error is not null)
            Fail(error);
    }

    // ── Internals ────────────────────────────────────────────────

    private bool EnterPlaying(PlayerIntent intent)
    {
        var transition = Machine.Request(intent);
        if (!transition.Allowed)
            return Fail(transition.DenyReason ?? "Could not enter Playing.");
        AutoDriver.SetEnabled(Profile.AutoEnabled);
        _stableTicks = 0;
        ApplyStateSideEffects();
        RefreshPresentation();
        return true;
    }

    private void ApplyStateSideEffects()
    {
        switch (Machine.Current)
        {
            case PlayerState.Playing:
                _engine.SetPaused(false);
                break;
            case PlayerState.Title:
                _engine.SetPaused(false);
                _engine.SetPlayState(false);
                break;
            default:
                _engine.SetPaused(true);
                break;
        }
        // ConfirmCurrentExit lands on Title with the flag set; the shell
        // observes ExitRequested and terminates.
        if (Machine.ExitConfirmed)
            ExitRequested = true;
        if (Machine.Current is PlayerState.Save or PlayerState.Load)
            Slots.Refresh();
        if (Machine.Current == PlayerState.Backlog)
            _engine.RefreshHistory();
        RaiseStateProperties();
    }

    private void RaiseStateProperties()
    {
        OnPropertyChanged(nameof(IsTitle));
        OnPropertyChanged(nameof(IsPlaying));
        OnPropertyChanged(nameof(IsPause));
        OnPropertyChanged(nameof(IsBacklog));
        OnPropertyChanged(nameof(IsSave));
        OnPropertyChanged(nameof(IsLoad));
        OnPropertyChanged(nameof(IsPreferences));
        OnPropertyChanged(nameof(IsConfirmExit));
        OnPropertyChanged(nameof(CanContinue));
        OnPropertyChanged(nameof(IsAutoOn));
        OnPropertyChanged(nameof(SkipModeLabel));
    }

    public void RefreshPresentation()
    {
        Speaker = _engine.Speaker ?? string.Empty;
        Dialogue = _engine.Dialogue ?? string.Empty;
        Choices.Clear();
        var labels = _engine.GetChoiceLabels();
        var optionIds = _engine.GetChoiceOptionIds();
        for (int i = 0; i < labels.Count; i++)
        {
            string optionId = i < optionIds.Count ? optionIds[i] : i.ToString();
            Choices.Add(new PlayerChoiceOption(optionId, labels[i]));
        }
        OnPropertyChanged(nameof(HistoryEntries));
        RaiseStateProperties();
    }

    private int? FindLatestSlot() => Slots.FindLatestOccupied();

    private bool Fail(string message)
    {
        LastError = message;
        return false;
    }
}
