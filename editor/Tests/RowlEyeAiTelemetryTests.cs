using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Tools.RowlEye.Ai;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using Xunit;

namespace RowlEngine.Editor.Tests;

public class RowlEyeAiTelemetryTests
{
    [Fact]
    public void AiTelemetryHub_RecordFocus_UpdatesCurrentStateAndHistory()
    {
        var hub = new AiTelemetryHub();

        var req = new AiFocusRequest(
            SessionId: "session-test-1",
            Target: "SettingsDialog",
            ControlPath: "MainWindow/Settings/ResolutionCombo",
            Action: "inspect",
            Reasoning: "Testing resolution drop-down bounds and options.",
            Bounds: new RectDto(100, 200, 150, 32),
            Finding: null,
            Confidence: 0.95
        );

        var evt = hub.RecordFocus(req);

        Assert.True(evt.Seq > 0);
        Assert.Equal(AiTelemetryEventType.FocusChanged, evt.Type);

        var cur = hub.CurrentState;
        Assert.Equal("session-test-1", cur.SessionId);
        Assert.Equal("SettingsDialog", cur.Target);
        Assert.Equal("MainWindow/Settings/ResolutionCombo", cur.FocusedPath);
        Assert.Equal("inspect", cur.LastAction);
        Assert.NotNull(cur.CurrentBounds);
        Assert.Equal(150, cur.CurrentBounds.W);

        var history = hub.GetHistory();
        Assert.NotEmpty(history);
        Assert.Contains(history, h => h.Seq == evt.Seq);
    }

    [Fact]
    public async Task AiTelemetryHub_Subscribe_ReceivesBroadcastEvents()
    {
        var hub = new AiTelemetryHub();
        var (subId, reader) = hub.Subscribe();

        try
        {
            var req = new AiFocusRequest(
                SessionId: "session-sub",
                Target: "MainWindow",
                ControlPath: "MainWindow/PlayButton",
                Action: "click_test",
                Reasoning: "Testing play button interaction."
            );

            hub.RecordFocus(req);

            bool available = await reader.WaitToReadAsync();
            Assert.True(available);

            bool read = reader.TryRead(out var received);
            Assert.True(read);
            Assert.NotNull(received);
            Assert.Equal(AiTelemetryEventType.FocusChanged, received.Type);
        }
        finally
        {
            hub.Unsubscribe(subId);
        }
    }

    [Fact]
    public async Task AiContextBuilder_Build_ExtractsSemanticTreeAndDetectsAnomalies()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();

        var context = await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(() =>
        {
            var (window, _, _) = RowlEyeFixtureFactory.Create(RowlEyeTarget.SettingsDialog, "Dark");
            try
            {
                window.Show();
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                return AiContextBuilder.Build(window, "SettingsDialog", "Dark", 1.0, "test-ai-session");
            }
            finally
            {
                window.Close();
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
            }
        });

        Assert.NotNull(context);
        Assert.NotNull(context.Meta);
        Assert.Equal("SettingsDialog", context.Meta.Target);
        Assert.Equal("Dark", context.Meta.Theme);
        Assert.True(context.Meta.NodeCount > 0);

        Assert.NotNull(context.SemanticTree);
        Assert.NotEmpty(context.SemanticTree.Role);

        // Anomalies collection should exist
        Assert.NotNull(context.Anomalies);
    }
}
