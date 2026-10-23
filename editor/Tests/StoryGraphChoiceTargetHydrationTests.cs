using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Gerçek ekran bulgusu (2026-10-20): second_signal'da Oyun panelinde seçime tıklayınca motor
/// "Choice option not found on Node #1: go_answer" diyordu. Seçim bileşeninde target_node_id
/// yoksa (hedef yalnız düğümün next_nodes'unda) seçeneğin hedefi 0 kalıyor, editörün motora
/// gönderdiği grafikte seçim kenarlarının hepsi düşüyordu — teller ekranda görünse de.
/// </summary>
public sealed class StoryGraphChoiceTargetHydrationTests
{
    [Fact]
    public void SecondSignal_ChoiceEdges_SurviveLoadAndSerialize()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        string path = Path.Combine(FindRepoRoot(), "samples", "second_signal", "Assets", "json", "full_story_graph.json");
        Dispatcher.UIThread.Invoke(() =>
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var loaded = StoryGraphLoaderService.Load(doc);
            Assert.True(loaded.Success, loaded.ErrorMessage);

            var start = loaded.Nodes.Single(n => n.Id == 1);
            var options = start.ChoiceOptions;
            Assert.Equal(2UL, options.Single(o => o.OptionId == "go_answer").TargetNodeId);
            Assert.Equal(3UL, options.Single(o => o.OptionId == "go_silent").TargetNodeId);

            string json = StoryGraphSerializer.SerializeFullStoryGraph(loaded.Nodes, loaded.Connections, 1);
            using var outDoc = JsonDocument.Parse(json);
            var next = outDoc.RootElement.GetProperty("nodes").EnumerateArray()
                .Single(n => n.GetProperty("id").GetUInt64() == 1)
                .GetProperty("next_nodes").EnumerateArray()
                .Select(e => (e.GetProperty("option_id").GetString(), e.GetProperty("id").GetUInt64()))
                .ToList();
            Assert.Contains(("go_answer", 2UL), next);
            Assert.Contains(("go_silent", 3UL), next);
        });
    }

    private static string FindRepoRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory, "samples", "second_signal")))
                return directory;
            directory = Directory.GetParent(directory)?.FullName;
        }
        throw new Exception("Repository root with samples/ could not be located.");
    }
}
