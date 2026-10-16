using System;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D5 — reads and writes the dock workspace in the machine-local
/// <see cref="EditorSettingsProfile"/> (<see cref="EditorSettingsProfile.WorkspaceState"/>).
/// Loading never throws and never shows anything to the user: a missing,
/// corrupt, outdated or unrecognisable saved workspace silently becomes the
/// default one, with a development warning through <c>warn</c>.
/// </summary>
public static class WorkspaceStatePersistence
{
    /// <summary>The saved workspace, or <see cref="EditorWorkspaceState.Default"/> (first run: no warning).</summary>
    public static EditorWorkspaceState Load(string settingsPath, Action<string>? warn = null)
    {
        try
        {
            EditorSettingsProfile profile = EditorSettingsProfile.Load(
                settingsPath,
                error => warn?.Invoke("editor profile unreadable, using the default workspace: " + error));
            if (profile.WorkspaceState is null)
                return EditorWorkspaceState.Default;

            WorkspaceStateLoadResult result = WorkspaceStateJson.Parse(profile.WorkspaceState);
            if (result.IsValid)
                return result.State!;
            warn?.Invoke("saved workspace layout rejected, using the default: " + result.Error);
        }
        catch (Exception ex)
        {
            // Loading is best effort by contract; the editor must always start.
            warn?.Invoke("saved workspace layout could not be loaded, using the default: " + ex.Message);
        }
        return EditorWorkspaceState.Default;
    }

    /// <summary>Writes the workspace into the profile (atomic file write; other settings are kept).</summary>
    public static void Save(string settingsPath, EditorWorkspaceState state)
    {
        string json = WorkspaceStateJson.Serialize(state);
        EditorSettingsProfile.Update(settingsPath, profile => profile.WorkspaceState = json);
    }
}

/// <summary>A restartable one-shot timer (the view model uses the UI dispatcher; tests use a manual clock).</summary>
public interface IDebounceTimer : IDisposable
{
    /// <summary>(Re)starts the countdown; an earlier pending tick is cancelled.</summary>
    void Restart();

    /// <summary>Cancels the pending tick, if any.</summary>
    void Stop();
}

/// <summary>Creates a timer that calls <c>elapsed</c> once <c>delay</c> after the last <see cref="IDebounceTimer.Restart"/>.</summary>
public delegate IDebounceTimer DebounceTimerFactory(TimeSpan delay, Action elapsed);

/// <summary>
/// Faz 5 D5 — coalesces workspace changes into few disk writes: every
/// <see cref="Notify"/> with a state that differs from what is already saved
/// restarts a short timer, and only when it elapses (no change for
/// <c>delay</c>) the newest state is written once. <see cref="Flush"/> writes
/// a pending state immediately (window close). A failed write never throws; it
/// is reported through <c>warn</c> and retried by the next change.
/// </summary>
public sealed class WorkspaceAutosave : IDisposable
{
    /// <summary>Default quiet period before a change is written (500 ms).</summary>
    public static readonly TimeSpan DefaultDelay = TimeSpan.FromMilliseconds(500);

    private readonly Action<EditorWorkspaceState> _write;
    private readonly Action<string>? _warn;
    private readonly IDebounceTimer _timer;
    private EditorWorkspaceState _saved;
    private EditorWorkspaceState? _pending;
    private bool _disposed;

    /// <param name="write">Persists a state (may throw; the failure is reported, not propagated).</param>
    /// <param name="timers">Timer source (UI dispatcher in the app, a manual clock in tests).</param>
    /// <param name="baseline">State already on disk (what was just loaded); it is not written again.</param>
    public WorkspaceAutosave(
        Action<EditorWorkspaceState> write,
        DebounceTimerFactory timers,
        TimeSpan delay,
        EditorWorkspaceState baseline,
        Action<string>? warn = null)
    {
        _write = write;
        _warn = warn;
        _saved = baseline;
        _timer = timers(delay, Flush);
    }

    /// <summary>A pending (not yet written) state exists.</summary>
    public bool HasPending => _pending is not null;

    /// <summary>The workspace changed. Equal to the saved state = nothing to write.</summary>
    public void Notify(EditorWorkspaceState state)
    {
        if (_disposed)
            return;
        if (state == _saved)
        {
            // Changed and changed back inside the quiet period: nothing to write after all.
            _pending = null;
            _timer.Stop();
            return;
        }
        _pending = state;
        _timer.Restart();
    }

    /// <summary>Writes the pending state now (no-op when there is none).</summary>
    public void Flush()
    {
        _timer.Stop();
        if (_pending is not { } state)
            return;
        _pending = null;
        try
        {
            _write(state);
            _saved = state;
        }
        catch (Exception ex)
        {
            _warn?.Invoke("workspace layout could not be saved: " + ex.Message);
        }
    }

    /// <summary>Flushes a pending state and releases the timer; later notifications are ignored.</summary>
    public void Dispose()
    {
        if (_disposed)
            return;
        Flush();
        _disposed = true;
        _timer.Dispose();
    }
}
