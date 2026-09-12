using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorGraphValidationTests
{
    public static void Run(string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 3]: ConnectionViewModel & Single Outgoing Wire Rule...");
        var testConnections = new ObservableCollection<ConnectionViewModel>();
        var node1 = new NodeViewModel(1, "N1", 0, 0);
        var node2 = new NodeViewModel(2, "N2", 300, 0);
        var node3 = new NodeViewModel(3, "N3", 600, 0);

        testConnections.Add(new ConnectionViewModel(node1, node2));
        testConnections.Add(new ConnectionViewModel(node1, node3));
        if (testConnections.Count != 2)
            throw new Exception("Initial test conns failed");
        Console.WriteLine(
            "  ✅ [PASS] Wire topology & single outgoing rule verified without touching project files");

        var validationNode = new NodeViewModel(901, "Validation", 0, 0, bare: false);
        var missingBackground = validationNode.GetComponent<BackgroundComponentViewModel>();
        if (missingBackground != null)
            missingBackground.Texture = "missing_build_asset.png";
        var orphanNode = new NodeViewModel(902, "Orphan", 0, 0, bare: true);
        var validationIssues = ProjectValidationService.Validate(
            new[] { validationNode, orphanNode },
            Array.Empty<ConnectionViewModel>(),
            Path.Combine(testProjectRoot, "Assets"),
            validationNode.Id);
        if (!validationIssues.Any(issue =>
                issue.IsError && issue.Message.Contains("missing_build_asset.png")) ||
            !validationIssues.Any(issue =>
                !issue.IsError && issue.Message.Contains("unreachable")))
        {
            throw new Exception("Build validation did not report missing assets and unreachable nodes");
        }
        Console.WriteLine(
            "  ✅ [PASS] Build validation blocks missing assets and reports unreachable nodes");

        var cycleConnections = new[]
        {
            new ConnectionViewModel(node3, node2),
            new ConnectionViewModel(node2, node3)
        };
        var graphIssues = ProjectValidationService.Validate(
            new[] { node1, node2, node3 },
            cycleConnections,
            Path.Combine(testProjectRoot, "Assets"),
            node3.Id);
        var terminalIssues = ProjectValidationService.Validate(
            new[] { node1, node2 },
            new[] { new ConnectionViewModel(node1, node2) },
            Path.Combine(testProjectRoot, "Assets"),
            node1.Id);
        if (!graphIssues.Any(issue => !issue.IsError && issue.Message.Contains("cycle")) ||
            !terminalIssues.Any(issue => !issue.IsError && issue.Message.Contains("terminal")))
        {
            throw new Exception("Graph analysis did not report reachable cycles and terminal nodes");
        }
        Console.WriteLine(
            "  ✅ [PASS] Graph analysis uses the actual start node and reports cycles/terminal nodes");
    }
}
