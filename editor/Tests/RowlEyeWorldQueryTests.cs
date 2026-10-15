using System;
using System.Text.Json;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class RowlEyeWorldQueryTests : IDisposable
{
    public void Dispose()
    {
        RowlEyeEngineService.Instance.Shutdown();
    }
    [Fact]
    public void QueryWorldJson_Returns_ValidSnapshot_With_Story_And_Anomalies()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        string json = service.QueryWorldJson("{\"compact\":true,\"anomalies\":true}");
        Assert.NotNull(json);

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("seq", out var seqProp), "WorldQuery should include seq");
        Assert.True(seqProp.GetInt64() > 0, "seq should be positive");

        Assert.True(root.TryGetProperty("story", out var storyProp), "WorldQuery should include story");
        Assert.True(storyProp.TryGetProperty("spk", out _), "Story should have spk field");
        Assert.True(storyProp.TryGetProperty("dlg", out _), "Story should have dlg field");
        Assert.True(storyProp.TryGetProperty("bg", out _), "Story should have bg field");

        Assert.True(root.TryGetProperty("vars", out _), "WorldQuery should include vars object");
        Assert.True(root.TryGetProperty("anom", out var anomProp), "WorldQuery should include anom array");
        Assert.Equal(JsonValueKind.Array, anomProp.ValueKind);
    }

    [Fact]
    public void InjectInputJson_Executes_Tap_And_Choice_Without_Error()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        // Test tap injection
        bool tapOk = service.InjectInputJson("{\"t\":\"tap\",\"x\":640.0,\"y\":360.0}");
        Assert.True(tapOk, "Injecting tap event should succeed");

        // Test advance / choice injection
        bool advanceOk = service.InjectInputJson("[{\"t\":\"advance\"}]");
        Assert.True(advanceOk, "Injecting advance sequence should succeed");

        // Test invalid JSON degrades safely
        bool badJsonOk = service.InjectInputJson("invalid json text");
        Assert.False(badJsonOk, "Malformed JSON should return false");
    }
}
