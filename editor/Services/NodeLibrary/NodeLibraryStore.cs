using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Services.NodeLibrary;

// Yükleme sonucu (durum + salt-okunur + uyarı).
public sealed record NodeLibraryLoadResult(NodeLibraryState State, bool ReadOnly, string? Warning);

// JSON dosya deposu (snake_case, girintili).
public static class NodeLibraryStore
{
    public const string FileName = "node-library.json";
    public const int SchemaVersion = 1;

    public static NodeLibraryLoadResult Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        if (!File.Exists(path))
            return new NodeLibraryLoadResult(NodeLibraryState.Empty, false, null);

        string text;
        try
        {
            text = File.ReadAllText(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return Corrupt(path, "Node library file could not be read; moved to backup and started empty.");
        }

        JsonNode? root;
        try
        {
            root = JsonNode.Parse(text);
        }
        catch (Exception ex) when (ex is JsonException or ArgumentException)
        {
            return Corrupt(path, "Node library file was corrupt; moved to backup and started empty.");
        }

        if (root is not JsonObject obj)
            return Corrupt(path, "Node library file has an invalid root; moved to backup and started empty.");

        int schemaVersion = 1;
        bool readOnly = false;
        if (obj.TryGetPropertyValue("schema_version", out JsonNode? versionNode) && versionNode is not null)
        {
            try
            {
                schemaVersion = versionNode.GetValue<int>();
            }
            catch (Exception ex) when (ex is InvalidOperationException or FormatException or ArgumentException)
            {
                return Corrupt(path, "Node library file has an invalid schema version; moved to backup and started empty.");
            }
            if (schemaVersion > SchemaVersion)
                readOnly = true;
        }

        var repairs = new List<string>();
        var folders = new List<LibraryFolder>();
        var folderIds = new HashSet<string>(StringComparer.Ordinal);

        if (obj.TryGetPropertyValue("folders", out JsonNode? foldersNode) && foldersNode is not null)
        {
            if (foldersNode is JsonArray farr)
            {
                foreach (var entry in farr)
                {
                    if (entry is not JsonObject fo)
                    {
                        repairs.Add("Skipped a folder entry that is not an object.");
                        continue;
                    }
                    string? fid = GetString(fo, "id");
                    string? fname = GetString(fo, "name");
                    if (string.IsNullOrWhiteSpace(fid) || string.IsNullOrWhiteSpace(fname))
                    {
                        repairs.Add("Skipped a folder entry with missing id or name.");
                        continue;
                    }
                    if (!folderIds.Add(fid!))
                    {
                        repairs.Add("Skipped a folder entry with a duplicate id.");
                        continue;
                    }
                    string? parentId = GetString(fo, "parent_id");
                    if (string.IsNullOrWhiteSpace(parentId))
                        parentId = null;
                    folders.Add(new LibraryFolder(fid!, fname!, parentId));
                }
            }
            else
            {
                repairs.Add("Ignored a folders section that is not an array.");
            }
        }

        for (int i = 0; i < folders.Count; i++)
        {
            var f = folders[i];
            if (f.ParentId is not null && !folderIds.Contains(f.ParentId))
            {
                folders[i] = new LibraryFolder(f.Id, f.Name, null);
                repairs.Add("Re-rooted a folder with a dangling parent.");
            }
        }

        var items = new List<LibraryItem>();
        var itemIds = new HashSet<string>(StringComparer.Ordinal);

        if (obj.TryGetPropertyValue("items", out JsonNode? itemsNode) && itemsNode is not null)
        {
            if (itemsNode is JsonArray iarr)
            {
                foreach (var entry in iarr)
                {
                    if (entry is not JsonObject io)
                    {
                        repairs.Add("Skipped an item entry that is not an object.");
                        continue;
                    }
                    string? iid = GetString(io, "id");
                    string? sourceKey = GetString(io, "source_key");
                    if (string.IsNullOrWhiteSpace(iid) || string.IsNullOrWhiteSpace(sourceKey))
                    {
                        repairs.Add("Skipped an item entry with missing id or source key.");
                        continue;
                    }
                    if (!itemIds.Add(iid!))
                    {
                        repairs.Add("Skipped an item entry with a duplicate id.");
                        continue;
                    }
                    string? folderId = GetString(io, "folder_id");
                    if (string.IsNullOrWhiteSpace(folderId))
                        folderId = null;
                    else if (!folderIds.Contains(folderId))
                    {
                        folderId = null;
                        repairs.Add("Re-rooted an item with a dangling folder.");
                    }
                    DateTimeOffset savedAt = DateTimeOffset.UnixEpoch;
                    string? rawDate = GetString(io, "saved_at_utc");
                    if (rawDate is not null && DateTimeOffset.TryParse(rawDate, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var parsed))
                        savedAt = parsed;
                    else
                        repairs.Add("Repaired an item with a missing or invalid saved_at_utc.");
                    if (io["template"] is not JsonObject tObj)
                    {
                        repairs.Add("Skipped an item entry with a missing template.");
                        itemIds.Remove(iid!);
                        continue;
                    }
                    string title = GetString(tObj, "title") ?? string.Empty;
                    var templateObjects = new List<TemplateObject>();
                    if (tObj.TryGetPropertyValue("objects", out JsonNode? objectsNode) && objectsNode is JsonArray oarr)
                    {
                        foreach (var oe in oarr)
                        {
                            if (oe is not JsonObject oo)
                            {
                                repairs.Add("Skipped a template object that is not an object.");
                                continue;
                            }
                            string? oname = GetString(oo, "name");
                            if (string.IsNullOrWhiteSpace(oname))
                            {
                                repairs.Add("Skipped a template object with a missing name.");
                                continue;
                            }
                            bool isActive = true;
                            if (oo.TryGetPropertyValue("is_active", out JsonNode? activeNode) && activeNode is not null)
                            {
                                try { isActive = activeNode.GetValue<bool>(); }
                                catch (Exception ex) when (ex is InvalidOperationException or FormatException or ArgumentException) { isActive = true; }
                            }
                            var comps = new List<TemplateComponent>();
                            if (oo.TryGetPropertyValue("components", out JsonNode? compsNode) && compsNode is JsonArray carr)
                            {
                                foreach (var ce in carr)
                                {
                                    if (ce is not JsonObject co)
                                        continue;
                                    string? type = GetString(co, "type");
                                    if (string.IsNullOrWhiteSpace(type))
                                        continue;
                                    JsonObject values;
                                    if (co["values"] is JsonObject vo)
                                        values = (JsonObject)vo.DeepClone();
                                    else
                                        values = new JsonObject();
                                    comps.Add(new TemplateComponent(type!, values, co["enabled"] is JsonValue ev && ev.TryGetValue(out bool en) ? en : true));
                                }
                            }
                            templateObjects.Add(new TemplateObject(oname!, isActive, comps));
                        }
                    }
                    var template = new NodeTemplate(title, templateObjects);
                    items.Add(new LibraryItem(iid!, folderId, sourceKey!, template, savedAt));
                }
            }
            else
            {
                repairs.Add("Ignored an items section that is not an array.");
            }
        }

        var state = new NodeLibraryState(folders, items);
        string? warning = null;
        if (readOnly)
            warning = "Node library was written by a newer editor version (" + schemaVersion.ToString(CultureInfo.InvariantCulture) + "); opened read-only.";
        if (repairs.Count > 0)
        {
            string repairText = "Repaired " + repairs.Count.ToString(CultureInfo.InvariantCulture) + " invalid entries in the node library.";
            warning = warning is null ? repairText : warning + " " + repairText;
        }
        return new NodeLibraryLoadResult(state, readOnly, warning);
    }

    public static void Save(string path, NodeLibraryState state)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(state);
        if (File.Exists(path))
        {
            try
            {
                string existing = File.ReadAllText(path);
                JsonNode? root = JsonNode.Parse(existing);
                if (root is JsonObject obj && obj.TryGetPropertyValue("schema_version", out JsonNode? vn) && vn is not null)
                {
                    int ver = SchemaVersion;
                    bool parsed = false;
                    try
                    {
                        ver = vn.GetValue<int>();
                        parsed = true;
                    }
                    catch (Exception ex) when (ex is InvalidOperationException or FormatException or ArgumentException)
                    {
                        parsed = false;
                    }
                    if (parsed && ver > SchemaVersion)
                        throw new InvalidOperationException("Node library was written by a newer editor version; refusing to overwrite.");
                }
            }
            catch (InvalidOperationException)
            {
                throw;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
            {
                // Bozuk ya da sürümsüz dosya üzerine yazılabilir.
            }
        }

        var foldersArr = new JsonArray();
        foreach (var f in state.Folders)
        {
            var fo = new JsonObject
            {
                ["id"] = f.Id,
                ["name"] = f.Name
            };
            fo["parent_id"] = f.ParentId is null ? null : JsonValue.Create(f.ParentId);
            foldersArr.Add(fo);
        }
        var itemsArr = new JsonArray();
        foreach (var it in state.Items)
        {
            var objectsArr = new JsonArray();
            foreach (var o in it.Template.Objects)
            {
                var compsArr = new JsonArray();
                foreach (var c in o.Components)
                {
                    var co = new JsonObject
                    {
                        ["type"] = c.TypeKey,
                        ["enabled"] = c.Enabled,
                        ["values"] = (JsonObject)c.Values.DeepClone()
                    };
                    compsArr.Add(co);
                }
                var oo = new JsonObject
                {
                    ["name"] = o.Name,
                    ["is_active"] = o.IsActive,
                    ["components"] = compsArr
                };
                objectsArr.Add(oo);
            }
            var tObj = new JsonObject
            {
                ["title"] = it.Template.Title,
                ["objects"] = objectsArr
            };
            var io = new JsonObject
            {
                ["id"] = it.Id,
                ["source_key"] = it.SourceKey,
                ["saved_at_utc"] = it.SavedAtUtc.ToString("o", CultureInfo.InvariantCulture),
                ["template"] = tObj
            };
            io["folder_id"] = it.FolderId is null ? null : JsonValue.Create(it.FolderId);
            itemsArr.Add(io);
        }
        var rootOut = new JsonObject
        {
            ["schema_version"] = SchemaVersion,
            ["folders"] = foldersArr,
            ["items"] = itemsArr
        };
        string json = rootOut.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
        ProjectFileSystem.WriteAllTextAtomically(path, json);
    }

    private static string? GetString(JsonObject obj, string key)
    {
        if (!obj.TryGetPropertyValue(key, out JsonNode? node) || node is null)
            return null;
        try { return node.GetValue<string>(); }
        catch (Exception ex) when (ex is InvalidOperationException or FormatException or ArgumentException)
        { return null; }
    }

    private static NodeLibraryLoadResult Corrupt(string path, string warning)
    {
        try
        {
            string stamp = DateTime.UtcNow.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture);
            string backup = path + ".corrupt-" + stamp;
            if (File.Exists(backup))
            {
                int n = 2;
                while (File.Exists(backup + "." + n.ToString(CultureInfo.InvariantCulture)))
                    n++;
                backup = backup + "." + n.ToString(CultureInfo.InvariantCulture);
            }
            File.Move(path, backup, overwrite: false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            // Yedek taşınamazsa bile boş durum döndür.
        }
        return new NodeLibraryLoadResult(NodeLibraryState.Empty, false, warning);
    }
}
