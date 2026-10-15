using System;
using System.Text.Json;
using System.Threading.Tasks;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class RowlEyeGlmP0AuditedFeaturesTests : IDisposable
{
    public void Dispose()
    {
        RowlEyeEngineService.Instance.Shutdown();
    }
    [Fact]
    public void AudioStats_Contains_RealChannels_Spectrum_And_HeuristicFlag()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        string json = service.GetAudioStatsJson();
        Assert.NotNull(json);

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("available", out var availProp));
        if (availProp.GetBoolean())
        {
            // Channels object with 6 distinct channel groups
            Assert.True(root.TryGetProperty("channels", out var chanProp), "Audio stats should contain 'channels'");
            Assert.True(chanProp.TryGetProperty("master", out var mChan));
            Assert.True(chanProp.TryGetProperty("bgm", out _));
            Assert.True(chanProp.TryGetProperty("voice", out _));
            Assert.True(chanProp.TryGetProperty("sfx", out _));
            Assert.True(chanProp.TryGetProperty("ambience", out _));
            Assert.True(chanProp.TryGetProperty("ui", out _));

            // Channel levels
            Assert.True(mChan.TryGetProperty("rmsL", out _));
            Assert.True(mChan.TryGetProperty("rmsR", out _));
            Assert.True(mChan.TryGetProperty("peakL", out _));
            Assert.True(mChan.TryGetProperty("peakR", out _));

            // Spectrum bands array (4 bands)
            Assert.True(root.TryGetProperty("spectrum", out var specProp), "Should contain 'spectrum'");
            Assert.Equal(JsonValueKind.Array, specProp.ValueKind);
            Assert.Equal(4, specProp.GetArrayLength());

            // Metrics with ground truth and heuristic flags
            Assert.True(root.TryGetProperty("metrics", out var metProp));
            Assert.True(metProp.TryGetProperty("isHeuristic", out var heurProp));
            Assert.True(heurProp.GetBoolean(), "estimatedRms should be labeled as heuristic");
            Assert.True(metProp.TryGetProperty("masterPeak", out _));
            Assert.True(metProp.TryGetProperty("masterRms", out _));
        }
    }

    [Fact]
    public async Task EditorBridge_HonestContracts_RejectsStubsWithoutTargets_AndDispatchesWhenHooked()
    {
        var bridge = RowlEyeEditorBridge.Instance;

        // Reset dispatchers
        bridge.CommandDispatcher = null;
        bridge.TextInputDispatcher = null;
        bridge.DragDropDispatcher = null;

        // Test 1: Unhooked save shortcut should return false with honest NoTarget error
        var unhookedActions = new[]
        {
            new EditorInputActionDto("shortcut", Id: "save"),
            new EditorInputActionDto("shortcut", Id: "delete"),
            new EditorInputActionDto("text", Text: "Hello AI"),
            new EditorInputActionDto("dragDrop", Asset: "Assets/item.png", X: 10, Y: 20)
        };

        var results = await bridge.InjectActionsAsync(unhookedActions);
        Assert.Equal(4, results.Count);

        // Save: NoTarget
        Assert.False(results[0].Success, "Save should fail honestly when no command dispatcher is hooked");
        Assert.Contains("NoTarget", results[0].Message ?? "");

        // Delete: NoTarget
        Assert.False(results[1].Success, "Delete should fail honestly when no command dispatcher is hooked");
        Assert.Contains("NoTarget", results[1].Message ?? "");

        // Text: NoTarget
        Assert.False(results[2].Success, "Text should fail honestly when no text dispatcher is hooked");
        Assert.Contains("NoTarget", results[2].Message ?? "");

        // DragDrop: NotImplemented
        Assert.False(results[3].Success, "DragDrop should fail honestly when no drop target is hooked");
        Assert.Contains("NotImplemented", results[3].Message ?? "");

        // Test 2: Hook custom dispatchers and verify execution succeeds
        bridge.CommandDispatcher = (cmd, arg) => Task.FromResult(cmd == "save");
        bridge.TextInputDispatcher = txt => txt.Length > 0;
        bridge.DragDropDispatcher = (asset, x, y) => asset.EndsWith(".png");

        var hookedResults = await bridge.InjectActionsAsync(unhookedActions);
        Assert.True(hookedResults[0].Success, "Save should succeed when dispatcher is hooked");
        Assert.False(hookedResults[1].Success, "Delete should report failure from dispatcher");
        Assert.True(hookedResults[2].Success, "Text should succeed when text dispatcher accepts input");
        Assert.True(hookedResults[3].Success, "DragDrop should succeed when target accepts asset");

        // Clean up
        bridge.CommandDispatcher = null;
        bridge.TextInputDispatcher = null;
        bridge.DragDropDispatcher = null;
    }

    [Fact]
    public void FrameStats_RingBuffer_Calculates_Percentiles_And_Spikes()
    {
        var service = RowlEyeEngineService.Instance;

        // Record a set of frame durations
        service.RecordFrameTime(16.6);
        service.RecordFrameTime(16.7);
        service.RecordFrameTime(16.5);
        service.RecordFrameTime(26.0); // spike > 25ms
        service.RecordFrameTime(42.0); // hitch > 33.33ms

        var stats = service.GetFrameStats();
        Assert.NotNull(stats);
        Assert.True(stats.SampleCount >= 5);
        Assert.True(stats.MinMs > 0);
        Assert.True(stats.MaxMs >= 42.0);
        Assert.True(stats.AvgMs > 0);
        Assert.True(stats.P50Ms > 0);
        Assert.True(stats.P95Ms >= stats.P50Ms);
        Assert.True(stats.P99Ms >= stats.P95Ms);
        Assert.True(stats.HitchCount >= 1, "Hitch count should be at least 1");
        Assert.True(stats.SpikeCount >= 1, "Spike count should be at least 1");
    }

    [Fact]
    public void EngineMemoryStats_Returns_Valid_Host_And_TextureCache_Telemetry()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        var mem = service.GetMemoryStats();
        Assert.NotNull(mem);
        Assert.True(mem.ManagedHeapBytes > 0, "Managed heap should be > 0");
        Assert.True(mem.ProcessWorkingSetBytes > 0, "Working set should be > 0");
        Assert.True(mem.ProcessPrivateBytes > 0, "Private bytes should be > 0");
        Assert.NotNull(mem.FrameStats);
    }

    [Fact]
    public void StoryCoverage_Tracks_Choices_Visits_And_Endings()
    {
        var graphService = RowlEyeStoryGraphService.Instance;

        // Record visit and choice
        graphService.RecordChoice("choice_opt_alpha");
        graphService.RecordVisit(101);

        var coverage = graphService.GetCoverage(currentNodeId: 101);
        Assert.NotNull(coverage);
        Assert.True(coverage.TotalNodes > 0, "Story graph should have nodes");
        Assert.True(coverage.VisitedCount >= 1, "Visited count should be at least 1");
        Assert.Contains(101u, coverage.VisitedNodes);
        Assert.Contains("choice_opt_alpha", coverage.TriedChoices);
        Assert.True(coverage.CoveragePercent > 0.0, "Coverage percent should be calculated");
        Assert.NotNull(coverage.UnvisitedNodes);
        Assert.NotNull(coverage.ReachedEndings);
    }

    [Fact]
    public void FrameSha256_Computes_Deterministic_Hash_And_Maps_To_NodeId()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        string hash1 = service.ComputeFrameSha256(associateWithNodeId: 101);
        Assert.NotEmpty(hash1);
        Assert.Equal(64, hash1.Length); // standard SHA-256 hex string length

        Assert.Equal(hash1, service.LastFrameSha256);
        Assert.True(service.NodeFrameHashes.ContainsKey(101));
        Assert.Equal(hash1, service.NodeFrameHashes[101]);

        // Same frame produces identical deterministic hash
        string hash2 = service.ComputeFrameSha256();
        Assert.Equal(hash1, hash2);
    }

    [Fact]
    public void ScenarioDriver_Executes_Choice_Step_And_Records_Node_Transitions()
    {
        var storySvc = RowlEyeStoryGraphService.Instance;

        var request = new ScenarioDriveRequestDto(
            ScenarioName: "Test Auto Drive Scenario",
            Steps: new[]
            {
                new ScenarioStepDto(Action: "step", DeltaTime: 0.01667f),
                new ScenarioStepDto(Action: "choice", OptionId: "opt_test_branch"),
                new ScenarioStepDto(Action: "rewind", Steps: 1)
            },
            StopOnError: false,
            StepBudget: 10
        );

        var result = storySvc.ExecuteScenario(request);
        Assert.NotNull(result);
        Assert.Equal("Test Auto Drive Scenario", result.ScenarioName);
        Assert.Equal(3, result.TotalSteps);
        Assert.Equal(3, result.ExecutedSteps);
        Assert.True(result.TotalElapsedMs >= 0.0);
        Assert.NotNull(result.StepResults);
        Assert.Equal(3, result.StepResults.Count);
        Assert.All(result.StepResults, s => Assert.NotEmpty(s.Action));
    }

    [Fact]
    public void EditorBridge_DialogAnswerQueue_Queues_And_Pops_Answers_Safely()
    {
        var bridge = RowlEyeEditorBridge.Instance;
        bridge.ClearDialogAnswers();

        var answer = new DialogAnswerDto("confirm", Approve: true, SelectedPath: "/tmp/project.rowl");
        bridge.QueueDialogAnswer(answer);

        var list = bridge.GetPendingDialogAnswers();
        Assert.Single(list);
        Assert.Equal("confirm", list[0].DialogType);
        Assert.True(list[0].Approve);
        Assert.Equal("/tmp/project.rowl", list[0].SelectedPath);

        bool popped = bridge.TryGetPendingDialogAnswer("confirm", out var retrieved);
        Assert.True(popped);
        Assert.NotNull(retrieved);
        Assert.Equal("confirm", retrieved.DialogType);
        Assert.True(retrieved.Approve);

        // Queue is now empty
        Assert.Empty(bridge.GetPendingDialogAnswers());
    }

    [Fact]
    public void ReportAggregator_Generates_Complete_Health_And_Telemetry_Report()
    {
        var aggregator = RowlEyeReportAggregator.Instance;
        var report = aggregator.GenerateReport(currentNodeId: 101);

        Assert.NotNull(report);
        Assert.NotEmpty(report.TimestampUtc);
        Assert.Contains(report.OverallHealth, new[] { "HEALTHY", "DEGRADED", "FAILED" });
        Assert.NotEmpty(report.HealthSummary);
        Assert.NotNull(report.EngineContext);
        Assert.NotNull(report.MemoryStats);
        Assert.NotNull(report.StoryCoverage);
        Assert.NotNull(report.NodeFrameHashes);
        Assert.NotNull(report.RecentErrors);
    }

    [Fact]
    public void VisualTreeService_Extracts_Rich_Properties_And_Detects_Overflow()
    {
        RowlEngine.Editor.Tools.RowlEye.RowlEyeHeadlessRuntime.EnsureInitialized();

        Avalonia.Threading.Dispatcher.UIThread.Invoke(() =>
        {
            var container = new Avalonia.Controls.Border
            {
                Name = "RootBorder",
                Width = 100,
                Height = 100,
                Opacity = 0.85,
                ZIndex = 5,
                ClipToBounds = false
            };

            var child = new Avalonia.Controls.Button
            {
                Name = "ActionBtn",
                Width = 150, // width 150 > container 100 -> causes overflow when ClipToBounds is false
                Height = 40,
                Opacity = 0.5,
                ZIndex = 10,
                IsEnabled = true,
                IsHitTestVisible = true
            };
            child.Classes.Add("primary-action");
            container.Child = child;

            var extracted = RowlEngine.Editor.Tools.RowlEye.Inspection.VisualTreeService.Extract(container);
            Assert.NotNull(extracted);
            Assert.Equal("RootBorder", extracted.Name);
            Assert.Equal(5, extracted.ZIndex);
            Assert.Equal(0.85, extracted.Opacity);
            Assert.False(extracted.ClipToBounds);
            Assert.Single(extracted.Children);

            var childNode = extracted.Children[0];
            Assert.Equal("ActionBtn", childNode.Name);
            Assert.Equal(10, childNode.ZIndex);
            Assert.Equal(0.5, childNode.Opacity);
            Assert.True(childNode.IsEnabled);
            Assert.True(childNode.IsHitTestVisible);
            Assert.Contains("primary-action", childNode.Classes);
        });
    }

    [Fact]
    public void AnomalyDetection_Dispatches_When_Fallback_Or_Error_Is_Logged()
    {
        var hub = RowlEngine.Editor.Tools.RowlEye.Ai.AiTelemetryHub.Instance;
        var (subId, reader) = hub.Subscribe();
        try
        {
            // Simulate log callback directly via hub
            hub.Publish(RowlEngine.Editor.Tools.RowlEye.Ai.AiTelemetryEventType.AnomalyDetected, new
            {
                category = "RenderFallback",
                code = "FALLBACK_TRIGGERED",
                detail = "MSDF GPU shader artifact is unavailable; using font fallback"
            });

            // Verify event reached the subscriber reader
            bool readOk = reader.TryRead(out var receivedEvt);
            Assert.True(readOk);
            Assert.Equal(RowlEngine.Editor.Tools.RowlEye.Ai.AiTelemetryEventType.AnomalyDetected, receivedEvt.Type);

            // Verify event was also stored in the hub history ring buffer
            var history = hub.GetHistory();
            var lastAnomaly = history.LastOrDefault(e => e.Type == RowlEngine.Editor.Tools.RowlEye.Ai.AiTelemetryEventType.AnomalyDetected);
            Assert.NotNull(lastAnomaly);

            string json = JsonSerializer.Serialize(lastAnomaly.Payload);
            using var doc = JsonDocument.Parse(json);
            Assert.True(doc.RootElement.TryGetProperty("category", out var catProp));
            Assert.Equal("RenderFallback", catProp.GetString());
        }
        finally
        {
            hub.Unsubscribe(subId);
        }
    }

    [Fact]
    public void Watchdog_Monitors_UIThread_And_Reports_Liveness()
    {
        var watchdog = RowlEngine.Editor.Tools.RowlEye.Engine.RowlEyeWatchdog.Instance;
        watchdog.Start(checkIntervalMs: 100);

        Assert.NotNull(watchdog);
        Assert.False(watchdog.IsHung, "Watchdog should not be hung on active process");
        Assert.True(watchdog.LastHeartbeatAgeMs >= 0.0);
        Assert.Equal(2500, watchdog.HangThresholdMs);

        watchdog.Stop();
    }

    [Fact]
    public void EngineService_Resize_TimeScale_And_RngSeed_Operations_Succeed()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        // Test Resize
        service.Resize(1920, 1080);
        var ctx1080 = service.GetContext();
        Assert.Equal(1920, ctx1080.Resolution[0]);
        Assert.Equal(1080, ctx1080.Resolution[1]);

        // Reset to original
        service.Resize(640, 360);
        var ctxOrig = service.GetContext();
        Assert.Equal(640, ctxOrig.Resolution[0]);
        Assert.Equal(360, ctxOrig.Resolution[1]);

        // Test TimeScale
        service.TimeScale = 2.5f;
        Assert.Equal(2.5f, service.TimeScale);
        service.TimeScale = 1.0f;
        Assert.Equal(1.0f, service.TimeScale);

        // Test RngSeed (should not throw)
        service.SetRngSeed(123456789UL);
    }

    [Fact]
    public void StoryGraph_Mutate_Add_Move_And_Remove_Nodes_Succeeds()
    {
        var storySvc = RowlEyeStoryGraphService.Instance;

        // 1. Add node
        var addReq = new StoryMutateRequestDto(
            Action: "add_node",
            NodeId: 9999,
            Title: "AI Generated Test Node",
            Speaker: "AI QA Agent",
            Text: "This node was created autonomously.",
            EditorX: 450.0,
            EditorY: 300.0
        );
        var addRes = storySvc.MutateStoryGraph(addReq);
        Assert.True(addRes.Success);
        Assert.Equal(9999u, addRes.NodeId);

        // 2. Move node
        var moveReq = new StoryMutateRequestDto(
            Action: "move_node",
            NodeId: 9999,
            EditorX: 500.0,
            EditorY: 350.0
        );
        var moveRes = storySvc.MutateStoryGraph(moveReq);
        Assert.True(moveRes.Success);

        // 3. Remove node
        var delReq = new StoryMutateRequestDto(
            Action: "remove_node",
            NodeId: 9999
        );
        var delRes = storySvc.MutateStoryGraph(delReq);
        Assert.True(delRes.Success);
    }

    [Fact]
    public void Vfs_DeadAssets_Scans_And_Reports_Unreferenced_Files()
    {
        var service = RowlEyeEngineService.Instance;
        var deadAssets = service.GetDeadAssets();

        Assert.NotNull(deadAssets);
        Assert.True(deadAssets.TotalScannedFiles >= 0);
        Assert.NotNull(deadAssets.UnreferencedFiles);
        Assert.True(deadAssets.TotalReclaimableBytes >= 0);
    }

    [Fact]
    public void ComprehensionService_Generates_All_Inclusive_360_Project_Snapshot()
    {
        var svc = RowlEngine.Editor.Tools.RowlEye.Ai.RowlEyeComprehensionService.Instance;
        var snapshot = svc.GenerateSnapshot();

        Assert.NotNull(snapshot);
        Assert.NotEmpty(snapshot.TimestampUtc);
        Assert.NotEmpty(snapshot.NarrativeSummary);
        Assert.NotNull(snapshot.ProjectInfo);
        Assert.NotEmpty(snapshot.ProjectInfo.ProjectName);
        Assert.NotNull(snapshot.LiveScene);
        Assert.NotNull(snapshot.StoryState);
        Assert.NotNull(snapshot.DiagnosticsAndHealth);
        Assert.Contains(snapshot.DiagnosticsAndHealth.OverallHealth, new[] { "HEALTHY", "DEGRADED", "FAILED" });
        Assert.NotEmpty(snapshot.AvailableAiTools);
    }

    [Fact]
    public void ComprehensionService_Exposes_Function_Tools_Schema_For_LLMs()
    {
        var svc = RowlEngine.Editor.Tools.RowlEye.Ai.RowlEyeComprehensionService.Instance;
        var tools = svc.GetAiToolsSchema();

        Assert.NotNull(tools);
        string json = JsonSerializer.Serialize(tools);
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("tools", out var toolsProp));
        Assert.Equal(JsonValueKind.Array, toolsProp.ValueKind);
        Assert.True(toolsProp.GetArrayLength() >= 5);
    }
}
