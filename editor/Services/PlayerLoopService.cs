using System;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 2 — save-slot independent read tracking and skip stepping
/// for one player profile.
/// <list type="bullet">
/// <item><see cref="AdvanceAndTrack"/> snapshots the presented content ids
/// <em>before</em> advancing (the line being left is the line just read),
/// advances, records them via <see cref="PlayerProfile.MarkRead"/>, and
/// atomically persists the profile.</item>
/// <item><see cref="TrySkipStep"/> evaluates <see cref="SkipGate"/> against
/// the current presentation and advances exactly once when skipping is
/// allowed. Continuous auto-skip belongs to the Faz 2 Playing-state loop;
/// this slice delivers the single-step gate the loop will drive.</item>
/// </list>
/// The engine is reached only through caller-supplied seams
/// (<paramref name="getActiveIds"/>, <paramref name="advance"/>) so the
/// tracking and gate logic is fully unit-testable without native code.
/// Production wiring passes <c>EngineHost</c> lambdas
/// (see <c>EngineHost.AdvancePlayerLoop</c>).
/// </summary>
public sealed class PlayerLoopService
{
    public PlayerProfile Profile { get; }

    /// <summary>Directory the profile persists to.</summary>
    public string ProfileDirectory => _profileDirectory;

    private readonly string _profileDirectory;
    private bool _dirty;

    public PlayerLoopService(PlayerProfile profile, string profileDirectory)
    {
        Profile = profile ?? throw new ArgumentNullException(nameof(profile));
        _profileDirectory = !string.IsNullOrWhiteSpace(profileDirectory)
            ? profileDirectory
            : throw new ArgumentException("A profile directory is required.", nameof(profileDirectory));
    }

    /// <summary>Loads (or defaults) the profile for a directory.</summary>
    public static PlayerLoopService Load(string profileDirectory)
    {
        var loaded = PlayerProfileStore.Load(profileDirectory);
        return new PlayerLoopService(loaded.Profile, profileDirectory);
    }

    /// <summary>
    /// Records presented content ids as read. Invalid/empty ids are ignored
    /// (fail closed). Returns how many ids were newly added.
    /// </summary>
    public int NotePresented(IEnumerable<string?> activeContentIds)
    {
        ArgumentNullException.ThrowIfNull(activeContentIds);
        int added = 0;
        foreach (string? id in activeContentIds)
        {
            if (Profile.MarkRead(id))
                added++;
        }
        if (added > 0)
            _dirty = true;
        return added;
    }

    /// <summary>
    /// Atomically persists the profile when new reads arrived since the last
    /// flush. Returns null on success (or when nothing changed), otherwise a
    /// structured error the caller can log or toast.
    /// </summary>
    public string? Flush()
    {
        if (!_dirty)
            return null;
        string? error = PlayerProfileStore.Save(_profileDirectory, Profile);
        if (error is null)
            _dirty = false;
        return error;
    }

    /// <summary>Evaluates the skip gate for the current presentation.</summary>
    public bool ShouldSkip(IReadOnlyList<string?> activeContentIds, bool hasChoices) =>
        SkipGate.ShouldSkip(Profile.SkipMode, activeContentIds, Profile.IsRead, hasChoices);

    /// <summary>
    /// Advances once and tracks the departed line as read.
    /// Returns a save error when persistence failed, null otherwise.
    /// </summary>
    public string? AdvanceAndTrack(
        Func<IReadOnlyList<string?>> getActiveIds,
        Action<uint> advance,
        uint choiceIndex = 0)
    {
        ArgumentNullException.ThrowIfNull(getActiveIds);
        ArgumentNullException.ThrowIfNull(advance);
        IReadOnlyList<string?> presented;
        try
        {
            presented = getActiveIds() ?? Array.Empty<string?>();
        }
        catch (Exception queryFailure)
        {
            return $"Active content ids could not be queried ({queryFailure.GetType().Name}: {queryFailure.Message}); advance aborted before tracking.";
        }
        advance(choiceIndex);
        NotePresented(presented);
        return Flush();
    }

    /// <summary>
    /// Performs a single skip step when the gate allows it.
    /// Returns true when an advance happened.
    /// </summary>
    public bool TrySkipStep(
        Func<IReadOnlyList<string?>> getActiveIds,
        Func<bool> hasChoices,
        Action<uint> advance,
        out string? saveError)
    {
        ArgumentNullException.ThrowIfNull(getActiveIds);
        ArgumentNullException.ThrowIfNull(hasChoices);
        ArgumentNullException.ThrowIfNull(advance);
        saveError = null;
        IReadOnlyList<string?> presented = getActiveIds() ?? Array.Empty<string?>();
        if (!ShouldSkip(presented, hasChoices()))
            return false;
        advance(0);
        NotePresented(presented);
        saveError = Flush();
        return true;
    }
}
