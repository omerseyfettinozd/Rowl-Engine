using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 4 Dilim 5 — birleşik lint + build-gate + deep-nav + crash-recovery +
/// background tarama: her kural, error-blocks/warning-passes, interior focus,
/// journal-recovery, v4/v5 round-trip regresyonları, worker semantiği ve
/// 2000-node bütçesi. Headless.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorProjectLinterSlice5Tests
{
    private sealed class SliceShell : IDisposable
    {
        public MainWindowViewModel Vm { get; }

        public string Root { get; }

        private readonly string _previousRoot;

        public SliceShell()
        {
            _previousRoot = MainWindowViewModel.ProjectRoot;
            string parent = Path.Combine(Path.GetTempPath(), "RowlSlice5_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(parent);
            var created = ProjectFactory.CreateNewProject("Slice5Test", parent);
            if (!created.Success || created.Info == null)
                throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
            Root = created.Info.Path;
            Vm = new MainWindowViewModel(Root, connectEngine: false);
            Vm.Nodes.Clear();
            Vm.Connections.Clear();
            Vm.Groups.Clear();
            Vm.Subgraphs.Clear();
            Vm.Chapters.Clear();
            // Keep the static root on the temp project for the test body so
            // ScheduleSave bookkeeping (dirty.flag) never touches the repo.
        }

        public string AssetsPath => Path.Combine(Root, "Assets");

        public void Dispose()
        {
            try { Directory.Delete(Path.GetDirectoryName(Root)!, recursive: true); }
            catch (Exception) { }
            try { MainWindowViewModel.ProjectRoot = _previousRoot; }
            catch (Exception) { }
        }
    }

    private static string MakeTempAssets()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlLint_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(root, "Assets");
        foreach (string sub in new[] { "", "images", "audio", "fonts", "scripts", "locales", "json" })
            Directory.CreateDirectory(Path.Combine(assets, sub));
        return assets;
    }

    private static void DeleteTempAssets(string assets)
    {
        try { Directory.Delete(Path.GetDirectoryName(assets)!, recursive: true); }
        catch (Exception) { }
    }

    private static NodeViewModel BareNode(ulong id, string title, double x = 0, double y = 0) =>
        new(id, title, x, y, bare: true);

    private static DialogueComponentViewModel AddDialogue(
        NodeViewModel node, string speaker, string text, string? contentId = null)
    {
        var dialogue = new DialogueComponentViewModel { Speaker = speaker, DialogueText = text };
        if (contentId is not null)
            dialogue.ContentId = contentId;
        node.CreateObject("Dialogue Box").AddComponent(dialogue);
        return dialogue;
    }

    private static ChoiceComponentViewModel AddChoice(NodeViewModel node, params (string text, ulong target, bool enabled)[] options)
    {
        var choice = node.CreateObject("Choice").AddComponent<ChoiceComponentViewModel>();
        choice.Options.Clear();
        foreach (var (text, target, enabled) in options)
            choice.Options.Add(new ChoiceOptionViewModel { Text = text, TargetNodeId = target, IsEnabled = enabled });
        return choice;
    }

    private static ScriptComponentViewModel AddScript(NodeViewModel node, string path)
    {
        var script = node.CreateObject("Script").AddComponent<ScriptComponentViewModel>();
        script.ScriptPath = path;
        return script;
    }

    // ── P1: lint inherits Validate ─────────────────────────────

    [Fact]
    public void Lint_InheritsValidateResults_WithoutDuplication()
    {
        string assets = MakeTempAssets();
        try
        {
            File.WriteAllBytes(Path.Combine(assets, "images", "stray.png"),
                new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            string cid = ContentIdService.NewContentId();
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "A", "Hello", cid);
            var n2 = BareNode(2, "Second");
            AddDialogue(n2, "B", "World", cid); // duplicate content_id (error)
            var dlg = AddDialogue(n2, "B", string.Empty); // empty text (lint-only warning)
            dlg.ContentId = string.Empty;
            var nodes = new[] { n1, n2 };
            var connections = new[] { new ConnectionViewModel(n1, n2, "go") };

            var validate = ProjectValidationService.Validate(nodes, connections, assets, 1);
            var lint = ProjectLintService.Lint(nodes, connections, assets, 1);

            Assert.NotEmpty(validate);
            foreach (var issue in validate)
                Assert.Equal(1, lint.Count(l => l.Message == issue.Message && l.IsError == issue.IsError));
            // Lint-only rules fire on top: empty text + unused stray.png.
            Assert.Contains(lint, l => !l.IsError && l.Message.Contains("dialogue text is empty"));
            Assert.Contains(lint, l => !l.IsError && l.AssetPath == "images/stray.png" && l.NodeId is null);
            Assert.DoesNotContain(validate, v => v.Message.Contains("dialogue text is empty"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_ChoiceTargetZero_IsError()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Start");
            var n2 = BareNode(2, "Next");
            AddChoice(n1, ("Go on", 0, true));
            var lint = ProjectLintService.Lint(
                new[] { n1, n2 },
                new[] { new ConnectionViewModel(n1, n2, "go") }, assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false));
            Assert.Contains(lint, l => l.IsError && l.NodeId == 1 && l.Message.Contains("target"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_ChoiceTargetDeletedNode_IsError()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Start");
            AddChoice(n1, ("Lost road", 999, true));
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false));
            var hit = Assert.Single(lint, l => l.IsError && l.Message.Contains("deleted node #999"));
            Assert.Equal(1UL, hit.NodeId);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_ConditionBadFailTarget_IsError()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Gate");
            var cond = n1.CreateObject("Cond").AddComponent<ConditionComponentViewModel>();
            cond.FailTargetNodeId = "0";
            var n2 = BareNode(2, "Other");
            var cond2 = n2.CreateObject("Cond").AddComponent<ConditionComponentViewModel>();
            cond2.FailTargetNodeId = "4242";
            var lint = ProjectLintService.Lint(
                new[] { n1, n2 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false));
            Assert.Contains(lint, l => l.IsError && l.NodeId == 1 && l.Message.Contains("fail target"));
            Assert.Contains(lint, l => l.IsError && l.NodeId == 2 && l.Message.Contains("deleted node #4242"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_DisabledComponentsSkipped()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Solo");
            var choice = n1.CreateObject("Choice").AddComponent<ChoiceComponentViewModel>();
            choice.IsEnabled = false; // default option targets 0: must be ignored
            var script = AddScript(n1, "missing.lua");
            script.IsEnabled = false;
            var dialogue = AddDialogue(n1, string.Empty, string.Empty);
            dialogue.IsEnabled = false;
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1);
            Assert.Empty(lint);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_EmptyLua_Warns()
    {
        string assets = MakeTempAssets();
        try
        {
            File.WriteAllText(Path.Combine(assets, "scripts", "empty.lua"), string.Empty);
            var n1 = BareNode(1, "Start");
            AddScript(n1, "empty.lua");
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false, CheckTranslations: false, CheckGlyphCoverage: false));
            Assert.Contains(lint, l => !l.IsError && l.Message.Contains("is empty") && l.AssetPath == "scripts/empty.lua");
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_OversizedLua_Warns()
    {
        string assets = MakeTempAssets();
        try
        {
            File.WriteAllText(Path.Combine(assets, "scripts", "big.lua"), new string('x', 100));
            var n1 = BareNode(1, "Start");
            var script = AddScript(n1, "big.lua");
            script.IsEnabled = true;
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false, CheckTranslations: false, CheckGlyphCoverage: false, MaxLuaBytes: 16));
            Assert.Contains(lint, l => !l.IsError && l.Message.Contains("big.lua") && l.Message.Contains("over 0 KB"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_RequireOutsideAssets_IsError()
    {
        string assets = MakeTempAssets();
        try
        {
            File.WriteAllText(
                Path.Combine(assets, "scripts", "evil.lua"),
                "local x = require \"../../outside\"\n");
            File.WriteAllText(
                Path.Combine(assets, "scripts", "calm.lua"),
                "local u = require \"utils\"\n");
            var n1 = BareNode(1, "Start");
            AddScript(n1, "evil.lua");
            var n2 = BareNode(2, "Calm");
            AddScript(n2, "calm.lua");
            var lint = ProjectLintService.Lint(
                new[] { n1, n2 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false, CheckTranslations: false, CheckGlyphCoverage: false));
            var hit = Assert.Single(lint, l => l.IsError && l.Message.Contains("../../outside"));
            Assert.Equal(1UL, hit.NodeId);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_EmptyDialogueTextAndSpeaker_Warn()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "A", "Hello");
            var n2 = BareNode(2, "Silent");
            AddDialogue(n2, "B", string.Empty);
            var n3 = BareNode(3, "Nameless");
            AddDialogue(n3, string.Empty, "Spoken words");
            var lint = ProjectLintService.Lint(
                new[] { n1, n2, n3 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckUnusedAssets: false, CheckGlyphCoverage: false));
            Assert.Contains(lint, l => !l.IsError && l.NodeId == 2 && l.Message.Contains("dialogue text is empty"));
            Assert.Contains(lint, l => !l.IsError && l.NodeId == 3 && l.Message.Contains("speaker is empty"));
            Assert.DoesNotContain(lint, l => l.NodeId == 1 && l.Message.Contains("empty"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_MissingTranslation_Warns()
    {
        string assets = MakeTempAssets();
        try
        {
            string cid = ContentIdService.NewContentId();
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "Evelyn", "Hello traveler", cid);
            File.WriteAllText(
                Path.Combine(assets, "locales", "tr.json"),
                "{\"schema_version\":1,\"locale\":\"tr\",\"entries\":{}}");
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckLuaScripts: false, CheckGlyphCoverage: false, CheckUnusedAssets: false));
            var hit = Assert.Single(lint, l => !l.IsError && l.Message.Contains("'tr'") && l.Message.Contains("missing"));
            Assert.Equal(1UL, hit.NodeId);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_ChangedTranslation_Warns()
    {
        string assets = MakeTempAssets();
        try
        {
            string cid = ContentIdService.NewContentId();
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "Evelyn", "Hello traveler", cid);
            File.WriteAllText(
                Path.Combine(assets, "locales", "tr.json"),
                "{\"schema_version\":1,\"locale\":\"tr\",\"entries\":{\"" + cid +
                "\":{\"speaker\":\"Evelyn\",\"text\":\"Merhaba\",\"alt_text\":\"\",\"source_hash\":\"stale\"}}}");
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckLuaScripts: false, CheckGlyphCoverage: false, CheckUnusedAssets: false));
            Assert.Contains(lint, l => !l.IsError && l.Message.Contains("stale") && l.NodeId == 1);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_UnshapableGlyph_Warns()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "A", "Hello � world");
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckLuaScripts: false, CheckTranslations: false, CheckUnusedAssets: false));
            Assert.Contains(lint, l => !l.IsError && l.NodeId == 1 && l.Message.Contains("U+FFFD"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_CleanDialogue_ProducesNoUnshapableWarning()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "A", "Hello world");
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckLuaScripts: false, CheckTranslations: false, CheckUnusedAssets: false));
            Assert.DoesNotContain(lint, l => l.Message.Contains("U+0000"));
            Assert.DoesNotContain(lint, l => l.Message.Contains("unshapable"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_UnusedAsset_Warns_WithNullNodeId()
    {
        string assets = MakeTempAssets();
        try
        {
            File.WriteAllBytes(Path.Combine(assets, "images", "used.png"), new byte[] { 0x89 });
            File.WriteAllBytes(Path.Combine(assets, "images", "old.png"), new byte[] { 0x89 });
            var n1 = BareNode(1, "Start");
            var dialogue = AddDialogue(n1, "A", "Hi");
            dialogue.CustomBoxTexture = "used.png";
            var lint = ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1,
                lintOptions: new ProjectLintOptions(CheckLuaScripts: false, CheckTranslations: false, CheckGlyphCoverage: false));
            var hit = Assert.Single(lint, l => !l.IsError && l.Message.Contains("not referenced"));
            Assert.Null(hit.NodeId);
            Assert.Equal("images/old.png", hit.AssetPath);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    // ── P2: build gate ─────────────────────────────────────────

    [Fact]
    public void BuildGate_ErrorBlocksWarningPasses()
    {
        string assets = MakeTempAssets();
        try
        {
            var bad = BareNode(9901, "Invalid Route Node");
            var badConn = new ConnectionViewModel(bad, null!, "opt1");
            string outDir = Path.Combine(Path.GetTempPath(), "ShouldNotBeCreatedSlice5_" + Guid.NewGuid().ToString("N"));
            var blocked = ProjectBuildService.ExecuteBuildPipeline(
                Path.GetDirectoryName(assets)!, assets, outDir,
                new[] { bad }, new[] { badConn }, 9901, null, null);
            Assert.False(blocked.Succeeded);
            Assert.False(blocked.Cancelled);
            Assert.NotNull(blocked.Diagnostic);
            Assert.Equal(BuildDiagnosticCode.ValidationFailed, blocked.Diagnostic.Code);
            Assert.Equal(BuildDiagnosticSeverity.Error, blocked.Diagnostic.Severity);
            Assert.Equal("build_validation", blocked.Diagnostic.Operation);
            Assert.False(Directory.Exists(outDir));

            // Warning-only graph (unreachable + terminal): the gate predicate passes.
            var w1 = BareNode(1, "Start");
            var w2 = BareNode(2, "Side");
            var validation = ProjectValidationService.Validate(
                new[] { w1, w2 }, Array.Empty<ConnectionViewModel>(), assets, 1);
            Assert.Contains(validation, v => !v.IsError && v.Message.Contains("unreachable"));
            Assert.DoesNotContain(validation, v => v.IsError);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    // ── P3: deep nav ───────────────────────────────────────────

    [Fact]
    public void DeepNav_FocusInteriorNode_EntersScopePansAndHighlights()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        var n1 = BareNode(1, "Entry", 0, 0);
        var n2 = BareNode(2, "Interior", 5000, 0);
        var n3 = BareNode(3, "Exit", 10000, 0);
        vm.Nodes.Add(n1);
        vm.Nodes.Add(n2);
        vm.Nodes.Add(n3);
        vm.Connections.Add(new ConnectionViewModel(n1, n2, "a"));
        vm.Connections.Add(new ConnectionViewModel(n2, n3, "b"));
        vm.Subgraphs.LoadDefinitions(new[]
        {
            new SubgraphDefinition("sg", "Trial", 1, new ulong[] { 3 }, new ulong[] { 1, 2, 3 }),
        });
        vm.NodeGraphViewModel.RefreshVisible();
        Assert.DoesNotContain(n2, vm.NodeGraphViewModel.VisibleNodes);

        vm.FocusIssueNode(2);

        Assert.Equal(new[] { "sg" }, vm.Subgraphs.DepthStack);
        Assert.Same(n2, vm.SelectedNode);
        Assert.True(n2.IsSearchHighlighted);
        Assert.Contains(n2, vm.NodeGraphViewModel.VisibleNodes);
        var (x, y, w, h) = NodeGraphViewModel.NodeBounds(n2);
        double zoom = vm.ZoomScale > 0 ? vm.ZoomScale : 1.0;
        Assert.Equal(vm.NodeGraphViewModel.ViewportWidth / 2 - (x + w / 2) * zoom, vm.PanX, 0);
        Assert.Equal(vm.NodeGraphViewModel.ViewportHeight / 2 - (y + h / 2) * zoom, vm.PanY, 0);

        // Null / unknown ids are a silent no-op.
        vm.FocusIssueNode(null);
        vm.FocusIssueNode(424242UL);
        Assert.Same(n2, vm.SelectedNode);
    }

    // ── P4: crash recovery ─────────────────────────────────────

    private static StoryGraphSaveSnapshot RecoverySnapshot()
    {
        var n1 = BareNode(1, "Start", 10, 20);
        n1.ChapterId = "ch1";
        n1.ColorTag = "red";
        n1.Tags.Add("intro");
        AddDialogue(n1, "Evelyn", "Hello", ContentIdService.NewContentId());
        var n2 = BareNode(2, "Next", 300, 20);
        n2.ChapterId = "ch1";
        var structure = new GraphStructureDocument();
        structure.Groups.Add(new CanvasGroup("g1", "G", "#3B82F6", 0, 0, 400, 200, new ulong[] { 1, 2 }));
        structure.Subgraphs.Add(new SubgraphDefinition("sg", "Trial", 1, new ulong[] { 2 }, new ulong[] { 1, 2 }));
        structure.Chapters.Add(new ChapterDefinition("ch1", "Chapter 1", 0, "sum", 1));
        return StoryGraphSaveService.Capture(
            new[] { n1, n2 },
            new[] { new ConnectionViewModel(n1, n2, "go") },
            1, n1, structure);
    }

    [Fact]
    public void Recovery_JournalReplay_Lossless()
    {
        string assets = MakeTempAssets();
        try
        {
            string jsonDir = Path.Combine(assets, "json");
            bool written = CrashRecoveryService.TryWriteSnapshotWithRecovery(
                RecoverySnapshot(), jsonDir, 1, () => 1, null);
            Assert.True(written);
            Assert.False(CrashRecoveryService.IsDirty(jsonDir));

            var replay = CrashRecoveryService.ReplayJournal(jsonDir);
            Assert.NotEmpty(replay);
            using var document = JsonDocument.Parse(replay[^1]);
            var root = document.RootElement;
            Assert.Equal(5, root.GetProperty("format_version").GetInt32());
            Assert.Equal(2, root.GetProperty("nodes").GetArrayLength());
            var first = root.GetProperty("nodes")[0];
            Assert.Equal("ch1", first.GetProperty("chapter_id").GetString());
            Assert.Equal("red", first.GetProperty("metadata").GetProperty("color_tag").GetString());
            Assert.Contains("intro", first.GetProperty("metadata").GetProperty("tags").EnumerateArray().Select(e => e.GetString()));
            Assert.Equal(1, root.GetProperty("groups").GetArrayLength());
            Assert.Equal(1, root.GetProperty("subgraphs").GetArrayLength());
            Assert.Equal(1, root.GetProperty("chapters").GetArrayLength());

            // Last-good copy + hash sidecar mirror the canonical file.
            string recovery = Path.Combine(jsonDir, ".recovery");
            string canonical = File.ReadAllText(Path.Combine(jsonDir, "full_story_graph.json"));
            Assert.Equal(canonical, File.ReadAllText(Path.Combine(recovery, "last-good.json")));
            Assert.Equal(
                CrashRecoveryService.ComputeSha256(canonical),
                File.ReadAllText(Path.Combine(recovery, "last-good.json.sha256")).Trim());
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Recovery_CorruptCanonical_OffersRestore_WithoutOverwrite()
    {
        string assets = MakeTempAssets();
        try
        {
            string jsonDir = Path.Combine(assets, "json");
            string canonical = Path.Combine(jsonDir, "full_story_graph.json");
            Directory.CreateDirectory(jsonDir);
            File.WriteAllText(canonical, "not json{{{");
            string snapshotJson = StoryGraphSaveService.SerializeFullGraph(RecoverySnapshot());
            Assert.True(CrashRecoveryService.AppendJournalEntry(jsonDir, 7, snapshotJson, 5));

            var status = CrashRecoveryService.CheckAtStartup(
                Path.GetDirectoryName(assets)!, jsonDir);
            Assert.False(status.CanonicalOk);
            Assert.True(status.JournalEntries > 0);
            Assert.Equal("not json{{{", File.ReadAllText(canonical)); // never auto-overwritten

            Assert.True(CrashRecoveryService.TryBuildRestoreOffer(
                Path.GetDirectoryName(assets)!, jsonDir, out var offer, out _));
            Assert.NotNull(offer);
            Assert.Equal("journal", offer.Source);
            Assert.Equal(2, offer.NodeCount);
            Assert.Equal(5, offer.FormatVersion);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Recovery_V5Roundtrip_StaysV5()
    {
        string json = StoryGraphSaveService.SerializeFullGraph(RecoverySnapshot());
        using var document = JsonDocument.Parse(json);
        var loaded = StoryGraphLoaderService.Load(document);
        Assert.True(loaded.Success);
        Assert.Equal(5, loaded.FormatVersion);
        Assert.Single(loaded.Structure.Groups);
        Assert.Single(loaded.Structure.Subgraphs);
        Assert.Single(loaded.Structure.Chapters);

        string resaved = StoryGraphSaveService.SerializeFullGraph(
            StoryGraphSaveService.Capture(loaded.Nodes, loaded.Connections, 1, loaded.Nodes[0], loaded.Structure));
        using var redocument = JsonDocument.Parse(resaved);
        var root = redocument.RootElement;
        Assert.Equal(5, root.GetProperty("format_version").GetInt32());
        Assert.Equal("ch1", root.GetProperty("nodes")[0].GetProperty("chapter_id").GetString());
        Assert.Equal("red", root.GetProperty("nodes")[0].GetProperty("metadata").GetProperty("color_tag").GetString());
        Assert.Single(root.GetProperty("groups").EnumerateArray());
        Assert.Single(root.GetProperty("subgraphs").EnumerateArray());
        Assert.Single(root.GetProperty("chapters").EnumerateArray());
        Assert.Single(loaded.Connections);
    }

    [Fact]
    public void Recovery_V4Roundtrip_ByteStable()
    {
        var n1 = BareNode(1, "A");
        var n2 = BareNode(2, "B");
        string json = StoryGraphSaveService.SerializeFullGraph(
            StoryGraphSaveService.Capture(
                new[] { n1, n2 },
                new[] { new ConnectionViewModel(n1, n2, "go") }, 1, n1, new GraphStructureDocument()));
        using (var document = JsonDocument.Parse(json))
        {
            var root = document.RootElement;
            Assert.Equal(4, root.GetProperty("format_version").GetInt32());
            Assert.False(root.TryGetProperty("groups", out _));
            Assert.False(root.TryGetProperty("subgraphs", out _));
            Assert.False(root.TryGetProperty("chapters", out _));
            foreach (var node in root.GetProperty("nodes").EnumerateArray())
            {
                Assert.False(node.TryGetProperty("chapter_id", out _));
                Assert.False(node.TryGetProperty("metadata", out _));
            }
        }

        using var reload = JsonDocument.Parse(json);
        var loaded = StoryGraphLoaderService.Load(reload);
        Assert.True(loaded.Success);
        string resaved = StoryGraphSaveService.SerializeFullGraph(
            StoryGraphSaveService.Capture(loaded.Nodes, loaded.Connections, 1, loaded.Nodes[0], loaded.Structure));
        using var redocument = JsonDocument.Parse(resaved);
        // Defaults hydrated on load carry no chapter/tag, so the v5 rule
        // must not trigger: no extra keys may appear.
        Assert.Equal(4, redocument.RootElement.GetProperty("format_version").GetInt32());
        Assert.False(redocument.RootElement.TryGetProperty("groups", out _));
    }

    [Fact]
    public void Recovery_StructureApplyFailure_RollsBack()
    {
        string assets = MakeTempAssets();
        try
        {
            string jsonDir = Path.Combine(assets, "json");
            Directory.CreateDirectory(jsonDir);
            Assert.True(StoryGraphSaveService.TryWriteSnapshot(
                RecoverySnapshot(), jsonDir, 1, () => 1, null));

            var sentinel = BareNode(777, "Session");
            var live = new ObservableCollection<NodeViewModel> { sentinel };
            var liveConns = new ObservableCollection<ConnectionViewModel>();
            bool ok = StoryGraphLifecycleCoordinator.LoadGraphWithRollback(
                Path.GetDirectoryName(assets)!, jsonDir, live, liveConns,
                (_, _) => { }, () => { }, _ => { }, null,
                _ => throw new InvalidOperationException("boom"));
            Assert.False(ok);
            var only = Assert.Single(live);
            Assert.Same(sentinel, only);
            Assert.Empty(liveConns);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    // ── P5: background worker ──────────────────────────────────

    [Fact]
    public void Worker_InvokeLint_And_TryPostSurvivesFailure()
    {
        string assets = MakeTempAssets();
        try
        {
            using var worker = new AssetScanWorker();
            var n1 = BareNode(1, "Start");
            var lint = worker.Invoke(() => ProjectLintService.Lint(
                new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1));
            Assert.NotNull(lint);

            Assert.True(worker.TryPost(() => throw new InvalidOperationException("thumbnail boom")));
            var deadline = Stopwatch.StartNew();
            while (deadline.ElapsedMilliseconds < 5000)
            {
                try
                {
                    // A live worker answers again; a dead one throws/timeouts.
                    worker.Invoke(() => { });
                    break;
                }
                catch (Exception)
                {
                    System.Threading.Thread.Sleep(50);
                }
            }
            int answer = worker.Invoke(() => 42);
            Assert.Equal(42, answer);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public async Task LintAsync_MatchesSyncResult()
    {
        string assets = MakeTempAssets();
        try
        {
            var n1 = BareNode(1, "Start");
            AddDialogue(n1, "A", "Hi");
            var nodes = new[] { n1 };
            var sync = ProjectLintService.Lint(nodes, Array.Empty<ConnectionViewModel>(), assets, 1);
            var async = await ProjectLintService.LintAsync(nodes, Array.Empty<ConnectionViewModel>(), assets, 1);
            Assert.Equal(
                sync.OrderBy(issue => issue.Message).ThenBy(issue => issue.NodeId),
                async.OrderBy(issue => issue.Message).ThenBy(issue => issue.NodeId));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public async Task CapturedLint_IgnoresLaterLiveGraphEdits()
    {
        string assets = MakeTempAssets();
        try
        {
            var start = BareNode(1, "Start");
            var choice = AddChoice(start, ("Unconnected", 0, true));
            var destination = BareNode(2, "Destination");
            var connection = new ConnectionViewModel(start, destination, "go");
            var captured = ProjectLintService.CaptureGraph(
                new[] { start, destination }, new[] { connection }, 1);

            choice.Options.Clear();
            start.Objects.Clear();
            connection.TargetNode = null;

            var issues = await Task.Run(() => ProjectLintService.LintCaptured(
                captured, assets, new ProjectLintOptions(CheckUnusedAssets: false)));
            Assert.Contains(issues, issue => issue.IsError &&
                issue.Message.Contains("Unconnected") && issue.Message.Contains("target 0"));
            Assert.DoesNotContain(issues, issue => issue.Message.Contains("rule failed"));
            Assert.DoesNotContain(issues, issue => issue.Message.Contains("missing source or target"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void CapturedLint_PreservesMissingConnectionWarning()
    {
        string assets = MakeTempAssets();
        try
        {
            var start = BareNode(1, "Start");
            var removed = BareNode(99, "Removed");
            var captured = ProjectLintService.CaptureGraph(
                new[] { start }, new[] { new ConnectionViewModel(start, removed) }, 1);
            var issues = ProjectLintService.LintCaptured(captured, assets);
            Assert.Contains(issues, issue => issue.IsError &&
                issue.Message.Contains("missing source or target"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void CapturedLint_PreservesInvalidStructureIssues()
    {
        string assets = MakeTempAssets();
        try
        {
            var start = BareNode(1, "Start");
            var structure = new GraphStructureDocument();
            structure.Groups.Add(new CanvasGroup("group", "Broken", "#ffffff",
                0, 0, 100, 100, new ulong[] { 99 }));
            var nodes = new[] { start };
            var connections = Array.Empty<ConnectionViewModel>();
            var live = ProjectLintService.Lint(nodes, connections, assets, 1, structure);
            var captured = ProjectLintService.CaptureGraph(nodes, connections, 1, structure);
            var detached = ProjectLintService.LintCaptured(captured, assets);
            Assert.Contains(detached, issue => issue.IsError &&
                issue.Message.Contains("references missing node #99"));
            Assert.Equal(
                live.OrderBy(issue => issue.Message).ThenBy(issue => issue.NodeId),
                detached.OrderBy(issue => issue.Message).ThenBy(issue => issue.NodeId));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Perf_2000Node_Lint_Budget()
    {
        string assets = MakeTempAssets();
        try
        {
            var nodes = new List<NodeViewModel>(2000);
            for (ulong i = 1; i <= 2000; i++)
            {
                var node = BareNode(i, "Node " + i, i * 10, 0);
                AddDialogue(node, "Narrator", "Lorem ipsum dolor sit amet.");
                nodes.Add(node);
            }
            var connections = new List<ConnectionViewModel>(1999);
            for (int i = 0; i < 1999; i++)
                connections.Add(new ConnectionViewModel(nodes[i], nodes[i + 1], "go"));

            var elapsed = Stopwatch.StartNew();
            var issues = ProjectLintService.Lint(nodes, connections, assets, 1);
            elapsed.Stop();

            Assert.True(elapsed.ElapsedMilliseconds < 8000,
                $"Validate+Lint on 2000 nodes took {elapsed.ElapsedMilliseconds} ms (budget 8000 ms).");
            Assert.Single(issues, l => !l.IsError && l.Message.Contains("terminal"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }
}
