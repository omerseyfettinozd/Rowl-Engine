using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Controls;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Inspector;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 4 Dilim 4 — advanced inspector and inline validation: schema
/// descriptors, range limits, reactive field rules (speaker/text,
/// content_id, ports, assets), chapter/group assignment reactivity,
/// content_id regen, badge queries and linter-foundation record shape.
/// Headless.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAdvancedInspectorSlice4Tests
{
    private sealed class InspectorShell : IDisposable
    {
        public string PreviousRoot { get; }

        public MainWindowViewModel Vm { get; }

        public string Root { get; }

        public InspectorShell()
        {
            PreviousRoot = MainWindowViewModel.ProjectRoot;
            string parent = Path.Combine(Path.GetTempPath(), "RowlInsp_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(parent);
            var created = ProjectFactory.CreateNewProject("InspTest", parent);
            if (!created.Success || created.Info == null)
                throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
            Root = created.Info.Path;
            Vm = new MainWindowViewModel(Root, connectEngine: false);
            Vm.Nodes.Clear();
            Vm.Connections.Clear();
            Vm.Groups.Clear();
            Vm.Subgraphs.Clear();
            Vm.Chapters.Clear();
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }

        public void Dispose()
        {
            try { Directory.Delete(Path.GetDirectoryName(Root)!, recursive: true); }
            catch (Exception) { }
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }
    }

    private static NodeViewModel BareNode(ulong id, string title, double x = 0, double y = 0) =>
        new(id, title, x, y, bare: true);

    private static DialogueComponentViewModel AddDialogue(
        NodeViewModel node, string speaker, string text,
        string? contentId = null, bool enabled = true)
    {
        var dialogue = node.CreateObject("Dialogue").AddComponent<DialogueComponentViewModel>();
        dialogue.Speaker = speaker;
        dialogue.DialogueText = text;
        dialogue.IsEnabled = enabled;
        if (contentId is not null)
            dialogue.ContentId = contentId;
        return dialogue;
    }

    private static InspectorValidationService AttachedService(
        ObservableCollection<NodeViewModel> nodes,
        ObservableCollection<ConnectionViewModel>? conns = null,
        string? assetsDir = null)
    {
        var service = new InspectorValidationService();
        service.Attach(nodes, conns, assetsDir is null ? null : (Func<string>)(() => assetsDir));
        return service;
    }

    // ── Schema ─────────────────────────────────────────────

    [Fact]
    public void Schema_NodeFields_TitleAndChapter()
    {
        var fields = InspectorSchemaProvider.NodeFields;
        Assert.Contains(fields, f =>
            f.Key == InspectorFieldDescriptor.Keys.NodeTitle &&
            f.Kind == InspectorFieldKind.Text);
        Assert.Contains(fields, f =>
            f.Key == InspectorFieldDescriptor.Keys.NodeChapter &&
            f.Kind == InspectorFieldKind.Chapter);
    }

    [Fact]
    public void Schema_DialogueRanges_MatchEditorLimits()
    {
        var speakerFont = InspectorSchemaProvider.FindRange(
            InspectorFieldDescriptor.Keys.DialogueSpeakerFontSize);
        Assert.NotNull(speakerFont);
        Assert.Equal(10, speakerFont!.Min);
        Assert.Equal(64, speakerFont.Max);
        Assert.Equal(2, speakerFont.Step);

        var opacity = InspectorSchemaProvider.FindRange(
            InspectorFieldDescriptor.Keys.DialogueBoxOpacity);
        Assert.NotNull(opacity);
        Assert.Equal(0, opacity!.Min);
        Assert.Equal(1, opacity.Max);

        Assert.NotEmpty(InspectorSchemaProvider.ForComponent("dialogue"));
        Assert.NotEmpty(InspectorSchemaProvider.ForComponent("background"));
        Assert.Empty(InspectorSchemaProvider.ForComponent("no-such-component"));
        Assert.Empty(InspectorSchemaProvider.ForComponent(null));
        Assert.Null(InspectorSchemaProvider.FindRange("dialogue.speaker"));
        Assert.Null(InspectorSchemaProvider.FindRange("missing.key"));
    }

    // ── Field rules ────────────────────────────────────────

    [Fact]
    public void Validation_EmptySpeakerAndText_Warn()
    {
        var nodes = new ObservableCollection<NodeViewModel> { BareNode(1, "A") };
        AddDialogue(nodes[0], "", "Merhaba");
        var service = AttachedService(nodes);

        var speaker = service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueSpeaker);
        Assert.Single(speaker);
        Assert.False(speaker[0].IsError);

        AddDialogue(nodes[0], "X", "");
        var text = service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueText);
        Assert.NotEmpty(text);
        Assert.All(text, i => Assert.False(i.IsError));
    }

    [Fact]
    public void Validation_InvalidAndDuplicateContentId_Error()
    {
        const string shared = "12345678-1234-1234-1234-1234567890ab";
        var nodes = new ObservableCollection<NodeViewModel>
        {
            BareNode(1, "A"),
            BareNode(2, "B"),
        };
        AddDialogue(nodes[0], "S", "T", "not-a-uuid");
        AddDialogue(nodes[1], "S", "T", shared);
        var first = BareNode(3, "C");
        nodes.Add(first);
        AddDialogue(first, "S", "T", shared);
        var service = AttachedService(nodes);

        var invalid = service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueContentId);
        Assert.Single(invalid);
        Assert.True(invalid[0].IsError);

        // First owner stays clean; the later duplicate errors.
        Assert.Empty(service.IssuesFor(2, InspectorFieldDescriptor.Keys.DialogueContentId));
        var dup = service.IssuesFor(3, InspectorFieldDescriptor.Keys.DialogueContentId);
        Assert.Single(dup);
        Assert.True(dup[0].IsError);
        Assert.Contains("mükerrer", dup[0].Message, StringComparison.Ordinal);

        // Disabled dialogue is invisible to validation.
        var quiet = BareNode(4, "D");
        nodes.Add(quiet);
        AddDialogue(quiet, "", "", "garbage!!", enabled: false);
        Assert.Empty(service.IssuesFor(4, InspectorFieldDescriptor.Keys.DialogueContentId));
        Assert.Empty(service.IssuesFor(4, InspectorFieldDescriptor.Keys.DialogueText));
    }

    [Fact]
    public void Validation_Ports_IsolatedWarnsAndDanglingTargetErrors()
    {
        var nodes = new ObservableCollection<NodeViewModel>
        {
            BareNode(1, "A"),
            BareNode(2, "B"),
        };
        var conns = new ObservableCollection<ConnectionViewModel>();
        var service = AttachedService(nodes, conns);

        Assert.Single(service.IssuesFor(1, InspectorFieldDescriptor.Keys.NodePorts));
        Assert.False(service.IssuesFor(1, InspectorFieldDescriptor.Keys.NodePorts)[0].IsError);

        conns.Add(new ConnectionViewModel(nodes[0], nodes[1]));
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.NodePorts));
        Assert.Empty(service.IssuesFor(2, InspectorFieldDescriptor.Keys.NodePorts));

        var choice = nodes[0].CreateObject("C").AddComponent<ChoiceComponentViewModel>();
        choice.Options.Clear();
        choice.Options.Add(new ChoiceOptionViewModel { Text = "Git", TargetNodeId = 0 });
        var target = service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget);
        Assert.Single(target);
        Assert.True(target[0].IsError);

        choice.Options[0].TargetNodeId = 2;
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget));
    }

    [Fact]
    public void Validation_Assets_SeveritiesMirrorBatchPolicy()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlInspAssets_" + Guid.NewGuid().ToString("N"));
        try
        {
            string images = Path.Combine(root, "images");
            Directory.CreateDirectory(images);
            File.WriteAllText(Path.Combine(images, "hero.png"), "fake");
            var nodes = new ObservableCollection<NodeViewModel> { BareNode(1, "A") };
            var bg = nodes[0].CreateObject("BG").AddComponent<BackgroundComponentViewModel>();
            var service = AttachedService(nodes, null, root);
            const string key = InspectorFieldDescriptor.Keys.BackgroundTexture;

            bg.Texture = "missing.png";
            Assert.True(service.IssuesFor(1, key).Single().IsError);

            bg.Texture = "hero.png";
            Assert.Empty(service.IssuesFor(1, key));

            bg.Texture = "../outside.png";
            Assert.Contains(service.IssuesFor(1, key), i => i.IsError);

            bg.Texture = "clip.mp3";
            Assert.Contains(service.IssuesFor(1, key), i => i.IsError);

            bg.Texture = "anim.gif";
            Assert.Contains(service.IssuesFor(1, key), i => i.IsError);

            bg.Texture = "   ";
            Assert.Empty(service.IssuesFor(1, key));
        }
        finally
        {
            try { Directory.Delete(root, true); } catch (Exception) { }
        }
    }

    [Fact]
    public void Validation_Reactive_KeystrokeClearsBadgeWithoutReattach()
    {
        var nodes = new ObservableCollection<NodeViewModel> { BareNode(1, "A") };
        var dialogue = AddDialogue(nodes[0], "", "Merhaba");
        var service = AttachedService(nodes);
        Assert.Single(service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueSpeaker));

        int changed = 0;
        service.Changed += (_, _) => changed++;
        dialogue.Speaker = "Evelyn";

        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueSpeaker));
        Assert.True(changed > 0);
    }

    // ── Chapter / group assignment ─────────────────────────

    [Fact]
    public void Inspector_ChapterAssignment_RoundTrips()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        vm.Chapters.LoadDefinitions(new[]
        {
            new ChapterDefinition("ch1", "Arrivals", 0, string.Empty, 1),
        });
        var node = BareNode(1, "A");
        vm.Nodes.Add(node);
        vm.SelectNodeQuiet(node);
        var inspector = vm.InspectorViewModel;

        Assert.Contains(InspectorViewModel.NoChapterLabel, inspector.ChapterAssignmentOptions);
        Assert.Contains("Arrivals", inspector.ChapterAssignmentOptions);
        Assert.Equal(InspectorViewModel.NoChapterLabel, inspector.SelectedChapterOption);

        inspector.SelectedChapterOption = "Arrivals";
        Assert.Equal("ch1", node.ChapterId);
        Assert.Contains("Arrivals", vm.Subgraphs.AvailableChapterOptions);

        inspector.SelectedChapterOption = InspectorViewModel.NoChapterLabel;
        Assert.Equal(string.Empty, node.ChapterId);
    }

    [Fact]
    public void Inspector_GroupJoinAndLeave_UpdatesMembership()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        var node = BareNode(1, "A", 0, 0);
        vm.Nodes.Add(node);
        vm.SelectNodeQuiet(node);
        var inspector = vm.InspectorViewModel;
        var group = vm.Groups.Create("Act", "#3B82F6", -100, -100, 800, 400);

        Assert.Single(inspector.JoinableGroups);
        Assert.Empty(inspector.MemberGroups);

        inspector.JoinSelectedNodeToGroup(group.GroupId);
        Assert.Single(inspector.MemberGroups);
        Assert.Empty(inspector.JoinableGroups);
        Assert.Contains(1UL, group.MemberNodeIds);

        inspector.RemoveSelectedNodeFromGroup(group.GroupId);
        Assert.Empty(inspector.MemberGroups);
        Assert.Single(inspector.JoinableGroups);
        Assert.DoesNotContain(1UL, group.MemberNodeIds);
    }

    [Fact]
    public void Inspector_RegenerateContentId_ClearsBadge()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        var node = BareNode(1, "A");
        vm.Nodes.Add(node);
        AddDialogue(node, "S", "T", "bad-id");
        vm.SelectNodeQuiet(node);
        var inspector = vm.InspectorViewModel;
        const string key = InspectorFieldDescriptor.Keys.DialogueContentId;

        Assert.True(inspector.Validation.IssuesFor(1, key).Single().IsError);
        inspector.RegenerateSelectedContentIdCommand.Execute(null);
        Assert.Empty(inspector.Validation.IssuesFor(1, key));
        Assert.True(ContentIdService.IsValidContentId(
            node.AllComponents.OfType<DialogueComponentViewModel>().First().ContentId));
    }

    // ── Queries, records, widgets ──────────────────────────

    [Fact]
    public void Inspector_IssuesFor_FiltersByNodeAndKey()
    {
        var nodes = new ObservableCollection<NodeViewModel>
        {
            BareNode(1, "A"),
            BareNode(2, ""),
        };
        AddDialogue(nodes[0], "", "Hi");
        var service = AttachedService(nodes);

        // Speaker warning + isolated-node warning on node 1.
        Assert.Equal(2, service.IssuesFor(1).Count);
        Assert.Single(service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueSpeaker));
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueText));
        Assert.Empty(service.IssuesFor(999, InspectorFieldDescriptor.Keys.DialogueSpeaker));
        Assert.NotEmpty(service.IssuesFor(2, InspectorFieldDescriptor.Keys.NodeTitle));
    }

    [Fact]
    public void Inspector_Issues_AreLinterReadyRecords()
    {
        var nodes = new ObservableCollection<NodeViewModel> { BareNode(1, "") };
        AddDialogue(nodes[0], "", "", "bad");
        var second = BareNode(2, "B");
        nodes.Add(second);
        var service = AttachedService(nodes);

        Assert.NotEmpty(service.Issues);
        Assert.All(service.Issues, issue =>
        {
            Assert.IsType<ProjectValidationIssue>(issue);
            Assert.True(issue.NodeId.HasValue);
        });
        Assert.True(service.HasErrors);
    }

    [Fact]
    public void Inspector_TitleAndDisabledRules()
    {
        var nodes = new ObservableCollection<NodeViewModel> { BareNode(1, "") };
        var service = AttachedService(nodes);
        var title = service.IssuesFor(1, InspectorFieldDescriptor.Keys.NodeTitle);
        Assert.Single(title);
        Assert.False(title[0].IsError);

        nodes[0].Title = "Named";
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.NodeTitle));

        // Disabled components (and blank optionals) stay silent.
        AddDialogue(nodes[0], "", "", "", enabled: false);
        var bg = nodes[0].CreateObject("BG").AddComponent<BackgroundComponentViewModel>();
        bg.IsEnabled = false;
        bg.Texture = "missing.png";
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.DialogueText));
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.BackgroundTexture));
    }

    [Fact]
    public void Widget_HexParsing_NormalizesAndRejects()
    {
        Assert.True(ColorPickerControl.TryParseHex("#ff0000", out _));
        Assert.True(ColorPickerControl.TryParseHex("ff0000", out _));
        Assert.True(ColorPickerControl.TryParseHex("#fff", out _));
        Assert.False(ColorPickerControl.TryParseHex("not-a-color", out _));
        Assert.False(ColorPickerControl.TryParseHex("", out _));
        Assert.False(ColorPickerControl.TryParseHex(null, out _));
        Assert.Equal("#FF0000", ColorPickerControl.NormalizeHex("ff0000"));
        Assert.Equal("#FFFFFF", ColorPickerControl.NormalizeHex("#fff"));
    }

    private sealed class SliceShell : IDisposable
    {
        public string PreviousRoot { get; }

        public MainWindowViewModel Vm { get; }

        public string Root { get; }

        public SliceShell()
        {
            PreviousRoot = MainWindowViewModel.ProjectRoot;
            string parent = Path.Combine(Path.GetTempPath(), "RowlInspShell_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(parent);
            var created = ProjectFactory.CreateNewProject("InspShell", parent);
            if (!created.Success || created.Info == null)
                throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
            Root = created.Info.Path;
            Vm = new MainWindowViewModel(Root, connectEngine: false);
            Vm.Nodes.Clear();
            Vm.Connections.Clear();
            Vm.Groups.Clear();
            Vm.Subgraphs.Clear();
            Vm.Chapters.Clear();
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }

        public void Dispose()
        {
            try { Directory.Delete(Path.GetDirectoryName(Root)!, recursive: true); }
            catch (Exception) { }
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }
    }
}
