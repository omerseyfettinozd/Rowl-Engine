using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 3 — karakter katmanları editör dilimi: serialize
/// round-trip (yeni anahtarlar + legacy migration), VM aynası, linter
/// kuralı ve expression önizlemesi. Headless, native çağrı YOKTUR
/// (sözlük + JSON fixture + kayıtçı fake motor).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorCharacterLayersSliceTests
{
    private const string DrawJson =
        "[{\"slot\":\"body\",\"asset\":\"b.png\",\"opacity\":1},{\"slot\":\"face\",\"asset\":\"f.png\",\"opacity\":0.5}]";

    private static string MakeTempAssets()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlLayers_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(root, "Assets");
        Directory.CreateDirectory(assets);
        return assets;
    }

    private static void DeleteTempAssets(string assets)
    {
        try { Directory.Delete(Path.GetDirectoryName(assets)!, recursive: true); }
        catch (Exception) { }
    }

    private sealed class RecordingPlayerEngine : IPlayerEngine
    {
        public bool IsAvailable => true;
        public string? RegisteredName;
        public string? RegisteredJson;
        public string? AppliedName;
        public readonly Dictionary<string, string> SlotAssets = new(StringComparer.Ordinal);

        public void SetPlayState(bool playing) { }
        public void SetPaused(bool paused) { }
        public void ResetToStartNode() { }
        public void Step(float dt) { }
        public void AdvanceNode(uint choiceIndex) { }
        public bool SelectChoice(string optionId) => false;
        public IReadOnlyList<string> GetActiveDialogueContentIds() => Array.Empty<string>();
        public bool HasChoices() => false;
        public IReadOnlyList<string> GetChoiceLabels() => Array.Empty<string>();
        public IReadOnlyList<string> GetChoiceOptionIds() => Array.Empty<string>();
        public string Speaker => string.Empty;
        public string Dialogue => string.Empty;
        public IReadOnlyList<DialogueHistoryEntry> History => Array.Empty<DialogueHistoryEntry>();
        public void RefreshHistory() { }
        public bool SaveSlot(int index) => false;
        public bool LoadSlot(int index) => false;
        public bool HasSlot(int index) => false;
        public bool DeleteSlot(int index) => false;
        public SaveSlotMetadata? GetSlotMetadata(int index) => null;
        public void SetMasterVolume(float value) { }
        public void SetBgmVolume(float value) { }
        public void SetVoiceVolume(float value) { }
        public void SetSfxVolume(float value) { }
        public void SetAmbienceVolume(float value) { }
        public void SetUiVolume(float value) { }
        public void SetFadeCurve(int curve) { }
        public void SetSfxPoolDepth(int depth) { }
        public void SetTextScale(float value) { }
        public void SetHighContrast(bool enabled) { }
        public void SetReducedMotion(bool enabled) { }

        public void SetCharacterSlotAsset(string slot, string asset) => SlotAssets[slot] = asset;
        public void RegisterCharacterPreset(string name, string expressionJson)
        {
            RegisteredName = name;
            RegisteredJson = expressionJson;
        }
        public void ApplyCharacterExpression(string name) => AppliedName = name;
    }

    private static Dictionary<string, object?> ToDeserializeData(Dictionary<string, object> serialized) =>
        serialized.ToDictionary(pair => pair.Key, pair => (object?)pair.Value);

    /// <summary>Hidrasyon yolunu simüle eder (ham JSON string; bkz. StoryGraphComponentHydrator).</summary>
    private static Dictionary<string, object?> ToHydratorData(Dictionary<string, object> serialized)
    {
        string json = JsonSerializer.Serialize(serialized);
        using var document = JsonDocument.Parse(json);
        var values = new Dictionary<string, object?>();
        foreach (var property in document.RootElement.EnumerateObject())
        {
            values[property.Name] = property.Value.ValueKind switch
            {
                JsonValueKind.String => property.Value.GetString(),
                JsonValueKind.Number => property.Value.GetDouble(),
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                _ => property.Value.GetRawText(),
            };
        }
        return values;
    }

    // ── Serialize round-trip ──

    [Fact]
    public void Serialize_RoundTripsLayersAndExpressions()
    {
        var character = new CharacterComponentViewModel { Sprite = "spr_evelyn.png" };
        Assert.True(character.SetLayerAsset("face", "face_base.png"));
        Assert.True(character.SetLayerAsset("outfit", "dress.png"));
        Assert.True(character.UpsertExpression("happy", "{\"face\": \"face_happy.png\"}"));
        Assert.True(character.UpsertExpression("sad", "{\"face\": \"face_sad.png\", \"accessory\": \"tear.png\"}"));

        var restored = new CharacterComponentViewModel();
        restored.Deserialize(ToDeserializeData(character.Serialize()));

        Assert.Equal("spr_evelyn.png", restored.Sprite);
        Assert.Equal(string.Empty, restored.LayerBodyAsset);
        Assert.Equal("face_base.png", restored.LayerFaceAsset);
        Assert.Equal("dress.png", restored.LayerOutfitAsset);
        Assert.Equal("happy", restored.ExpressionNames[0]);
        Assert.Equal("sad", restored.ExpressionNames[1]);
        Assert.Equal("face_happy.png", restored.ExpressionPresets["happy"]["face"]);
    }

    [Fact]
    public void Serialize_HydratorJson_RoundTripsLayersAndExpressions()
    {
        var character = new CharacterComponentViewModel { Sprite = "spr_evelyn.png" };
        character.SetLayerAsset("body", "body_custom.png");
        character.UpsertExpression("happy", "{\"face\": \"face_happy.png\"}");

        var restored = new CharacterComponentViewModel();
        restored.Deserialize(ToHydratorData(character.Serialize()));

        Assert.Equal("body_custom.png", restored.LayerBodyAsset);
        Assert.Equal("face_happy.png", restored.ExpressionPresets["happy"]["face"]);
    }

    [Fact]
    public void Deserialize_LegacySprite_FallsToBody()
    {
        var restored = new CharacterComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?>
        {
            ["sprite"] = "spr_legacy.png",
            ["x"] = 100.0,
        });
        Assert.Equal("spr_legacy.png", restored.Sprite);
        Assert.Equal("spr_legacy.png", restored.LayerBodyAsset);
        Assert.Equal(string.Empty, restored.LayerFaceAsset);
        Assert.Empty(restored.ExpressionNames);
    }

    [Fact]
    public void Deserialize_ExplicitBody_WinsOverSprite()
    {
        var restored = new CharacterComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?>
        {
            ["sprite"] = "spr_legacy.png",
            ["layers"] = "{\"body\": \"body_new.png\", \"face\": \"f.png\"}",
        });
        Assert.Equal("spr_legacy.png", restored.Sprite);
        Assert.Equal("body_new.png", restored.LayerBodyAsset);
        Assert.Equal("f.png", restored.LayerFaceAsset);
    }

    [Fact]
    public void Deserialize_BrokenLayers_FallsBackToSprite()
    {
        var restored = new CharacterComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?>
        {
            ["sprite"] = "spr_legacy.png",
            ["layers"] = "{\"face\": 42}",
        });
        Assert.Equal("spr_legacy.png", restored.LayerBodyAsset);
        Assert.Equal(string.Empty, restored.LayerFaceAsset);
    }

    [Fact]
    public void SetLayerAsset_UnknownSlotOrOversized_Rejected()
    {
        var character = new CharacterComponentViewModel();
        Assert.False(character.SetLayerAsset("tail", "t.png"));
        Assert.False(character.SetLayerAsset("Body", "b.png"));
        Assert.False(character.SetLayerAsset("face",
            new string('x', CharacterLayersService.MaxAssetPathBytes + 1)));
        Assert.Equal(string.Empty, character.LayerFaceAsset);
    }

    [Fact]
    public void UpsertExpression_BrokenInput_Rejected()
    {
        var character = new CharacterComponentViewModel();
        Assert.False(character.UpsertExpression("bad", "{\"tail\": \"t.png\"}"));
        Assert.False(character.UpsertExpression(string.Empty, "{\"face\": \"f.png\"}"));
        Assert.False(character.UpsertExpression("broken", "{ not json"));
        Assert.Empty(character.ExpressionNames);
    }

    // ── VM aynası ──

    [Fact]
    public void ViewModel_UpdateLayersBadge_MirrorsService()
    {
        var character = new CharacterComponentViewModel();
        Assert.False(character.IsLayersBadgeVisible);
        Assert.Equal(string.Empty, character.LayersBadgeText);

        character.UpdateLayersBadge(DrawJson);
        Assert.True(character.IsLayersBadgeVisible);
        Assert.Contains("2/4", character.LayersBadgeText);

        character.UpdateLayersBadge("{ broken");
        Assert.False(character.IsLayersBadgeVisible);
        Assert.Equal(string.Empty, character.LayersBadgeText);
    }

    // ── Linter ──

    [Fact]
    public void Lint_MissingLayerAsset_WarnsWithoutError()
    {
        string assets = MakeTempAssets();
        try
        {
            var node = new NodeViewModel(1, "Char 1", 0, 0, bare: true);
            var character = node.AddComponent<CharacterComponentViewModel>();
            character.SetLayerAsset("face", "face_missing.png");
            var issues = ProjectLintService.Lint(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            var warning = issues.FirstOrDefault(i =>
                !i.IsError && i.Message.Contains("face_missing.png"));
            Assert.NotNull(warning);
            Assert.Contains("face", warning!.Message);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Validate_MissingLayerAsset_OwnsTheError()
    {
        string assets = MakeTempAssets();
        try
        {
            var node = new NodeViewModel(1, "Char 1", 0, 0, bare: true);
            var character = node.AddComponent<CharacterComponentViewModel>();
            character.Sprite = "shared_missing.png";
            character.SetLayerAsset("body", "shared_missing.png");
            character.SetLayerAsset("face", "face_missing.png");
            var issues = ProjectValidationService.Validate(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            Assert.Contains(issues, i => i.IsError && i.Message.Contains("face_missing.png"));
            // Aynı component'in sprite == body durumu iki kez raporlanmaz.
            Assert.Single(issues, i => i.IsError && i.Message.Contains("shared_missing.png"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_ExistingLayerAssets_StaySilentAndReferenced()
    {
        string assets = MakeTempAssets();
        try
        {
            File.WriteAllText(Path.Combine(assets, "face_ok.png"), "x");
            File.WriteAllText(Path.Combine(assets, "spr_ok.png"), "x");
            var node = new NodeViewModel(1, "Char 1", 0, 0, bare: true);
            var character = node.AddComponent<CharacterComponentViewModel>();
            character.Sprite = "spr_ok.png";
            character.SetLayerAsset("body", "spr_ok.png");
            character.SetLayerAsset("face", "face_ok.png");
            character.UpsertExpression("happy", "{\"face\": \"face_ok.png\"}");
            var issues = ProjectLintService.Lint(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            Assert.DoesNotContain(issues, i => i.Message.Contains("face_ok.png"));
            Assert.DoesNotContain(issues, i => i.Message.Contains("spr_ok.png"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void LintOptions_CharacterLayersDefaultsToTrue()
    {
        Assert.True(new ProjectLintOptions().CheckCharacterLayers);
    }

    // ── Önizleme: expression uygula / geri-al ──

    [Fact]
    public void Preview_ApplyExpression_UpdatesLayersAndForwards()
    {
        var character = new CharacterComponentViewModel();
        character.SetLayerAsset("face", "face_base.png");
        character.UpsertExpression("happy", "{\"face\": \"face_happy.png\"}");
        var engine = new RecordingPlayerEngine();

        Assert.True(character.ApplyExpression("happy", engine, out string error));
        Assert.Equal(string.Empty, error);
        Assert.Equal("face_happy.png", character.LayerFaceAsset);
        Assert.False(character.IsLayersBadgeVisible);
        Assert.Equal("happy", engine.RegisteredName);
        Assert.Equal("happy", engine.AppliedName);
        Assert.Contains("face_happy.png", engine.RegisteredJson ?? string.Empty);
    }

    [Fact]
    public void Preview_ApplyExpression_RollsBackOnUnresolvable()
    {
        var character = new CharacterComponentViewModel();
        character.SetLayerAsset("face", "face_base.png");
        character.SetLayerAsset("outfit", "dress.png");
        character.UpsertExpression("party", "{\"face\": \"face_party.png\", \"accessory\": \"hat_missing.png\"}");

        bool ok = character.ApplyExpression(
            "party", null, out string error,
            isResolvable: asset => asset != "hat_missing.png");

        Assert.False(ok);
        Assert.NotEqual(string.Empty, error);
        Assert.Equal("face_base.png", character.LayerFaceAsset);
        Assert.Equal("dress.png", character.LayerOutfitAsset);
        Assert.Equal(string.Empty, character.LayerAccessoryAsset);
        Assert.True(character.IsLayersBadgeVisible);
        Assert.NotEqual(string.Empty, character.LayersBadgeText);
    }

    [Fact]
    public void Preview_ApplyUnknownExpression_FailsClosedWithBadge()
    {
        var character = new CharacterComponentViewModel();
        character.SetLayerAsset("face", "face_base.png");
        Assert.False(character.ApplyExpression("nope", null, out string error));
        Assert.NotEqual(string.Empty, error);
        Assert.Equal("face_base.png", character.LayerFaceAsset);
        Assert.True(character.IsLayersBadgeVisible);
    }

    [Fact]
    public void Adapter_DeadHandle_CharacterForwardingIsSilentNoOp()
    {
        // Native yok: başlatılmamış EngineHost'ta 6 çağrı da throw
        // atmadan sessizce yoksayılır (fail-closed).
        using var host = new EngineHost();
        var adapter = new EngineHostPlayerAdapter(host);
        Assert.False(adapter.IsAvailable);
        var exception = Record.Exception(() =>
        {
            adapter.SetCharacterSlotAsset("face", "f.png");
            adapter.SetCharacterSlotAsset("tail", "t.png");
            adapter.SetCharacterSlotAsset("face", new string('x', 5000));
            adapter.SetCharacterSlotOpacity("face", 0.5f);
            adapter.SetCharacterSlotOpacity("face", float.NaN);
            adapter.SetCharacterSlotVisible("face", true);
            adapter.RegisterCharacterPreset("happy", "{\"face\": \"f.png\"}");
            adapter.RegisterCharacterPreset("bad", "{\"tail\": \"t.png\"}");
            adapter.ApplyCharacterExpression("happy");
            adapter.ApplyCharacterExpression(string.Empty);
        });
        Assert.Null(exception);
        Assert.Equal(string.Empty, adapter.GetLastCharacterError());
    }
}
