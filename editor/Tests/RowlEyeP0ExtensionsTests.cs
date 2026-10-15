using System;
using System.Text.Json;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using Xunit;

namespace RowlEngine.Editor.Tests;

public sealed class RowlEyeP0ExtensionsTests
{
    [Fact]
    public void StoryGraphTopology_Returns_Valid_Nodes_Edges_And_Analysis()
    {
        var topology = RowlEyeStoryGraphService.Instance.GetTopology(101);
        Assert.NotNull(topology);

        Assert.True(topology.TotalNodes > 0, "Story topology should contain nodes");
        Assert.True(topology.StartNodeId > 0, "StartNodeId should be positive");
        Assert.NotNull(topology.Nodes);
        Assert.NotNull(topology.Edges);
        Assert.NotNull(topology.DeadEnds);

        // Verify start node exists in node list
        Assert.Contains(topology.Nodes, n => n.Id == topology.StartNodeId);
    }

    [Fact]
    public void WorldQuery_Includes_RewindStepsAvailable_And_History()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        // First advance completes typewriter, second advance moves to next node (Node 102)
        service.InjectInputJson("{\"t\":\"advance\"}");
        service.InjectInputJson("{\"t\":\"advance\"}");

        string json = service.QueryWorldJson();
        Assert.NotNull(json);

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("rewindStepsAvailable", out var rProp), "Query should include rewindStepsAvailable");
        Assert.True(rProp.GetUInt32() >= 1, "After advancing node, at least 1 rewind step should be available");

        Assert.True(root.TryGetProperty("history", out var hProp), "Query should include history");
        Assert.Equal(JsonValueKind.Array, hProp.ValueKind);
    }

    [Fact]
    public void Rewind_And_DragInput_Executes_Without_Errors()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        // Advance past typewriter and to next node
        service.InjectInputJson("{\"t\":\"advance\"}");
        service.InjectInputJson("{\"t\":\"advance\"}");

        // Now test Rewind via C# method
        bool rewindOk = service.Rewind(1);
        Assert.True(rewindOk, "Rewind(1) should succeed when history is present");

        // Test Drag / Swipe input
        bool dragOk = service.InjectInputJson("{\"t\":\"drag\",\"fromX\":200.0,\"fromY\":300.0,\"toX\":100.0,\"toY\":300.0}");
        Assert.True(dragOk, "Injecting drag input should succeed");

        // Test Rewind via input JSON
        bool rewindJsonOk = service.InjectInputJson("{\"t\":\"rewind\",\"steps\":1}");
        Assert.True(rewindJsonOk, "Injecting rewind JSON should succeed");
    }

    [Fact]
    public void VfsStatus_Query_Returns_Valid_Mounts_And_Diagnosis()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        string json = service.QueryVfsStatusJson("Assets/json/full_story_graph.json");
        Assert.NotNull(json);

        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.True(root.TryGetProperty("mounts", out var mProp), "VFS status should contain mounts array");
        Assert.Equal(JsonValueKind.Array, mProp.ValueKind);

        Assert.True(root.TryGetProperty("query", out var qProp), "VFS status should contain query object");
        Assert.True(qProp.TryGetProperty("exists", out var exProp));
        Assert.True(qProp.TryGetProperty("diagnosis", out var diagProp));
        Assert.NotEmpty(diagProp.GetString() ?? "");
    }

    [Fact]
    public void ScriptEval_Executes_Script_And_Condition_Safely()
    {
        var service = RowlEyeEngineService.Instance;
        service.EnsureInitialized(640, 360);

        // Test statement execution
        var execRes = service.EvalScript("local x = 10");
        Assert.NotNull(execRes);
        Assert.True(execRes.Ok, $"Script eval should succeed, error: {execRes.Error}");

        // Test condition evaluation
        var condRes = service.EvalScript("1 + 1 == 2", isCondition: true);
        Assert.NotNull(condRes);
        Assert.True(condRes.Ok);
        Assert.Equal("true", condRes.Result);
    }

    [Fact]
    public void EditorBridge_UndoRedoStatus_And_InputInject_Executes()
    {
        var bridge = RowlEyeEditorBridge.Instance;
        var status = bridge.GetUndoRedoStatus();
        Assert.NotNull(status);

        var actions = new System.Collections.Generic.List<EditorInputActionDto>
        {
            new EditorInputActionDto("shortcut", Id: "undo"),
            new EditorInputActionDto("key", Key: "Z", Ctrl: true),
            new EditorInputActionDto("text", Text: "AI Test Prompt"),
            new EditorInputActionDto("dragDrop", Asset: "Assets/json/full_story_graph.json", X: 100, Y: 150)
        };

        var results = bridge.InjectActions(actions);
        Assert.NotNull(results);
        Assert.Equal(actions.Count, results.Count);
        Assert.All(results, r => Assert.NotEmpty(r.ActionType));
    }
}
