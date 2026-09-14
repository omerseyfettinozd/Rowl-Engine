using System;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

public sealed class EditorTextShapingSlice3Tests
{
    [Fact]
    public void NativeCapability_MatchesOneShiftNine()
    {
        Assert.Equal(512UL, TextShapingService.NativeCapabilityTextShaping);
    }

    [Fact]
    public void NativeBridge_ReturnsClusteredShapingJson()
    {
        string root = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
        byte[] font = File.ReadAllBytes(Path.Combine(root, "Assets", "fonts", "default.ttf"));
        Assert.True(TextShapingService.TryShapeNativeJson(
            "e\u0301 <b>metin</b>", font, 24.0f, 240.0f, "tr", out string? json),
            "Native shaping library must be loaded for the bridge gate.");
        Assert.NotNull(json);
        using JsonDocument document = JsonDocument.Parse(json);
        JsonElement rootElement = document.RootElement;
        Assert.Equal("harfbuzz_freetype_fribidi_unibreak",
            rootElement.GetProperty("backend").GetString());
        Assert.Equal("e\u0301 metin", rootElement.GetProperty("plain_text").GetString());
        Assert.Equal(7, rootElement.GetProperty("reveal_count").GetInt32());
        Assert.NotEmpty(rootElement.GetProperty("glyphs").EnumerateArray());
    }
}
