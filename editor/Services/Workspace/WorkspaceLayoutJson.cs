using System;
using System.Buffers;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using System.Text.Json;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Result of loading a saved layout: either a valid <see cref="Layout"/> or an
/// <see cref="Error"/> explaining why the text was rejected. Loading never throws.
/// </summary>
public sealed record WorkspaceLayoutLoadResult(WorkspaceLayout? Layout, string? Error)
{
    /// <summary><c>true</c> when <see cref="Layout"/> is usable.</summary>
    public bool IsValid => Layout is not null;
}

/// <summary>
/// Faz 5 D1 — versioned JSON form of a <see cref="WorkspaceLayout"/>. Compact,
/// deterministic bytes, so save → load → save reproduces the same text:
/// <c>{"version":1,"lastSplitId":2,"tree":{"type":"split","id":"split-2","axis":"x","ratio":0.5,"a":{"type":"leaf","view":"node"},"b":{...}}}</c>.
/// Only the short discriminators <c>"leaf"</c> / <c>"split"</c> are written (no
/// class names), numbers use the invariant culture, and loading is strict:
/// anything unexpected (unknown version, panel, property, bad ratio...) is
/// reported as invalid so the caller can fall back to the default layout.
/// </summary>
public static class WorkspaceLayoutJson
{
    /// <summary>Current format version; bump together with a migration when the shape changes.</summary>
    public const int CurrentVersion = 1;

    private const int MaxDepth = 64;

    public static string Serialize(WorkspaceLayout layout)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteNumber("version", CurrentVersion);
            writer.WriteNumber("lastSplitId", layout.LastSplitId);
            writer.WritePropertyName("tree");
            WriteNode(writer, layout.Tree);
            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(buffer.WrittenSpan);
    }

    private static void WriteNode(Utf8JsonWriter writer, LayoutNode? node)
    {
        switch (node)
        {
            case null:
                writer.WriteNullValue();
                break;
            case LeafNode leaf:
                writer.WriteStartObject();
                writer.WriteString("type", "leaf");
                writer.WriteString("view", leaf.View);
                writer.WriteEndObject();
                break;
            case SplitNode split:
                writer.WriteStartObject();
                writer.WriteString("type", "split");
                writer.WriteString("id", split.Id);
                writer.WriteString("axis", split.Axis == SplitAxis.Horizontal ? "x" : "y");
                writer.WriteNumber("ratio", split.Ratio);
                writer.WritePropertyName("a");
                WriteNode(writer, split.A);
                writer.WritePropertyName("b");
                WriteNode(writer, split.B);
                writer.WriteEndObject();
                break;
            default:
                throw new ArgumentException("Unknown layout node kind.", nameof(node));
        }
    }

    /// <summary>
    /// Parses and validates saved layout text. Never throws: malformed or
    /// semantically invalid input yields <see cref="WorkspaceLayoutLoadResult.IsValid"/> = false.
    /// </summary>
    public static WorkspaceLayoutLoadResult Parse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
            return Fail("empty text");
        try
        {
            using JsonDocument document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = MaxDepth });
            return ReadLayout(document.RootElement);
        }
        catch (JsonException ex)
        {
            return Fail("not valid JSON: " + ex.Message);
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or FormatException or OverflowException)
        {
            return Fail("unreadable layout: " + ex.Message);
        }
    }

    private static WorkspaceLayoutLoadResult Fail(string error) => new(null, error);

    private static WorkspaceLayoutLoadResult ReadLayout(JsonElement root)
    {
        if (root.ValueKind != JsonValueKind.Object)
            return Fail("root is not an object");

        bool hasVersion = false, hasLast = false, hasTree = false;
        int version = 0, last = 0;
        LayoutNode? tree = null;
        string? treeError = null;

        foreach (JsonProperty property in root.EnumerateObject())
        {
            switch (property.Name)
            {
                case "version":
                    if (hasVersion) return Fail("duplicate version");
                    hasVersion = true;
                    if (property.Value.ValueKind != JsonValueKind.Number || !property.Value.TryGetInt32(out version))
                        return Fail("version is not an integer");
                    break;
                case "lastSplitId":
                    if (hasLast) return Fail("duplicate lastSplitId");
                    hasLast = true;
                    if (property.Value.ValueKind != JsonValueKind.Number || !property.Value.TryGetInt32(out last))
                        return Fail("lastSplitId is not an integer");
                    break;
                case "tree":
                    if (hasTree) return Fail("duplicate tree");
                    hasTree = true;
                    if (property.Value.ValueKind != JsonValueKind.Null)
                        tree = ReadNode(property.Value, out treeError);
                    break;
                default:
                    return Fail("unknown property '" + property.Name + "'");
            }
        }

        if (!hasVersion || !hasLast || !hasTree)
            return Fail("missing version, lastSplitId or tree");
        if (version != CurrentVersion)
            return Fail("unsupported version " + version.ToString(CultureInfo.InvariantCulture));
        if (treeError is not null)
            return Fail(treeError);

        var layout = new WorkspaceLayout(tree, last);
        string? violation = layout.Validate();
        return violation is null ? new WorkspaceLayoutLoadResult(layout, null) : Fail(violation);
    }

    private static LayoutNode? ReadNode(JsonElement element, out string? error)
    {
        error = null;
        if (element.ValueKind != JsonValueKind.Object)
        {
            error = "tree node is not an object";
            return null;
        }

        string? type = null, view = null, id = null, axisText = null;
        double ratio = 0;
        bool hasRatio = false;
        LayoutNode? a = null, b = null;
        var seen = new HashSet<string>(StringComparer.Ordinal);

        foreach (JsonProperty property in element.EnumerateObject())
        {
            if (!seen.Add(property.Name))
            {
                error = "duplicate node property '" + property.Name + "'";
                return null;
            }
            JsonElement value = property.Value;
            switch (property.Name)
            {
                case "type":
                case "view":
                case "id":
                case "axis":
                    if (value.ValueKind != JsonValueKind.String)
                    {
                        error = "'" + property.Name + "' is not a string";
                        return null;
                    }
                    string text = value.GetString()!;
                    if (property.Name == "type") type = text;
                    else if (property.Name == "view") view = text;
                    else if (property.Name == "id") id = text;
                    else axisText = text;
                    break;
                case "ratio":
                    if (value.ValueKind != JsonValueKind.Number || !value.TryGetDouble(out ratio))
                    {
                        error = "ratio is not a number";
                        return null;
                    }
                    hasRatio = true;
                    break;
                case "a":
                    a = ReadChild(value, out error);
                    if (error is not null) return null;
                    break;
                case "b":
                    b = ReadChild(value, out error);
                    if (error is not null) return null;
                    break;
                default:
                    error = "unknown node property '" + property.Name + "'";
                    return null;
            }
        }

        if (type == "leaf")
        {
            if (view is null || id is not null || axisText is not null || hasRatio || a is not null || b is not null || seen.Count != 2)
            {
                error = "leaf must contain exactly type and view";
                return null;
            }
            return new LeafNode(view);
        }

        if (type == "split")
        {
            SplitAxis? axis = axisText switch { "x" => SplitAxis.Horizontal, "y" => SplitAxis.Vertical, _ => null };
            if (id is null || axis is null || !hasRatio || a is null || b is null || view is not null || seen.Count != 6)
            {
                error = "split must contain exactly type, id, axis (x|y), ratio, a and b";
                return null;
            }
            return new SplitNode(id, axis.Value, ratio, a, b);
        }

        error = "unknown node type";
        return null;
    }

    private static LayoutNode? ReadChild(JsonElement value, out string? error)
    {
        if (value.ValueKind == JsonValueKind.Null)
        {
            error = "split child is null";
            return null;
        }
        return ReadNode(value, out error);
    }
}
