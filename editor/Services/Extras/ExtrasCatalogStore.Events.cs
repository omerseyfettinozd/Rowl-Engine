using System;

namespace RowlEngine.Editor.Services.Extras;

public static partial class ExtrasCatalogStore
{
    /// <summary>Raised after <see cref="Save"/> wrote the file (open unlock cards refresh their id list).</summary>
    public static event Action? Saved;

    private static void RaiseSaved() => Saved?.Invoke();
}
