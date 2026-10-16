using System.Collections.Generic;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Services;

/// <summary>Faz 8 D2: MainWindow_KeyDown'da gerçekten işlenen kısayollar (+F6 çifti).</summary>
public sealed record EditorShortcut(string Gesture, string DescriptionKey)
{
    public string Description => EditorStrings.T(DescriptionKey);

    /// <summary>Ayarlar'daki gösterim: "Ctrl + S", "Ctrl + ," (OemComma okunur karaktere çevrilir).</summary>
    public string DisplayGesture => Gesture.Replace("OemComma", ",").Replace("+", " + ");
}

public static class EditorShortcutCatalog
{
    public static IReadOnlyList<EditorShortcut> All { get; } = new List<EditorShortcut>
    {
        new("Ctrl+Z", "Shortcut.Undo"),
        new("Ctrl+Y", "Shortcut.Redo"),
        new("Ctrl+Shift+Z", "Shortcut.Redo"),
        new("Ctrl+S", "Shortcut.Save"),
        new("Ctrl+Shift+S", "Shortcut.SaveAs"),
        new("Ctrl+N", "Shortcut.NewNode"),
        new("Ctrl+A", "Shortcut.SelectAll"),
        new("Ctrl+C", "Shortcut.Copy"),
        new("Ctrl+V", "Shortcut.Paste"),
        new("Ctrl+D", "Shortcut.Duplicate"),
        new("Delete", "Shortcut.DeleteNode"),
        new("F2", "Shortcut.Rename"),
        new("Ctrl+B", "Shortcut.Build"),
        new("F5", "Shortcut.Play"),
        new("Ctrl+F", "Shortcut.Search"),
        new("Escape", "Shortcut.Cancel"),
        new("Ctrl+1", "Panel.Hierarchy"),
        new("Ctrl+2", "Panel.Inspector"),
        new("Ctrl+3", "Panel.Log"),
        new("Ctrl+4", "Panel.Assets"),
        new("Ctrl+5", "Panel.Backlog"),
        new("Ctrl+6", "Panel.SaveSlots"),
        new("Ctrl+7", "Panel.Issues"),
        new("Ctrl+8", "Panel.Library"),
        new("Ctrl+9", "Panel.Lua"),
        new("F11", "Shortcut.Fullscreen"),
        new("Ctrl+OemComma", "Shortcut.Settings"),
        new("Ctrl+H", "Shortcut.OpenHub"),
        new("F6", "Shortcut.FocusNextPanel"),
        new("Shift+F6", "Shortcut.FocusPrevPanel"),
    };
}
