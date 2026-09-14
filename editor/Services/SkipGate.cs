using System;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 2 — pure skip decision gate for the player loop.
/// Decides whether the currently presented line may be fast-forwarded
/// without player input. Side-effect free and engine-free so the full
/// matrix is unit-testable.
/// <list type="bullet">
/// <item>Choices always stop the skip: a selection needs manual input.</item>
/// <item><see cref="PlayerSkipMode.Off"/> never skips.</item>
/// <item><see cref="PlayerSkipMode.All"/> skips any presented line.</item>
/// <item><see cref="PlayerSkipMode.ReadOnly"/> skips only when every
/// presented content id is valid and already recorded as read. Empty or
/// malformed ids (pre-migration lines) fail closed: the skip stops.</item>
/// </list>
/// </summary>
public static class SkipGate
{
    public static bool ShouldSkip(
        PlayerSkipMode mode,
        IReadOnlyList<string?> activeContentIds,
        Func<string?, bool> isRead,
        bool hasChoices)
    {
        ArgumentNullException.ThrowIfNull(activeContentIds);
        ArgumentNullException.ThrowIfNull(isRead);
        if (hasChoices)
            return false;
        switch (mode)
        {
            case PlayerSkipMode.Off:
                return false;
            case PlayerSkipMode.All:
                return true;
            case PlayerSkipMode.ReadOnly:
                if (activeContentIds.Count == 0)
                    return false;
                foreach (string? id in activeContentIds)
                {
                    if (!isRead(id))
                        return false;
                }
                return true;
            default:
                return false;
        }
    }
}
