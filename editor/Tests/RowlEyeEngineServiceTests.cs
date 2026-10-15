using System;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public class RowlEyeEngineServiceTests : IDisposable
{
    public void Dispose()
    {
        RowlEyeEngineService.Instance.Shutdown();
    }
    [Fact]
    public void RowlEyeEngineService_LifecycleAndContext_WorksCorrectly()
    {
        var svc = RowlEyeEngineService.Instance;
        svc.EnsureInitialized(640, 360);

        Assert.True(svc.IsInitialized);

        var ctx = svc.GetContext();
        Assert.NotNull(ctx);
        Assert.True(ctx.IsInitialized);
        Assert.Equal(2, ctx.Resolution.Length);
        Assert.Equal(640, ctx.Resolution[0]);
        Assert.Equal(360, ctx.Resolution[1]);

        ulong initialFrameId = ctx.FrameId;
        svc.Step(0.016f);

        var ctxAfterStep = svc.GetContext();
        Assert.True(ctxAfterStep.FrameId > initialFrameId);
    }

    [Fact]
    public void RowlEyeEngineService_CaptureFrame_ReturnsValidEncodedBytes()
    {
        var svc = RowlEyeEngineService.Instance;
        svc.EnsureInitialized(640, 360);

        byte[]? jpegBytes = svc.CaptureFrame(320, "jpeg", 75);
        Assert.NotNull(jpegBytes);
        Assert.NotEmpty(jpegBytes);

        // JPEG header check: 0xFF 0xD8
        Assert.True(jpegBytes.Length > 4);
        Assert.Equal(0xFF, jpegBytes[0]);
        Assert.Equal(0xD8, jpegBytes[1]);
    }
}
