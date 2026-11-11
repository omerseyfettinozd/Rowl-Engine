using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Inspector;
using RowlEngine.Editor.Services.Search;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.ViewModels.Inspector;
using Avalonia.Threading;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D06-2 — authoring surfaces the native runtime already honours: the audio
/// component's voice line (<c>voice_track</c>), the native transition-kind
/// list, and the lint rules for the D06-1 condition component and voice files.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorD062AuthoringSurfaceTests
{
    // ── Voice line: audio VM ───────────────────────────────────────────────

    [Fact]
    public void Voice_RoundTripsThroughSerializeAndDeserialize()
    {
        var source = new AudioComponentViewModel { VoiceTrack = "ayse_01.ogg", BgmTrack = "theme.ogg" };

        var data = source.Serialize();
        Assert.Equal("ayse_01.ogg", data["voice_track"]);

        var json = JsonSerializer.Serialize(data);
        var parsed = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(json)!
            .ToDictionary(p => p.Key, p => (object?)(p.Value.ValueKind == JsonValueKind.String ? p.Value.GetString() : p.Value.GetRawText()));
        var restored = new AudioComponentViewModel();
        restored.Deserialize(parsed);

        Assert.Equal("ayse_01.ogg", restored.VoiceTrack);
        Assert.Equal("theme.ogg", restored.BgmTrack);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void Voice_EmptyValueWritesNoKey(string value)
    {
        var audio = new AudioComponentViewModel { VoiceTrack = value };
        Assert.False(audio.Serialize().ContainsKey("voice_track"));
    }

    [Fact]
    public void Voice_MissingKeyOnLoadClearsTheField()
    {
        var audio = new AudioComponentViewModel { VoiceTrack = "stale.ogg" };
        audio.Deserialize(new Dictionary<string, object?> { ["bgm_track"] = "x.ogg" });
        Assert.Equal("", audio.VoiceTrack);
    }

    [Fact]
    public void Voice_PickerAssignsVoiceTrackOnly()
    {
        var audio = new AudioComponentViewModel { BgmTrack = "keep.ogg" };
        Assert.True(EditorAudioAssetPickerService.ApplyAudioTrackToComponent(audio, "line.ogg", "voice"));
        Assert.Equal("line.ogg", audio.VoiceTrack);
        Assert.Equal("keep.ogg", audio.BgmTrack);
        Assert.Equal("", audio.SfxTrack);
    }

    [Fact]
    public void Voice_PreviewUsesVoiceChannelAndSkipsEmptyTrack()
    {
        var previous = AudioComponentViewModel.GlobalPreviewAudioAction;
        var calls = new List<(string path, int channel)>();
        AudioComponentViewModel.GlobalPreviewAudioAction = (path, channel, _) => calls.Add((path, channel));
        try
        {
            var audio = new AudioComponentViewModel();
            audio.ToggleVoicePreview();
            Assert.Empty(calls);
            Assert.False(audio.IsPreviewingVoice);

            audio.VoiceTrack = "v.ogg";
            audio.ToggleVoicePreview();
            Assert.True(audio.IsPreviewingVoice);
            Assert.Equal(("v.ogg", 1), Assert.Single(calls));

            audio.StopAllPreview();
            Assert.False(audio.IsPreviewingVoice);
        }
        finally
        {
            AudioComponentViewModel.GlobalPreviewAudioAction = previous;
        }
    }

    [Fact]
    public void Voice_FieldIsInTheInspectorSchemaAndSearch()
    {
        Assert.Contains(InspectorSchemaProvider.ForComponent("audio"),
            f => f.Key == InspectorFieldDescriptor.Keys.AudioVoiceTrack && f.Kind == InspectorFieldKind.Asset);

        var node = new NodeViewModel(1, "N", 0, 0, bare: true);
        var audio = node.CreateObject("Audio").AddComponent<AudioComponentViewModel>();
        audio.VoiceTrack = "needle_voice.ogg";
        Assert.Contains("needle_voice.ogg", NodeSearchDocument.Build(node).Assets);
    }

    // ── Voice line reaches the native engine (end to end) ──────────────────

    private static byte[] Wav(int seconds)
    {
        const int rate = 22050;
        int dataBytes = rate * 2 * seconds;
        using var ms = new MemoryStream();
        using var w = new BinaryWriter(ms, Encoding.ASCII, leaveOpen: true);
        w.Write(Encoding.ASCII.GetBytes("RIFF")); w.Write(36 + dataBytes); w.Write(Encoding.ASCII.GetBytes("WAVE"));
        w.Write(Encoding.ASCII.GetBytes("fmt ")); w.Write(16); w.Write((short)1); w.Write((short)1);
        w.Write(rate); w.Write(rate * 2); w.Write((short)2); w.Write((short)16);
        w.Write(Encoding.ASCII.GetBytes("data")); w.Write(dataBytes); w.Write(new byte[dataBytes]);
        w.Flush();
        return ms.ToArray();
    }

    /// <summary>
    /// Authors node 1 -> node 2 with the editor's own view models and graph
    /// saver, mounts the project in the native engine, enters node 2 and
    /// returns the backlog row JSON of node 2 (the row carries "voice" when
    /// the native <c>voice_track</c> read found the editor-written key).
    /// </summary>
    private static string EnterNodeTwoAndReadHistoryRow(string? voiceTrack)
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlD062_" + Guid.NewGuid().ToString("N"));
        string previousXdg = Environment.GetEnvironmentVariable("XDG_DATA_HOME") ?? "";
        try
        {
            string assets = Path.Combine(root, "Assets");
            Directory.CreateDirectory(Path.Combine(assets, "json"));
            Directory.CreateDirectory(Path.Combine(assets, "audio"));
            File.WriteAllText(Path.Combine(root, "project.rowlproj"),
                "{\"project_uuid\":\"7a1e0a64-5b2c-4d7e-9a10-0c2b7d5e8f23\",\"name\":\"D062\"}");
            File.WriteAllBytes(Path.Combine(assets, "audio", "ayse_01.wav"), Wav(2));
            Environment.SetEnvironmentVariable("XDG_DATA_HOME", Path.Combine(root, "xdg"));

            var n1 = new NodeViewModel(1, "Start", 0, 0, bare: true);
            n1.CreateObject("Box").AddComponent(new DialogueComponentViewModel { Speaker = "A", DialogueText = "First line.", TypewriterEnabled = false });
            var n2 = new NodeViewModel(2, "Voiced", 0, 0, bare: true);
            var audio = n2.CreateObject("Audio").AddComponent<AudioComponentViewModel>();
            if (voiceTrack is not null)
                audio.VoiceTrack = voiceTrack;
            n2.CreateObject("Box").AddComponent(new DialogueComponentViewModel { Speaker = "A", DialogueText = "Second line.", TypewriterEnabled = false });
            var snapshot = StoryGraphSaveService.Capture(
                new[] { n1, n2 }, new[] { new ConnectionViewModel(n1, n2, "go") }, 1, activeNode: null);
            File.WriteAllText(Path.Combine(assets, "json", "full_story_graph.json"),
                StoryGraphSaveService.SerializeFullGraph(snapshot));

            NativeEnvironment.EnsureDisplayFreeDrivers();
            using var host = new EngineHost();
            Assert.True(host.Initialize(64, 64, false), host.LastError);
            Assert.True(host.SetProjectDirectoryChecked(root, out string detail), detail);
            host.SetPlayState(true);
            host.Step(0.016f);
            Assert.Equal(1UL, host.GetCurrentNodeId());
            host.AdvanceNode(0);
            host.Step(0.016f);
            Assert.Equal(2UL, host.GetCurrentNodeId());

            string history = host.Runtime!.Invoke(h => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetDialogueHistoryJsonWithLength(h, out uint len), len));
            using var doc = JsonDocument.Parse(history);
            var row = doc.RootElement.EnumerateArray().Single(e => e.GetProperty("node_id").GetUInt64() == 2);
            return row.GetRawText();
        }
        finally
        {
            Environment.SetEnvironmentVariable("XDG_DATA_HOME", previousXdg);
            try { Directory.Delete(root, recursive: true); } catch (Exception) { }
        }
    }

    [Fact]
    public void Voice_EditorWrittenKeyIsPlayedByTheNativeEngine()
    {
        string row = EnterNodeTwoAndReadHistoryRow("ayse_01.wav");
        using var doc = JsonDocument.Parse(row);
        Assert.True(doc.RootElement.TryGetProperty("voice", out var voice), "row must carry the voice clip: " + row);
        Assert.Equal("ayse_01.wav", voice.GetString());
    }

    [Fact]
    public void Voice_WithoutTheFieldNativeRowCarriesNoClip()
    {
        string row = EnterNodeTwoAndReadHistoryRow(null);
        using var doc = JsonDocument.Parse(row);
        Assert.False(doc.RootElement.TryGetProperty("voice", out var voice) && !string.IsNullOrEmpty(voice.GetString()),
            "control: no VoiceTrack, no clip: " + row);
    }

    // ── Transition kinds ───────────────────────────────────────────────────

    private static readonly string[] HistoricKinds =
        { "crossfade", "fade_black", "fade_white", "fade_color", "wipe_left", "wipe_right" };

    [Fact]
    public void Transitions_NativeListIsUsedAndCoversTheHistoricSix()
    {
        var kinds = TransitionKindCatalog.Kinds;
        foreach (string historic in HistoricKinds)
            Assert.Contains(historic, kinds);
        // The native table has 13 kinds (transition_manager.cpp supportedKindsJson).
        foreach (string added in new[] { "dissolve", "push_left", "push_right", "push_up", "push_down", "iris_in", "iris_out" })
            Assert.Contains(added, kinds);
        Assert.Equal(kinds.Count, kinds.Distinct().Count());
        Assert.Equal(kinds, TransitionComponentViewModel.AvailableKinds);
        Assert.Equal(kinds, TransitionComponentViewModel.KindOptions.Select(o => o.Kind));
    }

    [Fact]
    public void Transitions_FallbackWhenNativeIsMissingOrUnusable()
    {
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(null));
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(() => null));
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(() => ""));
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(() => "[]"));
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(() => "{\"a\":1}"));
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(() => "not json"));
        Assert.Equal(HistoricKinds, TransitionKindCatalog.Resolve(() => throw new DllNotFoundException()));
    }

    [Fact]
    public void Transitions_ParseKeepsOrderAndDropsJunk()
    {
        var kinds = TransitionKindCatalog.Resolve(() => "[\"dissolve\", 5, \"\", \" iris_in \", \"dissolve\", null, \"future_kind\"]");
        Assert.Equal(new[] { "dissolve", "iris_in", "future_kind" }, kinds);
    }

    [Fact]
    public void Transitions_EveryKindHasAFilledDisplayNameInBothLanguages()
    {
        string language = EditorStrings.Instance.Language;
        try
        {
            foreach (string lang in new[] { "tr", "en" })
            {
                EditorStrings.Instance.SetLanguage(lang);
                foreach (var option in TransitionKindCatalog.Options)
                {
                    Assert.False(string.IsNullOrWhiteSpace(option.DisplayName), $"{lang}/{option.Kind}");
                    Assert.NotEqual(option.Kind, option.DisplayName);
                }
            }
            EditorStrings.Instance.SetLanguage("tr");
            Assert.Equal("Sola itme", TransitionKindCatalog.DisplayNameOf("push_left"));
            EditorStrings.Instance.SetLanguage("en");
            Assert.Equal("Push left", TransitionKindCatalog.DisplayNameOf("push_left"));
            // A kind the editor has no string for still shows something.
            Assert.Equal("future_kind", TransitionKindCatalog.DisplayNameOf("future_kind"));
        }
        finally
        {
            EditorStrings.Instance.SetLanguage(language);
        }
    }

    [Fact]
    public void Transitions_DisplayNameFollowsLanguageSwitch()
    {
        string language = EditorStrings.Instance.Language;
        try
        {
            EditorStrings.Instance.SetLanguage("tr");
            var option = TransitionKindCatalog.FindOption("dissolve")!;
            var changed = new List<string?>();
            option.PropertyChanged += (_, e) => changed.Add(e.PropertyName);
            EditorStrings.Instance.SetLanguage("en");
            Assert.Contains(nameof(TransitionKindOption.DisplayName), changed);
            Assert.Equal("Dissolve", option.DisplayName);
        }
        finally
        {
            EditorStrings.Instance.SetLanguage(language);
        }
    }

    [Fact]
    public void Transitions_NewKindRoundTripsAndPickerSelectionFollowsKind()
    {
        var transition = new TransitionComponentViewModel();
        transition.SelectedKindOption = TransitionKindCatalog.FindOption("iris_out");
        Assert.Equal("iris_out", transition.Kind);
        Assert.Equal("iris_out", transition.Serialize()["kind"]);

        var restored = new TransitionComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?> { ["kind"] = "push_down", ["duration"] = 0.5 });
        Assert.Equal("push_down", restored.SelectedKindOption?.Kind);

        // A native alias shows as its canonical entry; the stored value stays.
        restored.Kind = "fade";
        Assert.Equal("crossfade", restored.SelectedKindOption?.Kind);
        Assert.Equal("fade", restored.Kind);

        var seen = new List<string?>();
        restored.PropertyChanged += (_, e) => seen.Add(e.PropertyName);
        restored.Kind = "dissolve";
        Assert.Contains(nameof(TransitionComponentViewModel.SelectedKindOption), seen);
    }

    [Fact]
    public void SceneInspector_CardListNeverClearsAKindOutsideItsFourCards()
    {
        // The scene inspector's card list only knows four kinds. For any other
        // kind (wipe_right, dissolve, ...) the ListBox reports an empty
        // selection; that must not be written back over the node's kind.
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlD062Scene_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("D062Scene", parent);
        Assert.True(created.Success, created.Error);
        string root = created.Info!.Path;
        MainWindowViewModel vm = null!;
        try
        {
            Dispatcher.UIThread.Invoke(() =>
            {
                vm = new MainWindowViewModel(root, connectEngine: false);
                vm.Nodes.Clear();
                vm.Connections.Clear();
                var node = new NodeViewModel(1, "A", 0, 0, bare: true);
                vm.Nodes.Add(node);
                var transition = node.CreateObject("T").AddComponent<TransitionComponentViewModel>();
                transition.Kind = "dissolve";
                vm.SelectedNode = node;
                var inspector = new InspectorSceneViewModel(vm);
                try
                {
                    inspector.TransitionKind = string.Empty;
                    Assert.Equal("dissolve", transition.Kind);
                    inspector.TransitionKind = null!;
                    Assert.Equal("dissolve", transition.Kind);
                    inspector.TransitionKind = "fade_black";
                    Assert.Equal("fade_black", transition.Kind);
                }
                finally { inspector.Dispose(); }
            });
        }
        finally
        {
            try { Dispatcher.UIThread.Invoke(() => vm?.Dispose()); } catch (Exception) { }
            try { Directory.Delete(parent, recursive: true); } catch (Exception) { }
            if (string.Equals(MainWindowViewModel.ProjectRoot, root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = previousRoot;
        }
    }

    // ── Lint ───────────────────────────────────────────────────────────────

    private static string MakeAssets()
    {
        string assets = Path.Combine(Path.GetTempPath(), "RowlD062Lint_" + Guid.NewGuid().ToString("N"), "Assets");
        foreach (string sub in new[] { "", "images", "audio", "fonts", "scripts", "locales", "json" })
            Directory.CreateDirectory(Path.Combine(assets, sub));
        return assets;
    }

    private static void Cleanup(string assets)
    {
        try { Directory.Delete(Path.GetDirectoryName(assets)!, recursive: true); } catch (Exception) { }
    }

    private static IReadOnlyList<ProjectValidationIssue> Lint(string assets, params NodeViewModel[] nodes) =>
        ProjectLintService.Lint(nodes, Array.Empty<ConnectionViewModel>(), assets, 1,
            lintOptions: new ProjectLintOptions(CheckUnusedAssets: false));

    private static ConditionComponentViewModel AddCondition(NodeViewModel node, string expression, string failTarget)
    {
        var condition = node.CreateObject("Cond").AddComponent<ConditionComponentViewModel>();
        condition.Expression = expression;
        condition.FailTargetNodeId = failTarget;
        return condition;
    }

    [Fact]
    public void Lint_ConditionEmptyExpression_Warns()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            var n2 = new NodeViewModel(2, "Other", 0, 0, bare: true);
            AddCondition(n1, "   ", "2");
            AddCondition(n2, "gold >= 1", "1");
            var issues = Lint(assets, n1, n2);
            var hit = Assert.Single(issues, i => i.Message.Contains("empty expression"));
            Assert.False(hit.IsError);
            Assert.Equal(1UL, hit.NodeId);
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Lint_ConditionWithoutFailTarget_WarnsThatNodeStillPlays()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            AddCondition(n1, "gold >= 10", "");
            var hit = Assert.Single(Lint(assets, n1), i => i.Message.Contains("no fail target"));
            Assert.False(hit.IsError);
            Assert.Contains("still plays", hit.Message);
            Assert.Equal(1UL, hit.NodeId);

            // Empty expression reports the empty expression only; the missing
            // target of a condition that gates nothing is not extra noise.
            var n2 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            AddCondition(n2, "", "");
            var issues = Lint(assets, n2);
            Assert.Contains(issues, i => i.Message.Contains("empty expression"));
            Assert.DoesNotContain(issues, i => i.Message.Contains("no fail target"));
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Lint_ConditionTargetingItself_Warns()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            AddCondition(n1, "gold >= 10", "1");
            var hit = Assert.Single(Lint(assets, n1), i => i.Message.Contains("the node itself"));
            Assert.False(hit.IsError);
            Assert.Equal(1UL, hit.NodeId);
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Lint_ConditionTargetMissingNode_StaysAnError_ValidConditionIsClean()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            AddCondition(n1, "gold >= 10", "77");
            Assert.Contains(Lint(assets, n1), i => i.IsError && i.Message.Contains("deleted node #77"));

            var good1 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            var good2 = new NodeViewModel(2, "Fallback", 0, 0, bare: true);
            AddCondition(good1, "gold >= 10", "2");
            var issues = Lint(assets, good1, good2);
            Assert.DoesNotContain(issues, i => i.Message.Contains("condition"));
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Lint_DisabledConditionIsIgnored()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Gate", 0, 0, bare: true);
            AddCondition(n1, "", "1").IsEnabled = false;
            Assert.DoesNotContain(Lint(assets, n1), i => i.Message.Contains("condition"));
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Lint_MissingVoiceFile_IsReportedLikeAMissingBgm_PresentFileIsClean()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Voiced", 0, 0, bare: true);
            var audio = n1.CreateObject("Audio").AddComponent<AudioComponentViewModel>();
            audio.VoiceTrack = "ghost_voice.ogg";
            audio.BgmTrack = "ghost_bgm.ogg";
            var issues = Lint(assets, n1);
            var voice = Assert.Single(issues, i => i.IsError && i.Message.Contains("ghost_voice.ogg"));
            var bgm = Assert.Single(issues, i => i.IsError && i.Message.Contains("ghost_bgm.ogg"));
            Assert.Equal(bgm.IsError, voice.IsError);
            Assert.True(voice.IsError);

            File.WriteAllBytes(Path.Combine(assets, "audio", "ghost_voice.ogg"), new byte[] { 1 });
            Assert.DoesNotContain(Lint(assets, n1), i => i.Message.Contains("ghost_voice.ogg"));
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Lint_ReferencedVoiceFileIsNotUnused_UnreferencedOneIs()
    {
        string assets = MakeAssets();
        try
        {
            File.WriteAllBytes(Path.Combine(assets, "audio", "used_voice.ogg"), new byte[] { 1 });
            File.WriteAllBytes(Path.Combine(assets, "audio", "orphan_voice.ogg"), new byte[] { 1 });
            var n1 = new NodeViewModel(1, "Voiced", 0, 0, bare: true);
            n1.CreateObject("Audio").AddComponent<AudioComponentViewModel>().VoiceTrack = "used_voice.ogg";
            var issues = ProjectLintService.Lint(new[] { n1 }, Array.Empty<ConnectionViewModel>(), assets, 1);
            Assert.DoesNotContain(issues, i => i.Message.Contains("used_voice.ogg"));
            Assert.Contains(issues, i => i.Message.Contains("orphan_voice.ogg"));
        }
        finally { Cleanup(assets); }
    }

    [Fact]
    public void Inline_MissingVoiceFile_GetsAFieldBadgeKey()
    {
        string assets = MakeAssets();
        try
        {
            var n1 = new NodeViewModel(1, "Voiced", 0, 0, bare: true);
            n1.CreateObject("Audio").AddComponent<AudioComponentViewModel>().VoiceTrack = "ghost_voice.ogg";
            var nodes = new ObservableCollection<NodeViewModel> { n1 };
            var service = new InspectorValidationService();
            service.Attach(nodes, null, () => assets);
            var hits = service.IssuesFor(1, InspectorFieldDescriptor.Keys.AudioVoiceTrack);
            Assert.Contains(hits, i => i.IsError && i.Message.Contains("ghost_voice.ogg"));
        }
        finally { Cleanup(assets); }
    }
}
