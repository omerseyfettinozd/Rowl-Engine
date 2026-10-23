using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// D01c-2 - where a <see cref="PlayerLoopService"/> keeps its profile when the
/// profile file is owned by the native engine (single-writer rule: with a
/// native store mounted, the managed code never writes the file itself).
/// The implementation lives next to the P/Invoke layer
/// (<c>NativePlayerProfileBackend</c>); tests may fake it.
/// </summary>
public interface IPlayerProfileBackend
{
    /// <summary>False once the profile can no longer be written (read-only store, project changed).</summary>
    bool IsWritable { get; }

    /// <summary>Tells the store that these (normalized) content ids were read.</summary>
    string? MarkRead(IReadOnlyList<string> contentIds);

    /// <summary>Pushes the preference fields of <paramref name="profile"/> and writes the file now.</summary>
    string? SavePreferences(PlayerProfile profile);

    /// <summary>Writes pending changes now.</summary>
    string? Flush();
}
