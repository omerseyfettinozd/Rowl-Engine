using System;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 3 — auto-play timer for the player loop.
/// Waits out the computed reading time (or the voice duration, whichever is
/// longer) and then reports that an advance is due. The host performs the
/// advance; this class only keeps time.
/// <list type="bullet">
/// <item>Wait for a line: <c>AutoAdvanceDelay + chars × PerCharSeconds /
/// TextSpeedMultiplier</c>, clamped to [MinWaitSeconds, MaxWaitSeconds],
/// then raised to at least the voice duration.</item>
/// <item>The clock runs only for a complete line with no pending choice;
/// otherwise it freezes (a choice or an unfinished typewriter never
/// fast-forwards itself).</item>
/// <item>Manual input restarts the clock via
/// <see cref="NotifyManualAdvance"/>; disabling via
/// <see cref="SetEnabled"/> stops it outright.</item>
/// </list>
/// Pure logic — the full timing matrix is unit-tested without an engine.
/// </summary>
public sealed class AutoPlayDriver
{
    /// <summary>Reading seconds per character at 1× text speed.</summary>
    public const float PerCharSeconds = 0.04f;

    /// <summary>Shortest auto wait for any line.</summary>
    public const float MinWaitSeconds = 0.8f;

    /// <summary>Longest auto wait before a forced advance.</summary>
    public const float MaxWaitSeconds = 30.0f;

    public bool Enabled { get; private set; }

    /// <summary>Seconds accumulated toward the current line's wait.</summary>
    public float ElapsedSeconds { get; private set; }

    /// <summary>Wait computed by the last <see cref="BeginLine"/>.</summary>
    public float CurrentWaitSeconds { get; private set; }

    public void SetEnabled(bool enabled)
    {
        Enabled = enabled;
        if (!enabled)
            ElapsedSeconds = 0.0f;
    }

    /// <summary>
    /// Arms the timer for a freshly presented line. Negative inputs clamp
    /// to zero; a non-positive text speed falls back to 1×.
    /// </summary>
    public void BeginLine(int charCount, float voiceDurationSeconds, PlayerProfile profile)
    {
        ArgumentNullException.ThrowIfNull(profile);
        int chars = Math.Max(0, charCount);
        float voice = Math.Max(0.0f, voiceDurationSeconds);
        float speed = profile.TextSpeedMultiplier > 0.0f ? profile.TextSpeedMultiplier : 1.0f;
        float wait = profile.AutoAdvanceDelay + (chars * PerCharSeconds / speed);
        wait = Math.Clamp(wait, MinWaitSeconds, MaxWaitSeconds);
        CurrentWaitSeconds = Math.Max(wait, voice);
        ElapsedSeconds = 0.0f;
    }

    /// <summary>Restarts the clock after a manual advance or tap.</summary>
    public void NotifyManualAdvance() => ElapsedSeconds = 0.0f;

    /// <summary>
    /// Advances the clock by <paramref name="dtSeconds"/>; returns true once
    /// when the wait elapses (the clock resets for the next line).
    /// Non-positive dt never fires.
    /// </summary>
    public bool Tick(float dtSeconds, bool lineComplete, bool hasChoices)
    {
        if (!Enabled || !lineComplete || hasChoices || dtSeconds <= 0.0f)
            return false;
        ElapsedSeconds += dtSeconds;
        if (ElapsedSeconds < CurrentWaitSeconds)
            return false;
        ElapsedSeconds = 0.0f;
        return true;
    }
}
