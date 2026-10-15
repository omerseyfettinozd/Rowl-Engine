using System;
using System.Text.Json;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

public sealed class RowlEyeAudioTelemetryTests
{
    [Fact]
    public void GetAudioStatsJson_Returns_ValidStructure_With_Metrics()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        string json = service.GetAudioStatsJson();
        Assert.NotNull(json);

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("available", out var availProp), "Audio stats should contain 'available'");
        if (availProp.GetBoolean())
        {
            Assert.True(root.TryGetProperty("volumes", out var volProp), "Should contain 'volumes' dictionary");
            Assert.True(volProp.TryGetProperty("master", out _), "Volumes should have master");
            Assert.True(volProp.TryGetProperty("bgm", out _), "Volumes should have bgm");

            Assert.True(root.TryGetProperty("metrics", out var metProp), "Should contain 'metrics' object");
            Assert.True(metProp.TryGetProperty("estimatedRms", out _), "Metrics should have estimatedRms");
            Assert.True(metProp.TryGetProperty("clippingRisk", out _), "Metrics should have clippingRisk");
            Assert.True(metProp.TryGetProperty("isSilent", out _), "Metrics should have isSilent");
        }
    }
}
