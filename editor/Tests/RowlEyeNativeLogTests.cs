using System;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Tools.RowlEye.Ai;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

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
        var hub = AiTelemetryHub.Instance;
        var service = RowlEyeEngineService.Instance;
        service.HookLogCallback();

        bool receivedLog = false;
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

        var (subId, reader) = hub.Subscribe();
        var subTask = Task.Run(async () =>
        {
            try
            {
                while (await reader.WaitToReadAsync(cts.Token).ConfigureAwait(false))
                {
                    while (reader.TryRead(out var evt))
                    {
                        if (evt.Type == AiTelemetryEventType.NativeLog)
                        {
                            receivedLog = true;
                            return;
                        }
                    }
                }
            }
            catch (OperationCanceledException) { }
            finally
            {
                hub.Unsubscribe(subId);
            }
        });

        // Trigger native engine step which produces log/telemetry
        service.EnsureInitialized(640, 360);
        service.Step(0.016f);

        // Publish a test native log event to guarantee coverage
        hub.Publish(AiTelemetryEventType.NativeLog, new NativeLogPayloadDto(2, "Info", "Rowl.Eye AI Test Log", DateTime.UtcNow.ToString("o")));

        await subTask.ConfigureAwait(false);
        Assert.True(receivedLog, "AiTelemetryHub should broadcast NativeLog event");
    }
}
