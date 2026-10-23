using System.Collections.Generic;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>Faz 8 D2: F6 / Shift+F6 panel odak sırası (sarmalı).</summary>
public static class WorkspaceFocusOrder
{
    // Boş → null; bilinmeyen current → ileri ilk, geri son.
    public static string? Next(IReadOnlyList<string> visiblePanelIds, string? currentPanelId, bool backwards)
    {
        if (visiblePanelIds is null || visiblePanelIds.Count == 0)
            return null;
        int index = currentPanelId is null ? -1 : visiblePanelIds.IndexOf(currentPanelId);
        if (index < 0)
            return backwards ? visiblePanelIds[visiblePanelIds.Count - 1] : visiblePanelIds[0];
        int next = backwards
            ? (index - 1 + visiblePanelIds.Count) % visiblePanelIds.Count
            : (index + 1) % visiblePanelIds.Count;
        return visiblePanelIds[next];
    }

    // IndexOf (IReadOnlyList için).
    private static int IndexOf(this IReadOnlyList<string> list, string value)
    {
        for (int i = 0; i < list.Count; i++)
        {
            if (list[i] == value)
                return i;
        }
        return -1;
    }
}
