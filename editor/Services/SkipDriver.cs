using System;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 3 — continuous skip driver for the player loop.
/// Evaluates the Dilim 2 single-step gate once per call, so a per-frame
/// Playing loop fast-forwards read lines and halts itself on unread text
/// or choices. At most one advance happens per <see cref="Tick"/> to keep
/// the host responsive; the loop keeps calling while playing.
/// </summary>
public sealed class SkipDriver
{
    /// <summary>
    /// Performs one skip step when the profile gate allows it.
    /// Returns true when an advance happened.
    /// </summary>
    public bool Tick(
        PlayerLoopService loop,
        Func<IReadOnlyList<string?>> getActiveIds,
        Func<bool> hasChoices,
        Action<uint> advance,
        out string? saveError)
    {
        ArgumentNullException.ThrowIfNull(loop);
        ArgumentNullException.ThrowIfNull(getActiveIds);
        ArgumentNullException.ThrowIfNull(hasChoices);
        ArgumentNullException.ThrowIfNull(advance);
        saveError = null;
        if (loop.Profile.SkipMode == PlayerSkipMode.Off)
            return false;
        return loop.TrySkipStep(getActiveIds, hasChoices, advance, out saveError);
    }
}
