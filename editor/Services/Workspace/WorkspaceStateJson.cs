using System;
using System.Buffers;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Result of loading a saved workspace: a valid <see cref="State"/> or an
/// <see cref="Error"/> saying why the text was rejected. Loading never throws.
/// </summary>
public sealed record WorkspaceStateLoadResult(EditorWorkspaceState? State, string? Error)
{
    public bool IsValid => State is not null;
}

/// <summary>
/// Faz 5 D5 — versioned JSON form of the whole <see cref="EditorWorkspaceState"/>
/// (the part of the user's workspace that outlives a session): the D1 split
/// tree, the bottom strip's tabs, the pinned panels and the remembered side
/// panel shares.
/// <code>
/// {"version":1,"layout":{...D1 layout document...},
///  "bottom":{"open":[0,1],"active":0},"pinned":["node"],"shares":{"hierarchy":0.2}}
/// </code>
/// Compact and deterministic (save → load → save gives the same text). Loading
/// is strict, like <see cref="WorkspaceLayoutJson"/>: an unknown version or
/// property, a panel this editor does not host, an inconsistent bottom strip
/// (strip leaf without open tabs or the reverse), a pinned panel that is not in
/// the tree... is reported as invalid so the caller falls back to the default
/// workspace instead of showing a half-broken one.
/// </summary>
public static class WorkspaceStateJson
{
    /// <summary>Current format version; bump together with a migration when the shape changes.</summary>
    public const int CurrentVersion = 1;

    private const int MaxDepth = 80;

    public static string Serialize(EditorWorkspaceState state)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteNumber("version", CurrentVersion);
            writer.WritePropertyName("layout");
            writer.WriteRawValue(WorkspaceLayoutJson.Serialize(state.Layout), skipInputValidation: true);

            writer.WritePropertyName("bottom");
            writer.WriteStartObject();
            writer.WritePropertyName("open");
            writer.WriteStartArray();
            for (int i = 0; i < BottomStripState.TabCount; i++)
            {
                if (state.Bottom.IsOpen(i))
                    writer.WriteNumberValue(i);
            }
            writer.WriteEndArray();
            writer.WriteNumber("active", state.Bottom.ActiveTab);
            writer.WriteEndObject();

            writer.WritePropertyName("pinned");
            writer.WriteStartArray();
            foreach (string id in state.Pinned)
                writer.WriteStringValue(id);
            writer.WriteEndArray();

            writer.WritePropertyName("shares");
            writer.WriteStartObject();
            foreach (KeyValuePair<string, double> share in state.Shares)
                writer.WriteNumber(share.Key, share.Value);
            writer.WriteEndObject();

            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(buffer.WrittenSpan);
    }

    /// <summary>
    /// Parses and validates saved workspace text. Never throws: malformed or
    /// semantically invalid input yields <see cref="WorkspaceStateLoadResult.IsValid"/> = false.
    /// </summary>
    public static WorkspaceStateLoadResult Parse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
            return Fail("empty text");
        try
        {
            using JsonDocument document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = MaxDepth });
            return Read(document.RootElement);
        }
        catch (JsonException ex)
        {
            return Fail("not valid JSON: " + ex.Message);
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or FormatException or OverflowException)
        {
            return Fail("unreadable workspace: " + ex.Message);
        }
    }

    private static WorkspaceStateLoadResult Fail(string error) => new(null, error);

    private static WorkspaceStateLoadResult Read(JsonElement root)
    {
        if (root.ValueKind != JsonValueKind.Object)
            return Fail("root is not an object");

        var seen = new HashSet<string>(StringComparer.Ordinal);
        int version = 0;
        WorkspaceLayout? layout = null;
        IReadOnlySet<string>? originalViews = null;
        IReadOnlySet<string>? prunedViews = null;
        BottomStripState? bottom = null;
        PanelSet? pinned = null;
        PanelShares? shares = null;

        foreach (JsonProperty property in root.EnumerateObject())
        {
            if (!seen.Add(property.Name))
                return Fail("duplicate property '" + property.Name + "'");
            string? error;
            switch (property.Name)
            {
                case "version":
                    if (property.Value.ValueKind != JsonValueKind.Number || !property.Value.TryGetInt32(out version))
                        return Fail("version is not an integer");
                    break;
                case "layout":
                    WorkspaceLayoutJson.PrunedLayoutLoadResult loaded = WorkspaceLayoutJson.ParseCore(
                        property.Value.GetRawText(),
                        id => PanelIds.Editor.Contains(id, StringComparer.Ordinal));
                    if (!loaded.IsValid)
                        return Fail("layout: " + loaded.Error);
                    layout = loaded.Layout;
                    originalViews = loaded.OriginalViews;
                    prunedViews = loaded.PrunedViews;
                    break;
                case "bottom":
                    bottom = ReadBottom(property.Value, out error);
                    if (error is not null) return Fail(error);
                    break;
                case "pinned":
                    pinned = ReadPinned(property.Value, out error);
                    if (error is not null) return Fail(error);
                    break;
                case "shares":
                    shares = ReadShares(property.Value, out error);
                    if (error is not null) return Fail(error);
                    break;
                default:
                    return Fail("unknown property '" + property.Name + "'");
            }
        }

        if (seen.Contains("version") && version != CurrentVersion)
            return Fail("unsupported version " + version.ToString(CultureInfo.InvariantCulture));
        if (seen.Count != 5 || layout is null || originalViews is null || prunedViews is null || bottom is null || pinned is null || shares is null)
            return Fail("missing version, layout, bottom, pinned or shares");

        return Validate(layout, bottom, pinned, shares, originalViews, prunedViews);
    }

    private static WorkspaceStateLoadResult Validate(
        WorkspaceLayout layout,
        BottomStripState bottom,
        PanelSet pinned,
        PanelShares shares,
        IReadOnlySet<string> originalViews,
        IReadOnlySet<string> prunedViews)
    {
        if (layout.Tree is null)
            return Fail("layout has no panel");
        IReadOnlyList<string> views = layout.Views;
        foreach (string view in views)
        {
            // The D1 model also knows prototype-only ids; a saved editor workspace may only use hosted panels.
            if (!PanelIds.Editor.Contains(view, StringComparer.Ordinal))
                return Fail("panel '" + view + "' is not hosted by the editor");
        }
        if (originalViews.Contains(PanelIds.Bottom) != bottom.AnyOpen)
            return Fail("bottom strip leaf and open tabs disagree");
        var keptPinned = new List<string>();
        foreach (string id in pinned)
        {
            if (views.Contains(id, StringComparer.Ordinal))
                keptPinned.Add(id);
            else if (!prunedViews.Contains(id))
                return Fail("pinned panel '" + id + "' is not in the layout");
        }

        // A selected tab that is not open would show a headerless page: select the first open one.
        BottomStripState normalized = bottom.AnyOpen && !bottom.IsOpen(bottom.ActiveTab)
            ? bottom with { ActiveTab = bottom.FirstOpenTab() }
            : bottom;

        var state = new EditorWorkspaceState(layout, normalized) { Pinned = PanelSet.Of(keptPinned), Shares = shares };
        return new WorkspaceStateLoadResult(state, null);
    }

    private static BottomStripState? ReadBottom(JsonElement element, out string? error)
    {
        error = null;
        if (element.ValueKind != JsonValueKind.Object)
        {
            error = "bottom is not an object";
            return null;
        }
        BottomTabs open = BottomTabs.None;
        int active = 0;
        bool hasOpen = false, hasActive = false;
        foreach (JsonProperty property in element.EnumerateObject())
        {
            switch (property.Name)
            {
                case "open" when !hasOpen:
                    hasOpen = true;
                    if (property.Value.ValueKind != JsonValueKind.Array)
                    {
                        error = "bottom.open is not an array";
                        return null;
                    }
                    foreach (JsonElement tab in property.Value.EnumerateArray())
                    {
                        if (tab.ValueKind != JsonValueKind.Number || !tab.TryGetInt32(out int index)
                            || index < 0 || index >= BottomStripState.TabCount
                            || (open & BottomStripState.Flag(index)) != 0)
                        {
                            error = "bottom.open holds an invalid or repeated tab index";
                            return null;
                        }
                        open |= BottomStripState.Flag(index);
                    }
                    break;
                case "active" when !hasActive:
                    hasActive = true;
                    if (property.Value.ValueKind != JsonValueKind.Number || !property.Value.TryGetInt32(out active)
                        || active < 0 || active >= BottomStripState.TabCount)
                    {
                        error = "bottom.active is not a tab index";
                        return null;
                    }
                    break;
                default:
                    error = "unknown or duplicate bottom property '" + property.Name + "'";
                    return null;
            }
        }
        if (!hasOpen || !hasActive)
        {
            error = "bottom must contain open and active";
            return null;
        }
        return new BottomStripState(open, active);
    }

    private static PanelSet? ReadPinned(JsonElement element, out string? error)
    {
        error = null;
        if (element.ValueKind != JsonValueKind.Array)
        {
            error = "pinned is not an array";
            return null;
        }
        var ids = new List<string>();
        foreach (JsonElement item in element.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String || item.GetString() is not { } id || ids.Contains(id, StringComparer.Ordinal))
            {
                error = "pinned holds a non-string or repeated panel id";
                return null;
            }
            ids.Add(id);
        }
        return PanelSet.Of(ids);
    }

    private static PanelShares? ReadShares(JsonElement element, out string? error)
    {
        error = null;
        if (element.ValueKind != JsonValueKind.Object)
        {
            error = "shares is not an object";
            return null;
        }
        PanelShares shares = PanelShares.Empty;
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (JsonProperty property in element.EnumerateObject())
        {
            if (!seen.Add(property.Name) || !PanelShares.Remembered.Contains(property.Name, StringComparer.Ordinal))
            {
                error = "shares holds an unknown or repeated panel '" + property.Name + "'";
                return null;
            }
            if (property.Value.ValueKind != JsonValueKind.Number || !property.Value.TryGetDouble(out double share)
                || !(share >= WorkspaceLayout.MinRatio && share <= WorkspaceLayout.MaxRatio))
            {
                error = "share of '" + property.Name + "' is outside the allowed range";
                return null;
            }
            shares = shares.With(property.Name, share);
        }
        return shares;
    }
}
