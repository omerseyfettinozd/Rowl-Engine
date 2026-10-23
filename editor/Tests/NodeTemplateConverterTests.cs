using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.NodeLibrary;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 7 D2 — kütüphane şablonu düğümün objelerini ve bileşen değerlerini taşır; sahneye
/// eklenen kopya bağımsızdır (yeni kimlik, yeni diyalog content_id, boş bağlantı hedefi) ve
/// kaynak düğümdeki sonraki değişiklikler kaydı etkilemez.
/// </summary>
public sealed class NodeTemplateConverterTests
{
    private static NodeViewModel SourceNode()
    {
        NodeViewModel node = StoryGraphCanvasService.CreateDefaultNode(205, 10, 20);
        node.Title = "Yol ayrımı";
        FrameObjectViewModel obj = node.CreateObject("Seçim");
        obj.IsActive = false;
        var choice = new ChoiceComponentViewModel();
        choice.Options[0].TargetNodeId = 301;
        choice.Options[0].Text = "Ormana git";
        choice.IsEnabled = false;
        obj.AddComponent(choice);
        return node;
    }

    private static DialogueComponentViewModel FirstDialogue(NodeViewModel node) =>
        node.AllComponents.OfType<DialogueComponentViewModel>().First();

    [Fact]
    public void Capture_KeepsObjectsAndComponentValues()
    {
        NodeViewModel node = SourceNode();
        NodeTemplate template = NodeTemplateConverter.Capture(node);

        Assert.Equal("Yol ayrımı", template.Title);
        Assert.Equal(node.Objects.Select(o => o.Name), template.Objects.Select(o => o.Name));
        TemplateObject choiceObject = template.Objects.Single(o => o.Name == "Seçim");
        Assert.False(choiceObject.IsActive);
        TemplateComponent choice = Assert.Single(choiceObject.Components);
        Assert.Equal("choice", choice.TypeKey);
        Assert.False(choice.Enabled);
        Assert.Contains("Ormana git", choice.Values.ToJsonString());
    }

    [Fact]
    public void SavedTemplate_InstantiatesAsIndependentCopy()
    {
        NodeViewModel node = SourceNode();
        string sourceDialogueId = FirstDialogue(node).ContentId;
        string sourceText = FirstDialogue(node).DialogueText;

        NodeLibraryState state = NodeLibraryOperations.Save(
            NodeLibraryState.Empty, NodeTemplateConverter.SourceKey("proj", node.Id),
            NodeTemplateConverter.Capture(node), folderId: null, DateTimeOffset.UnixEpoch);
        // Kaynakta sonradan yapılan değişiklik kaydı etkilemez.
        node.Title = "Değişti";
        FirstDialogue(node).DialogueText = "değişti";

        LibraryItem item = Assert.Single(state.Items);
        var result = NodeTemplateConverter.Instantiate(item.Template, id: 900, x: 40, y: 50);
        NodeViewModel copy = result.Node;

        Assert.Empty(result.SkippedComponentTypes);
        Assert.Equal(900ul, copy.Id);
        Assert.Equal("Yol ayrımı", copy.Title);
        Assert.Equal((40.0, 50.0), (copy.X, copy.Y));
        Assert.Equal(item.Template.Objects.Select(o => o.Name), copy.Objects.Select(o => o.Name));
        Assert.Equal(sourceText, FirstDialogue(copy).DialogueText);
        Assert.NotEqual(sourceDialogueId, FirstDialogue(copy).ContentId);
        Assert.False(string.IsNullOrEmpty(FirstDialogue(copy).ContentId));

        // AllComponents yalnız etkin objeleri sayar; "Seçim" bilerek pasif.
        ChoiceComponentViewModel choice = copy.Objects.SelectMany(o => o.Components).OfType<ChoiceComponentViewModel>().Single();
        Assert.False(choice.IsEnabled);
        Assert.Equal("Ormana git", choice.Options[0].Text);
        Assert.Equal(0ul, choice.Options[0].TargetNodeId); // bağlantı hedefi taşınmaz
        Assert.False(copy.Objects.Single(o => o.Name == "Seçim").IsActive);

        var sourceIds = node.Objects.SelectMany(o => o.Components).Select(c => c.ComponentId).ToHashSet();
        Assert.DoesNotContain(copy.Objects.SelectMany(o => o.Components), c => sourceIds.Contains(c.ComponentId));
    }

    [Fact]
    public void Instantiate_SkipsUnknownComponentTypes()
    {
        var template = new NodeTemplate("X", new[]
        {
            new TemplateObject("O", true, new[]
            {
                new TemplateComponent("future_widget", new System.Text.Json.Nodes.JsonObject()),
            }),
        });
        var result = NodeTemplateConverter.Instantiate(template, 7, 0, 0);
        Assert.Equal(new[] { "future_widget" }, result.SkippedComponentTypes);
        Assert.Empty(result.Node.Objects.SelectMany(o => o.Components));
    }
}

/// <summary>Faz 7 D2 — şablondan sahneye ekleme ana pencerede geri alınabilir bir düğüm ekler.</summary>
[Collection("StaticRootSequential")]
public sealed class NodeLibraryAddToSceneTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public NodeLibraryAddToSceneTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlLibraryAdd_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("LibraryAddTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
        // KÖK GERİ VERİLMEZ: bkz. EditorAssetGridVirtualizationTests.
    }

    public void Dispose()
    {
        try { _vm.Dispose(); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); }
        catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    [Fact]
    public void AddNodeFromTemplate_AddsSelectsAndIsUndoable()
    {
        NodeViewModel source = _vm.Nodes.First();
        NodeTemplate template = NodeLibraryOperations.Reusable(NodeTemplateConverter.Capture(source));
        int before = _vm.Nodes.Count;
        ulong maxId = _vm.Nodes.Max(n => n.Id);

        NodeViewModel added = _vm.AddNodeFromTemplate(template);

        Assert.Equal(before + 1, _vm.Nodes.Count);
        Assert.Equal(maxId + 1, added.Id);
        Assert.Same(added, _vm.SelectedNode);
        Assert.Equal(source.Title, added.Title);
        Assert.StartsWith(_vm.ProjectLibraryKey + ":", _vm.SelectedNodeLibraryKey);

        _vm.UndoRedo.Undo();
        Assert.DoesNotContain(added, _vm.Nodes);
        _vm.UndoRedo.Redo();
        Assert.Contains(added, _vm.Nodes);
    }
}
