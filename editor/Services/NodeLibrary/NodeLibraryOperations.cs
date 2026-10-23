using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services.NodeLibrary;

// Saf, değişmez kütüphane işlemleri (yeni durum döndürür).
public static class NodeLibraryOperations
{
    private static readonly CompareInfo TrCompare = CultureInfo.GetCultureInfo("tr-TR").CompareInfo;

    private static bool NamesEqual(string a, string b) =>
        TrCompare.Compare(a, b, CompareOptions.IgnoreCase) == 0;

    private static string NormalizeFolderName(string? name)
    {
        string trimmed = (name ?? string.Empty).Trim();
        if (trimmed.Length > 60)
            trimmed = trimmed.Substring(0, 60);
        return trimmed;
    }

    private static List<LibraryItem> CloneItems(IReadOnlyList<LibraryItem> items)
    {
        var list = new List<LibraryItem>(items.Count);
        foreach (var it in items)
            list.Add(it.DeepCopy());
        return list;
    }

    // Bağlantı hedeflerini temizleyen derin kopya.
    public static NodeTemplate Reusable(NodeTemplate template)
    {
        ArgumentNullException.ThrowIfNull(template);
        var objects = new List<TemplateObject>(template.Objects.Count);
        foreach (var o in template.Objects)
        {
            var comps = new List<TemplateComponent>(o.Components.Count);
            foreach (var c in o.Components)
            {
                var values = (JsonObject)c.Values.DeepClone();
                if (string.Equals(c.TypeKey, "choice", StringComparison.Ordinal))
                {
                    if (values["options"] is JsonArray arr)
                    {
                        foreach (var el in arr)
                        {
                            if (el is JsonObject opt)
                                opt["target_node_id"] = 0;
                        }
                    }
                }
                else if (string.Equals(c.TypeKey, "condition", StringComparison.Ordinal))
                {
                    values["fail_target_node_id"] = "";
                }
                comps.Add(new TemplateComponent(c.TypeKey, values, c.Enabled));
            }
            objects.Add(new TemplateObject(o.Name, o.IsActive, comps));
        }
        return new NodeTemplate(template.Title, objects);
    }

    // Kaynak anahtarla kayıtlı öğeyi bulur (kopya döndürür).
    public static LibraryItem? FindBySource(NodeLibraryState state, string sourceKey)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(sourceKey);
        foreach (var item in state.Items)
        {
            if (string.Equals(item.SourceKey, sourceKey, StringComparison.Ordinal))
                return item.DeepCopy();
        }
        return null;
    }

    // Yeni kayıt ekler (Reusable ile).
    public static NodeLibraryState Save(NodeLibraryState state, string sourceKey, NodeTemplate template, string? folderId, DateTimeOffset now, string? id = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(template);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceKey);
        if (folderId is not null && !state.Folders.Any(f => f.Id == folderId))
            throw new NodeLibraryException(NodeLibraryError.ParentNotFound);
        if (state.Items.Any(i => string.Equals(i.SourceKey, sourceKey, StringComparison.Ordinal)))
            throw new NodeLibraryException(NodeLibraryError.AlreadySaved);
        string newId = string.IsNullOrEmpty(id) ? Guid.NewGuid().ToString("N") : id;
        var reusable = Reusable(template);
        var added = new LibraryItem(newId, folderId, sourceKey, reusable, now);
        var folders = new List<LibraryFolder>(state.Folders);
        var items = CloneItems(state.Items);
        items.Add(added);
        return new NodeLibraryState(folders, items);
    }

    // Kaydın şablonunu günceller.
    public static NodeLibraryState Update(NodeLibraryState state, string itemId, NodeTemplate template, DateTimeOffset now)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(template);
        ArgumentNullException.ThrowIfNull(itemId);
        int index = -1;
        for (int i = 0; i < state.Items.Count; i++)
        {
            if (state.Items[i].Id == itemId)
            {
                index = i;
                break;
            }
        }
        if (index < 0)
            throw new NodeLibraryException(NodeLibraryError.ItemNotFound);
        var reusable = Reusable(template);
        var folders = new List<LibraryFolder>(state.Folders);
        var items = CloneItems(state.Items);
        var old = items[index];
        items[index] = new LibraryItem(old.Id, old.FolderId, old.SourceKey, reusable, now);
        return new NodeLibraryState(folders, items);
    }

    // Kayıt siler (bilinmeyen id değişmez).
    public static NodeLibraryState RemoveItem(NodeLibraryState state, string itemId)
    {
        ArgumentNullException.ThrowIfNull(state);
        bool found = false;
        foreach (var it in state.Items)
        {
            if (it.Id == itemId)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return state;
        var folders = new List<LibraryFolder>(state.Folders);
        var items = new List<LibraryItem>(state.Items.Count);
        foreach (var it in state.Items)
        {
            if (it.Id != itemId)
                items.Add(it.DeepCopy());
        }
        return new NodeLibraryState(folders, items);
    }

    // Kaydı başka klasöre taşır.
    public static NodeLibraryState MoveItem(NodeLibraryState state, string itemId, string? folderId)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(itemId);
        if (folderId is not null && !state.Folders.Any(f => f.Id == folderId))
            throw new NodeLibraryException(NodeLibraryError.ParentNotFound);
        int index = -1;
        for (int i = 0; i < state.Items.Count; i++)
        {
            if (state.Items[i].Id == itemId)
            {
                index = i;
                break;
            }
        }
        if (index < 0)
            throw new NodeLibraryException(NodeLibraryError.ItemNotFound);
        var folders = new List<LibraryFolder>(state.Folders);
        var items = CloneItems(state.Items);
        var old = items[index];
        items[index] = new LibraryItem(old.Id, folderId, old.SourceKey, old.Template, old.SavedAtUtc);
        return new NodeLibraryState(folders, items);
    }

    // Yeni klasör açar.
    public static NodeLibraryState CreateFolder(NodeLibraryState state, string name, string? parentId, string? id = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        string normalized = NormalizeFolderName(name);
        if (normalized.Length == 0)
            throw new NodeLibraryException(NodeLibraryError.InvalidName);
        if (parentId is not null && !state.Folders.Any(f => f.Id == parentId))
            throw new NodeLibraryException(NodeLibraryError.ParentNotFound);
        foreach (var f in state.Folders)
        {
            if (f.ParentId == parentId && NamesEqual(f.Name, normalized))
                throw new NodeLibraryException(NodeLibraryError.DuplicateName);
        }
        string newId = string.IsNullOrEmpty(id) ? Guid.NewGuid().ToString("N") : id;
        var folders = new List<LibraryFolder>(state.Folders.Count + 1);
        folders.AddRange(state.Folders);
        folders.Add(new LibraryFolder(newId, normalized, parentId));
        var items = CloneItems(state.Items);
        return new NodeLibraryState(folders, items);
    }

    // Klasörü yeniden adlandırır ve taşır.
    public static NodeLibraryState MoveFolder(NodeLibraryState state, string id, string? parentId, string name)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(id);
        LibraryFolder? folder = null;
        foreach (var f in state.Folders)
        {
            if (f.Id == id)
            {
                folder = f;
                break;
            }
        }
        if (folder is null)
            throw new NodeLibraryException(NodeLibraryError.FolderNotFound);
        string normalized = NormalizeFolderName(name);
        if (normalized.Length == 0)
            throw new NodeLibraryException(NodeLibraryError.InvalidName);
        string? ancestor = parentId;
        var seen = new HashSet<string>(StringComparer.Ordinal);
        while (ancestor is not null)
        {
            if (ancestor == id || !seen.Add(ancestor))
                throw new NodeLibraryException(NodeLibraryError.CycleNotAllowed);
            LibraryFolder? parent = null;
            foreach (var f in state.Folders)
            {
                if (f.Id == ancestor)
                {
                    parent = f;
                    break;
                }
            }
            if (parent is null)
                throw new NodeLibraryException(NodeLibraryError.ParentNotFound);
            ancestor = parent.ParentId;
        }
        foreach (var f in state.Folders)
        {
            if (f.Id != id && f.ParentId == parentId && NamesEqual(f.Name, normalized))
                throw new NodeLibraryException(NodeLibraryError.DuplicateName);
        }
        var folders = new List<LibraryFolder>(state.Folders.Count);
        foreach (var f in state.Folders)
        {
            if (f.Id == id)
                folders.Add(new LibraryFolder(f.Id, normalized, parentId));
            else
                folders.Add(f);
        }
        var items = CloneItems(state.Items);
        return new NodeLibraryState(folders, items);
    }

    // Klasörü kaldırır; çocukları üst konuma taşır.
    public static NodeLibraryState RemoveFolder(NodeLibraryState state, string id)
    {
        ArgumentNullException.ThrowIfNull(state);
        LibraryFolder? folder = null;
        foreach (var f in state.Folders)
        {
            if (f.Id == id)
            {
                folder = f;
                break;
            }
        }
        if (folder is null)
            return state;
        string? newParent = folder.ParentId;
        var remaining = new List<LibraryFolder>(state.Folders.Count);
        foreach (var f in state.Folders)
        {
            if (f.Id != id && f.ParentId != id)
                remaining.Add(f);
        }
        foreach (var f in state.Folders)
        {
            if (f.ParentId != id)
                continue;
            string candidate = f.Name;
            bool collides = false;
            foreach (var r in remaining)
            {
                if (r.ParentId == newParent && NamesEqual(r.Name, candidate))
                {
                    collides = true;
                    break;
                }
            }
            if (collides)
            {
                string baseName = f.Name.Length > 50 ? f.Name.Substring(0, 50) : f.Name;
                int suffix = 2;
                do
                {
                    candidate = baseName + " (" + suffix.ToString(CultureInfo.InvariantCulture) + ")";
                    suffix++;
                    bool still = false;
                    foreach (var r in remaining)
                    {
                        if (r.ParentId == newParent && NamesEqual(r.Name, candidate))
                        {
                            still = true;
                            break;
                        }
                    }
                    if (!still)
                        break;
                } while (true);
            }
            remaining.Add(new LibraryFolder(f.Id, candidate, newParent));
        }
        var items = new List<LibraryItem>(state.Items.Count);
        foreach (var it in state.Items)
        {
            var clone = it.DeepCopy();
            if (clone.FolderId == id)
                items.Add(new LibraryItem(clone.Id, newParent, clone.SourceKey, clone.Template, clone.SavedAtUtc));
            else
                items.Add(clone);
        }
        return new NodeLibraryState(remaining, items);
    }

    // Kökten klasöre giden yolu döndürür (döngü güvenli).
    public static IReadOnlyList<LibraryFolder> PathTo(NodeLibraryState state, string? folderId)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (folderId is null)
            return Array.Empty<LibraryFolder>();
        var byId = new Dictionary<string, LibraryFolder>(StringComparer.Ordinal);
        foreach (var f in state.Folders)
        {
            if (!byId.ContainsKey(f.Id))
                byId[f.Id] = f;
        }
        var chain = new List<LibraryFolder>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        string? current = folderId;
        while (current is not null)
        {
            if (!seen.Add(current))
                break;
            if (!byId.TryGetValue(current, out var folder))
                break;
            chain.Add(folder);
            current = folder.ParentId;
        }
        chain.Reverse();
        return chain;
    }
}
