using System;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Tools.RowlEye.Ai;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class RowlEyeNativeLogTests
{
    [Fact]
    public void HookLogCallback_And_GetRecentLogsJson_ReturnsValidJsonArray()
    {
        var service = RowlEyeEngineService.Instance;
        service.HookLogCallback();

        string json = service.GetRecentLogsJson(minLevel: 0, maxCount: 50);
        Assert.NotNull(json);

        // Verify valid JSON
        using var doc = JsonDocument.Parse(json);
        Assert.Equal(JsonValueKind.Array, doc.RootElement.ValueKind);
    }

    [Fact]
    public async Task NativeLogReceived_Publishes_To_TelemetryHub()
    {
        const string marker = "seq_since/dirty_only filter rejected";
        var hub = AiTelemetryHub.Instance;
        var service = RowlEyeEngineService.Instance;
        service.HookLogCallback();
        service.EnsureInitialized(640, 360);

        // Abonelik tetikten ÖNCE: istemci kanalı tamponludur, olay kaçmaz.
        var (subId, reader) = hub.Subscribe();
        try
        {
            // Gerçek native yol: reddedilen filtre motor içinde WARN üretir →
            // Logger callback → OnNativeLogReceived → hub. Test olayı kendisi
            // YAYINLAMAZ; eski hali sahte bir NativeLog yayınladığından
            // callback köprüsü kırıkken de yeşildi.
            Assert.Equal("{}", service.QueryWorldJson("{\"seq_since\":1}"));

            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            try
            {
                while (await reader.WaitToReadAsync(cts.Token))
                {
                    while (reader.TryRead(out var evt))
                    {
                        if (evt.Type == AiTelemetryEventType.NativeLog &&
                            evt.Payload is NativeLogPayloadDto log &&
                            log.Message.Contains(marker, StringComparison.Ordinal))
                        {
                            Assert.Equal(3, log.Level); // WARN
                            return;
                        }
                    }
                }
            }
            catch (OperationCanceledException)
            {
            }
            Assert.Fail("Native WARN did not reach AiTelemetryHub through the log callback.");
        }
        finally
        {
            hub.Unsubscribe(subId);
        }
    }
}
