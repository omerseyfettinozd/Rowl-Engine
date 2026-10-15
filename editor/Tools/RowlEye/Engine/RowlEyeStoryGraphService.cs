using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace RowlEngine.Editor.Tools.RowlEye.Engine;

/// <summary>
/// DTO representing a single edge / connection between two story nodes.
/// </summary>
public sealed record StoryEdgeDto(
    [property: JsonPropertyName("from")] uint From,
    [property: JsonPropertyName("to")] uint To,
    [property: JsonPropertyName("label")] string Label,
    [property: JsonPropertyName("optionId")] string OptionId
);

/// <summary>
/// DTO representing a summarized story node for AI topology exploration.
/// </summary>
public sealed record StoryNodeSummaryDto(
    [property: JsonPropertyName("id")] uint Id,
    [property: JsonPropertyName("title")] string Title,
    [property: JsonPropertyName("speaker")] string Speaker,
    [property: JsonPropertyName("text")] string Text,
    [property: JsonPropertyName("background")] string Background,
    [property: JsonPropertyName("character")] string Character,
    [property: JsonPropertyName("visited")] bool Visited,
    [property: JsonPropertyName("isDeadEnd")] bool IsDeadEnd,
    [property: JsonPropertyName("nextCount")] int NextCount
);

/// <summary>
/// Full topological graph analysis for AI agents.
/// </summary>
public sealed record StoryGraphTopologyDto(
    [property: JsonPropertyName("startNodeId")] uint StartNodeId,
    [property: JsonPropertyName("currentNodeId")] uint CurrentNodeId,
    [property: JsonPropertyName("totalNodes")] int TotalNodes,
    [property: JsonPropertyName("totalEdges")] int TotalEdges,
    [property: JsonPropertyName("visitedCount")] int VisitedCount,
    [property: JsonPropertyName("coveragePercent")] double CoveragePercent,
    [property: JsonPropertyName("nodes")] IReadOnlyList<StoryNodeSummaryDto> Nodes,
    [property: JsonPropertyName("edges")] IReadOnlyList<StoryEdgeDto> Edges,
    [property: JsonPropertyName("deadEnds")] IReadOnlyList<uint> DeadEnds,
    [property: JsonPropertyName("unreachableNodes")] IReadOnlyList<uint> UnreachableNodes
);

/// <summary>
/// Detailed coverage metrics and ending progress for AI story exploration.
/// </summary>
public sealed record StoryCoverageDto(
    [property: JsonPropertyName("visitedNodes")] IReadOnlyList<uint> VisitedNodes,
    [property: JsonPropertyName("visitedCount")] int VisitedCount,
    [property: JsonPropertyName("triedChoices")] IReadOnlyList<string> TriedChoices,
    [property: JsonPropertyName("triedChoiceCount")] int TriedChoiceCount,
    [property: JsonPropertyName("totalNodes")] int TotalNodes,
    [property: JsonPropertyName("totalEdges")] int TotalEdges,
    [property: JsonPropertyName("totalEndings")] int TotalEndings,
    [property: JsonPropertyName("reachedEndings")] IReadOnlyList<uint> ReachedEndings,
    [property: JsonPropertyName("isAtEnding")] bool IsAtEnding,
    [property: JsonPropertyName("currentNodeId")] uint CurrentNodeId,
    [property: JsonPropertyName("coveragePercent")] double CoveragePercent,
    [property: JsonPropertyName("unvisitedNodes")] IReadOnlyList<uint> UnvisitedNodes
);

/// <summary>
/// A single step in an autonomous playthrough scenario.
/// </summary>
public sealed record ScenarioStepDto(
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("optionId")] string? OptionId = null,
    [property: JsonPropertyName("expectNodeId")] uint? ExpectNodeId = null,
    [property: JsonPropertyName("steps")] uint? Steps = null,
    [property: JsonPropertyName("deltaTime")] float? DeltaTime = null
);

/// <summary>
/// Request to drive a playthrough scenario autonomously.
/// </summary>
public sealed record ScenarioDriveRequestDto(
    [property: JsonPropertyName("scenarioName")] string? ScenarioName = null,
    [property: JsonPropertyName("steps")] IReadOnlyList<ScenarioStepDto>? Steps = null,
    [property: JsonPropertyName("stopOnError")] bool? StopOnError = null,
    [property: JsonPropertyName("stepBudget")] int? StepBudget = null
);

/// <summary>
/// Result of executing an individual step within an autonomous scenario.
/// </summary>
public sealed record ScenarioStepResultDto(
    [property: JsonPropertyName("stepIndex")] int StepIndex,
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("success")] bool Success,
    [property: JsonPropertyName("fromNodeId")] uint FromNodeId,
    [property: JsonPropertyName("toNodeId")] uint ToNodeId,
    [property: JsonPropertyName("frameSha256")] string? FrameSha256,
    [property: JsonPropertyName("elapsedMs")] double ElapsedMs,
    [property: JsonPropertyName("error")] string? Error
);

/// <summary>
/// Overall outcome and coverage summary of an autonomous scenario playthrough.
/// </summary>
public sealed record ScenarioDriveResultDto(
    [property: JsonPropertyName("scenarioName")] string ScenarioName,
    [property: JsonPropertyName("success")] bool Success,
    [property: JsonPropertyName("totalSteps")] int TotalSteps,
    [property: JsonPropertyName("executedSteps")] int ExecutedSteps,
    [property: JsonPropertyName("totalElapsedMs")] double TotalElapsedMs,
    [property: JsonPropertyName("finalNodeId")] uint FinalNodeId,
    [property: JsonPropertyName("isAtEnding")] bool IsAtEnding,
    [property: JsonPropertyName("coveragePercent")] double CoveragePercent,
    [property: JsonPropertyName("stepResults")] IReadOnlyList<ScenarioStepResultDto> StepResults
);

/// <summary>
/// Request to mutate, add, connect, or reposition nodes in the story graph.
/// </summary>
public sealed record StoryMutateRequestDto(
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("nodeId")] uint? NodeId = null,
    [property: JsonPropertyName("targetNodeId")] uint? TargetNodeId = null,
    [property: JsonPropertyName("title")] string? Title = null,
    [property: JsonPropertyName("speaker")] string? Speaker = null,
    [property: JsonPropertyName("text")] string? Text = null,
    [property: JsonPropertyName("editorX")] double? EditorX = null,
    [property: JsonPropertyName("editorY")] double? EditorY = null,
    [property: JsonPropertyName("optionId")] string? OptionId = null,
    [property: JsonPropertyName("optionLabel")] string? OptionLabel = null
);

/// <summary>
/// Result of a story graph mutation operation.
/// </summary>
public sealed record StoryMutateResultDto(
    [property: JsonPropertyName("success")] bool Success,
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("nodeId")] uint NodeId,
    [property: JsonPropertyName("message")] string? Message,
    [property: JsonPropertyName("totalNodes")] int TotalNodes,
    [property: JsonPropertyName("totalEdges")] int TotalEdges
);

/// <summary>
/// Service that parses, caches, and tracks topological coverage of the Story Graph.
/// </summary>
public sealed class RowlEyeStoryGraphService
{
    private static readonly Lazy<RowlEyeStoryGraphService> InstanceLazy = new(() => new RowlEyeStoryGraphService());
    public static RowlEyeStoryGraphService Instance => InstanceLazy.Value;

    private readonly object _lock = new();
    private readonly HashSet<uint> _visitedNodeIds = new();
    private readonly HashSet<string> _triedChoices = new();
    private string? _cachedFilePath;
    private DateTime _lastFileWriteTime = DateTime.MinValue;
    private StoryGraphTopologyDto? _cachedTopology;
    private List<Dictionary<string, object?>>? _inMemoryNodes;

    private List<Dictionary<string, object?>> EnsureLoadedNodes(string filePath)
    {
        if (_inMemoryNodes != null) return _inMemoryNodes;

        var list = new List<Dictionary<string, object?>>();
        string json = File.ReadAllText(filePath);
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        if (root.TryGetProperty("nodes", out var nodesProp) && nodesProp.ValueKind == JsonValueKind.Array)
        {
            foreach (var n in nodesProp.EnumerateArray())
            {
                var dict = JsonSerializer.Deserialize<Dictionary<string, object?>>(n.GetRawText());
                if (dict != null) list.Add(dict);
            }
        }
        _inMemoryNodes = list;
        return _inMemoryNodes;
    }

    public void ResetMutations()
    {
        lock (_lock)
        {
            _inMemoryNodes = null;
            _cachedTopology = null;
        }
    }

    private static bool MatchNodeId(object? idVal, uint targetId)
    {
        if (idVal == null) return false;
        if (idVal is uint u) return u == targetId;
        if (idVal is int i) return i == (int)targetId;
        if (idVal is long l) return l == (long)targetId;
        if (idVal is JsonElement je)
        {
            if (je.TryGetUInt32(out var parsedId)) return parsedId == targetId;
            if (je.TryGetInt32(out var parsedInt)) return parsedInt == (int)targetId;
        }
        return false;
    }

    public void RecordVisit(uint nodeId)
    {
        if (nodeId == 0) return;
        lock (_lock)
        {
            _visitedNodeIds.Add(nodeId);
        }
    }

    public void RecordChoice(string optionId)
    {
        if (string.IsNullOrWhiteSpace(optionId)) return;
        lock (_lock)
        {
            _triedChoices.Add(optionId);
        }
    }

    public StoryCoverageDto GetCoverage(uint currentNodeId = 0)
    {
        var topology = GetTopology(currentNodeId);
        lock (_lock)
        {
            uint activeNode = currentNodeId > 0 ? currentNodeId : topology.CurrentNodeId;
            bool isAtEnding = topology.DeadEnds.Contains(activeNode);
            var reachedEndings = topology.DeadEnds.Where(d => _visitedNodeIds.Contains(d)).ToList();
            var allIds = topology.Nodes.Select(n => n.Id).ToHashSet();
            var unvisited = allIds.Except(_visitedNodeIds).ToList();

            return new StoryCoverageDto(
                VisitedNodes: _visitedNodeIds.OrderBy(id => id).ToList(),
                VisitedCount: _visitedNodeIds.Count,
                TriedChoices: _triedChoices.OrderBy(c => c).ToList(),
                TriedChoiceCount: _triedChoices.Count,
                TotalNodes: topology.TotalNodes,
                TotalEdges: topology.TotalEdges,
                TotalEndings: topology.DeadEnds.Count,
                ReachedEndings: reachedEndings,
                IsAtEnding: isAtEnding,
                CurrentNodeId: activeNode,
                CoveragePercent: topology.CoveragePercent,
                UnvisitedNodes: unvisited
            );
        }
    }

    /// <summary>
    /// Mutates the active story graph in memory, supporting add_node, remove_node, move_node, and update_dialogue.
    /// Automatically invalidates topological caches.
    /// </summary>
    public StoryMutateResultDto MutateStoryGraph(StoryMutateRequestDto req)
    {
        ArgumentNullException.ThrowIfNull(req);
        lock (_lock)
        {
            string? filePath = ResolveStoryGraphPath();
            if (string.IsNullOrEmpty(filePath) || !File.Exists(filePath))
            {
                return new StoryMutateResultDto(false, req.Action ?? "", 0, "Story graph file not found", 0, 0);
            }

            try
            {
                var nodesList = EnsureLoadedNodes(filePath);

                uint targetNodeId = req.NodeId ?? 0;
                string action = req.Action?.ToLowerInvariant() ?? "";
                string message = "Success";

                switch (action)
                {
                    case "add_node":
                    {
                        if (targetNodeId == 0)
                        {
                            uint maxId = 100;
                            foreach (var n in nodesList)
                            {
                                if (n.TryGetValue("id", out var idVal) && idVal is JsonElement je && je.TryGetUInt32(out var parsedId))
                                {
                                    if (parsedId > maxId) maxId = parsedId;
                                }
                            }
                            targetNodeId = maxId + 1;
                        }

                        var newNode = new Dictionary<string, object?>
                        {
                            ["id"] = targetNodeId,
                            ["title"] = req.Title ?? $"Node #{targetNodeId}",
                            ["editor_x"] = req.EditorX ?? 200.0,
                            ["editor_y"] = req.EditorY ?? 200.0,
                            ["objects"] = new List<object?>
                            {
                                new Dictionary<string, object?>
                                {
                                    ["id"] = Guid.NewGuid().ToString("N")[..8],
                                    ["name"] = "Dialogue",
                                    ["is_active"] = true,
                                    ["components"] = new List<object?>
                                    {
                                        new Dictionary<string, object?>
                                        {
                                            ["type"] = "dialogue",
                                            ["id"] = Guid.NewGuid().ToString("N")[..8],
                                            ["enabled"] = true,
                                            ["data"] = new Dictionary<string, object?>
                                            {
                                                ["speaker"] = req.Speaker ?? "Narrator",
                                                ["dialogue"] = req.Text ?? "New story dialogue",
                                                ["typewriter_enabled"] = true,
                                                ["text_speed"] = 30
                                            }
                                        }
                                    }
                                }
                            }
                        };
                        nodesList.Add(newNode);
                        message = $"Node #{targetNodeId} added successfully";
                        break;
                    }

                    case "remove_node":
                    {
                        int removed = nodesList.RemoveAll(n =>
                            n.TryGetValue("id", out var idVal) && MatchNodeId(idVal, targetNodeId));
                        if (removed == 0)
                        {
                            return new StoryMutateResultDto(false, action, targetNodeId, $"Node #{targetNodeId} not found", nodesList.Count, 0);
                        }
                        message = $"Node #{targetNodeId} removed successfully";
                        break;
                    }

                    case "move_node":
                    {
                        var found = nodesList.FirstOrDefault(n =>
                            n.TryGetValue("id", out var idVal) && MatchNodeId(idVal, targetNodeId));
                        if (found == null)
                        {
                            return new StoryMutateResultDto(false, action, targetNodeId, $"Node #{targetNodeId} not found", nodesList.Count, 0);
                        }
                        if (req.EditorX.HasValue) found["editor_x"] = req.EditorX.Value;
                        if (req.EditorY.HasValue) found["editor_y"] = req.EditorY.Value;
                        message = $"Node #{targetNodeId} moved to ({req.EditorX}, {req.EditorY})";
                        break;
                    }

                    case "update_dialogue":
                    {
                        var found = nodesList.FirstOrDefault(n =>
                            n.TryGetValue("id", out var idVal) && MatchNodeId(idVal, targetNodeId));
                        if (found == null)
                        {
                            return new StoryMutateResultDto(false, action, targetNodeId, $"Node #{targetNodeId} not found", nodesList.Count, 0);
                        }
                        if (req.Title != null) found["title"] = req.Title;
                        message = $"Node #{targetNodeId} dialogue updated";
                        break;
                    }

                    default:
                        return new StoryMutateResultDto(false, action, targetNodeId, $"Unknown mutation action '{action}'", nodesList.Count, 0);
                }

                _cachedTopology = null;
                return new StoryMutateResultDto(true, action, targetNodeId, message, nodesList.Count, 0);
            }
            catch (Exception ex)
            {
                return new StoryMutateResultDto(false, req.Action ?? "", req.NodeId ?? 0, ex.Message, 0, 0);
            }
        }
    }

    /// <summary>
    /// Executes an autonomous scenario playthrough on the native engine, validating choices, steps, and node transitions.
    /// </summary>
    public ScenarioDriveResultDto ExecuteScenario(ScenarioDriveRequestDto request)
    {
        var totalSw = System.Diagnostics.Stopwatch.StartNew();
        string name = request?.ScenarioName ?? "Autonomous Scenario";
        var steps = request?.Steps ?? Array.Empty<ScenarioStepDto>();
        bool stopOnError = request?.StopOnError ?? true;
        int budget = request?.StepBudget ?? 50;

        var stepResults = new List<ScenarioStepResultDto>();
        bool overallSuccess = true;
        uint currentNodeId = 0;

        // Ensure engine is ready
        var engineSvc = RowlEyeEngineService.Instance;
        engineSvc.EnsureInitialized();

        for (int i = 0; i < steps.Count && i < budget; i++)
        {
            var step = steps[i];
            var stepSw = System.Diagnostics.Stopwatch.StartNew();
            uint fromNode = 0;

            try
            {
                string worldJson = engineSvc.QueryWorldJson();
                using var doc = JsonDocument.Parse(worldJson);
                if (doc.RootElement.TryGetProperty("nodeId", out var nidProp) && nidProp.TryGetUInt32(out var nid))
                {
                    fromNode = nid;
                }
            }
            catch { }

            bool stepSuccess = true;
            string? stepError = null;
            string action = step.Action?.ToLowerInvariant() ?? "step";

            try
            {
                switch (action)
                {
                    case "choice":
                        if (!string.IsNullOrEmpty(step.OptionId))
                        {
                            RecordChoice(step.OptionId);
                            string injectJson = "{\"type\":\"choice\",\"optionId\":\"" + step.OptionId + "\"}";
                            bool ok = engineSvc.InjectInputJson(injectJson);
                            if (!ok)
                            {
                                stepSuccess = false;
                                stepError = $"Failed to inject choice '{step.OptionId}'";
                            }
                        }
                        engineSvc.Step(step.DeltaTime ?? 0.01667f);
                        break;

                    case "rewind":
                        uint rewSteps = step.Steps ?? 1;
                        bool rewOk = engineSvc.Rewind(rewSteps);
                        if (!rewOk)
                        {
                            stepSuccess = false;
                            stepError = $"Failed to rewind {rewSteps} step(s)";
                        }
                        engineSvc.Step(0.01667f);
                        break;

                    case "step":
                        engineSvc.Step(step.DeltaTime ?? 0.01667f);
                        break;

                    case "assert":
                        // Verification only
                        break;

                    default:
                        stepSuccess = false;
                        stepError = $"Unknown scenario action: {action}";
                        break;
                }
            }
            catch (Exception ex)
            {
                stepSuccess = false;
                stepError = ex.Message;
            }

            // Determine resulting nodeId
            uint toNode = 0;
            try
            {
                string worldJson = engineSvc.QueryWorldJson();
                using var doc = JsonDocument.Parse(worldJson);
                if (doc.RootElement.TryGetProperty("nodeId", out var nidProp) && nidProp.TryGetUInt32(out var nid))
                {
                    toNode = nid;
                    currentNodeId = toNode;
                    RecordVisit(toNode);
                }
            }
            catch { }

            // Verify expectNodeId if specified
            if (step.ExpectNodeId.HasValue && step.ExpectNodeId.Value != toNode)
            {
                stepSuccess = false;
                stepError = $"Expected node {step.ExpectNodeId.Value} but arrived at {toNode}";
            }

            string frameHash = engineSvc.ComputeFrameSha256(toNode);
            stepSw.Stop();

            stepResults.Add(new ScenarioStepResultDto(
                StepIndex: i,
                Action: action,
                Success: stepSuccess,
                FromNodeId: fromNode,
                ToNodeId: toNode,
                FrameSha256: frameHash,
                ElapsedMs: Math.Round(stepSw.Elapsed.TotalMilliseconds, 2),
                Error: stepError
            ));

            if (!stepSuccess)
            {
                overallSuccess = false;
                if (stopOnError) break;
            }
        }

        totalSw.Stop();
        var cov = GetCoverage(currentNodeId);

        return new ScenarioDriveResultDto(
            ScenarioName: name,
            Success: overallSuccess,
            TotalSteps: steps.Count,
            ExecutedSteps: stepResults.Count,
            TotalElapsedMs: Math.Round(totalSw.Elapsed.TotalMilliseconds, 2),
            FinalNodeId: currentNodeId,
            IsAtEnding: cov.IsAtEnding,
            CoveragePercent: cov.CoveragePercent,
            StepResults: stepResults
        );
    }

    public StoryGraphTopologyDto GetTopology(uint currentNodeId = 0)
    {
        lock (_lock)
        {
            if (currentNodeId > 0)
            {
                _visitedNodeIds.Add(currentNodeId);
            }

            string? path = ResolveStoryGraphPath();
            if (path == null || !File.Exists(path))
            {
                return new StoryGraphTopologyDto(
                    StartNodeId: 0,
                    CurrentNodeId: currentNodeId,
                    TotalNodes: 0,
                    TotalEdges: 0,
                    VisitedCount: _visitedNodeIds.Count,
                    CoveragePercent: 0.0,
                    Nodes: Array.Empty<StoryNodeSummaryDto>(),
                    Edges: Array.Empty<StoryEdgeDto>(),
                    DeadEnds: Array.Empty<uint>(),
                    UnreachableNodes: Array.Empty<uint>()
                );
            }

            var writeTime = File.GetLastWriteTimeUtc(path);
            if (_cachedTopology == null || _cachedFilePath != path || _lastFileWriteTime != writeTime)
            {
                _cachedTopology = ParseTopologyFromFile(path);
                _cachedFilePath = path;
                _lastFileWriteTime = writeTime;
            }

            // Update visited status and coverage on the cached topology
            var updatedNodes = _cachedTopology.Nodes.Select(n =>
                n with { Visited = _visitedNodeIds.Contains(n.Id) }
            ).ToList();

            int visitedCount = updatedNodes.Count(n => n.Visited);
            double coverage = updatedNodes.Count > 0 ? Math.Round((double)visitedCount / updatedNodes.Count * 100.0, 1) : 0.0;

            return _cachedTopology with
            {
                CurrentNodeId = currentNodeId > 0 ? currentNodeId : _cachedTopology.CurrentNodeId,
                VisitedCount = visitedCount,
                CoveragePercent = coverage,
                Nodes = updatedNodes
            };
        }
    }

    private StoryGraphTopologyDto ParseTopologyFromFile(string path)
    {
        try
        {
            string json = File.ReadAllText(path);
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            uint startNodeId = root.TryGetProperty("start_node_id", out var sn) && sn.TryGetUInt32(out var s) ? s : 101;
            var nodes = new List<StoryNodeSummaryDto>();
            var edges = new List<StoryEdgeDto>();
            var allNodeIds = new HashSet<uint>();
            var reachable = new HashSet<uint>();

            if (root.TryGetProperty("nodes", out var nodesProp) && nodesProp.ValueKind == JsonValueKind.Array)
            {
                foreach (var n in nodesProp.EnumerateArray())
                {
                    uint id = n.TryGetProperty("id", out var idProp) && idProp.TryGetUInt32(out var nid) ? nid : 0;
                    if (id == 0) continue;

                    allNodeIds.Add(id);
                    string title = n.TryGetProperty("title", out var tProp) ? tProp.GetString() ?? "" : $"Node #{id}";
                    string spk = n.TryGetProperty("speaker", out var spkProp) ? spkProp.GetString() ?? "" : "";
                    string dlg = n.TryGetProperty("dialogue", out var dlgProp) ? dlgProp.GetString() ?? "" : "";
                    string bg = n.TryGetProperty("background", out var bgProp) ? bgProp.GetString() ?? "" : "";
                    string ch = n.TryGetProperty("character", out var chProp) ? chProp.GetString() ?? "" : "";

                    int nextCount = 0;
                    if (n.TryGetProperty("next_nodes", out var nextArr) && nextArr.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var target in nextArr.EnumerateArray())
                        {
                            uint toId = target.TryGetProperty("id", out var toProp) && toProp.TryGetUInt32(out var to) ? to : 0;
                            string label = target.TryGetProperty("label", out var lProp) ? lProp.GetString() ?? "" : "";
                            string optId = target.TryGetProperty("option_id", out var oProp) ? oProp.GetString() ?? "" : "";

                            if (toId > 0)
                            {
                                edges.Add(new StoryEdgeDto(id, toId, label, optId));
                                nextCount++;
                            }
                        }
                    }

                    nodes.Add(new StoryNodeSummaryDto(
                        Id: id,
                        Title: title,
                        Speaker: spk,
                        Text: dlg,
                        Background: bg,
                        Character: ch,
                        Visited: _visitedNodeIds.Contains(id),
                        IsDeadEnd: nextCount == 0,
                        NextCount: nextCount
                    ));
                }
            }

            // Find dead ends
            var deadEnds = nodes.Where(n => n.IsDeadEnd).Select(n => n.Id).ToList();

            // BFS from startNodeId to find reachable nodes
            var queue = new Queue<uint>();
            if (allNodeIds.Contains(startNodeId))
            {
                queue.Enqueue(startNodeId);
                reachable.Add(startNodeId);
            }

            var adjacency = edges.GroupBy(e => e.From).ToDictionary(g => g.Key, g => g.Select(e => e.To).ToList());
            while (queue.Count > 0)
            {
                uint curr = queue.Dequeue();
                if (adjacency.TryGetValue(curr, out var neighbors))
                {
                    foreach (var neighbor in neighbors)
                    {
                        if (allNodeIds.Contains(neighbor) && reachable.Add(neighbor))
                        {
                            queue.Enqueue(neighbor);
                        }
                    }
                }
            }

            var unreachable = allNodeIds.Except(reachable).ToList();
            int visitedCount = nodes.Count(n => n.Visited);
            double coverage = nodes.Count > 0 ? Math.Round((double)visitedCount / nodes.Count * 100.0, 1) : 0.0;

            return new StoryGraphTopologyDto(
                StartNodeId: startNodeId,
                CurrentNodeId: startNodeId,
                TotalNodes: nodes.Count,
                TotalEdges: edges.Count,
                VisitedCount: visitedCount,
                CoveragePercent: coverage,
                Nodes: nodes,
                Edges: edges,
                DeadEnds: deadEnds,
                UnreachableNodes: unreachable
            );
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[RowlEyeStoryGraphService] Error parsing topology: {ex.Message}");
            return new StoryGraphTopologyDto(0, 0, 0, 0, 0, 0.0, Array.Empty<StoryNodeSummaryDto>(), Array.Empty<StoryEdgeDto>(), Array.Empty<uint>(), Array.Empty<uint>());
        }
    }

    private static string? ResolveStoryGraphPath()
    {
        var dir = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (dir != null)
        {
            string p1 = Path.Combine(dir.FullName, "Assets", "full_story_graph.json");
            if (File.Exists(p1)) return p1;
            string p2 = Path.Combine(dir.FullName, "Assets", "json", "full_story_graph.json");
            if (File.Exists(p2)) return p2;
            dir = dir.Parent;
        }

        dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            string p1 = Path.Combine(dir.FullName, "Assets", "full_story_graph.json");
            if (File.Exists(p1)) return p1;
            dir = dir.Parent;
        }

        return null;
    }
}
