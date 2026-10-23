using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services.NodeLibrary;

// Kütüphane hata kodları (UI enum ile yerelleştirir).
public enum NodeLibraryError
{
    InvalidName,
    DuplicateName,
    ParentNotFound,
    FolderNotFound,
    CycleNotAllowed,
    ItemNotFound,
    AlreadySaved
}

// Kararlı İngilizce mesajlı alan hatası.
public sealed class NodeLibraryException : Exception
{
    public NodeLibraryError Error { get; }

    public NodeLibraryException(NodeLibraryError error)
        : base(GetMessage(error))
    {
        Error = error;
    }

    private static string GetMessage(NodeLibraryError error) => error switch
    {
        NodeLibraryError.InvalidName => "Invalid folder name.",
        NodeLibraryError.DuplicateName => "A folder with the same name already exists here.",
        NodeLibraryError.ParentNotFound => "Parent folder not found.",
        NodeLibraryError.FolderNotFound => "Folder not found.",
        NodeLibraryError.CycleNotAllowed => "A folder cannot be moved into itself or one of its descendants.",
        NodeLibraryError.ItemNotFound => "Library item not found.",
        NodeLibraryError.AlreadySaved => "This node is already saved in the library.",
        _ => "Node library error."
    };
}

// Yeniden kullanılabilir düğüm şablonu.
public sealed record NodeTemplate(string Title, IReadOnlyList<TemplateObject> Objects)
{
    public NodeTemplate DeepCopy()
    {
        var list = new List<TemplateObject>(Objects.Count);
        foreach (var o in Objects)
            list.Add(o.DeepCopy());
        return new NodeTemplate(Title, list);
    }
}

// Şablon içindeki tek nesne.
public sealed record TemplateObject(string Name, bool IsActive, IReadOnlyList<TemplateComponent> Components)
{
    public TemplateObject DeepCopy()
    {
        var list = new List<TemplateComponent>(Components.Count);
        foreach (var c in Components)
            list.Add(c.DeepCopy());
        return new TemplateObject(Name, IsActive, list);
    }
}

// Bileşen: tür + açık/kapalı + kayıt dosyasıyla aynı "data" sözlüğü (StoryGraphSerializer biçimi).
public sealed record TemplateComponent(string TypeKey, JsonObject Values, bool Enabled = true)
{
    public TemplateComponent DeepCopy()
    {
        return new TemplateComponent(TypeKey, (JsonObject)Values.DeepClone(), Enabled);
    }
}

// İç içe klasör.
public sealed record LibraryFolder(string Id, string Name, string? ParentId);

// Kayıtlı şablon (kaynak düğüm anahtarı ile).
public sealed record LibraryItem(string Id, string? FolderId, string SourceKey, NodeTemplate Template, DateTimeOffset SavedAtUtc)
{
    public LibraryItem DeepCopy()
    {
        return new LibraryItem(Id, FolderId, SourceKey, Template.DeepCopy(), SavedAtUtc);
    }
}

// Kütüphane kök durumu.
public sealed record NodeLibraryState(IReadOnlyList<LibraryFolder> Folders, IReadOnlyList<LibraryItem> Items)
{
    public static NodeLibraryState Empty { get; } = new(Array.Empty<LibraryFolder>(), Array.Empty<LibraryItem>());

    public NodeLibraryState DeepCopy()
    {
        var folders = new List<LibraryFolder>(Folders.Count);
        folders.AddRange(Folders);
        var items = new List<LibraryItem>(Items.Count);
        foreach (var i in Items)
            items.Add(i.DeepCopy());
        return new NodeLibraryState(folders, items);
    }
}
