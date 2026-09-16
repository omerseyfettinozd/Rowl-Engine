using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 3 — <see cref="CharacterLayersService"/> dilim testleri:
/// slot sırası, atomiklik kararı, preset parse, asset clamp, delege
/// fail-closed ve rozet. Headless, native çağrı YOKTUR.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorCharacterLayersServiceTests
{
    // ── Slot sırası sabiti ──

    [Fact]
    public void SlotOrder_IsBodyFaceOutfitAccessory()
    {
        Assert.Equal(new[] { "body", "face", "outfit", "accessory" }, CharacterLayersService.SlotOrder);
        Assert.True(CharacterLayersService.TryGetSlotIndex("body", out int body) && body == 0);
        Assert.True(CharacterLayersService.TryGetSlotIndex("face", out int face) && face == 1);
        Assert.True(CharacterLayersService.TryGetSlotIndex("outfit", out int outfit) && outfit == 2);
        Assert.True(CharacterLayersService.TryGetSlotIndex("accessory", out int accessory) && accessory == 3);
    }

    [Fact]
    public void SlotIndex_UnknownOrWrongCase_Rejected()
    {
        Assert.False(CharacterLayersService.TryGetSlotIndex("tail", out _));
        Assert.False(CharacterLayersService.TryGetSlotIndex("Body", out _));
        Assert.False(CharacterLayersService.TryGetSlotIndex("FACE", out _));
        Assert.False(CharacterLayersService.TryGetSlotIndex(null, out _));
        Assert.False(CharacterLayersService.TryGetSlotIndex(string.Empty, out _));
        Assert.False(CharacterLayersService.IsKnownSlot("tail"));
        Assert.True(CharacterLayersService.IsKnownSlot("body"));
    }

    // ── Atomiklik kararı ──

    private static Dictionary<string, string> Current() => new(StringComparer.Ordinal)
    {
        ["body"] = "body.png",
        ["face"] = "face.png",
        ["outfit"] = string.Empty,
        ["accessory"] = string.Empty,
    };

    [Fact]
    public void Atomicity_CleanPreset_AppliesAndLeavesMissingUntouched()
    {
        var preset = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["face"] = "happy.png",
            ["outfit"] = "jacket.png",
        };
        Assert.True(CharacterLayersService.TryBuildExpressionResult(
            Current(), preset, out Dictionary<string, string> next, out string error));
        Assert.Equal(string.Empty, error);
        Assert.Equal("body.png", next["body"]);
        Assert.Equal("happy.png", next["face"]);
        Assert.Equal("jacket.png", next["outfit"]);
        Assert.Equal(string.Empty, next["accessory"]);
    }

    [Fact]
    public void Atomicity_BrokenSlotPresent_AppliesNothing()
    {
        var preset = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["face"] = "happy.png",
            ["outfit"] = "jacket.png",
            ["accessory"] = new string('x', CharacterLayersService.MaxAssetPathBytes + 1) + ".png",
        };
        var before = Current();
        Assert.False(CharacterLayersService.TryBuildExpressionResult(
            before, preset, out Dictionary<string, string> next, out string error));
        Assert.NotEqual(string.Empty, error);
        Assert.Equal(before, next);
    }

    [Fact]
    public void Atomicity_UnknownSlot_AppliesNothing()
    {
        var preset = new Dictionary<string, string>(StringComparer.Ordinal) { ["tail"] = "t.png" };
        var before = Current();
        Assert.False(CharacterLayersService.TryBuildExpressionResult(
            before, preset, out Dictionary<string, string> next, out string error));
        Assert.NotEqual(string.Empty, error);
        Assert.Equal(before, next);
    }

    [Fact]
    public void Atomicity_EmptyPreset_AppliesNothing()
    {
        var before = Current();
        Assert.False(CharacterLayersService.TryBuildExpressionResult(
            before, new Dictionary<string, string>(), out Dictionary<string, string> next, out string error));
        Assert.NotEqual(string.Empty, error);
        Assert.Equal(before, next);
    }

    [Fact]
    public void Atomicity_ResolverRejectsOne_AppliesNothing()
    {
        var preset = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["face"] = "happy.png",
            ["outfit"] = "missing.png",
        };
        var before = Current();
        Assert.False(CharacterLayersService.TryBuildExpressionResult(
            before, preset, out Dictionary<string, string> next, out string error,
            isResolvable: asset => asset != "missing.png"));
        Assert.NotEqual(string.Empty, error);
        Assert.Equal(before, next);
    }

    [Fact]
    public void Atomicity_ThrowingResolver_FailsClosed()
    {
        var preset = new Dictionary<string, string>(StringComparer.Ordinal) { ["face"] = "happy.png" };
        var before = Current();
        Assert.False(CharacterLayersService.TryBuildExpressionResult(
            before, preset, out Dictionary<string, string> next, out _,
            isResolvable: _ => throw new InvalidOperationException("disk down")));
        Assert.Equal(before, next);
    }

    [Fact]
    public void Atomicity_NullResolver_IsSyntaxOnly()
    {
        var preset = new Dictionary<string, string>(StringComparer.Ordinal) { ["face"] = "happy.png" };
        Assert.True(CharacterLayersService.TryBuildExpressionResult(
            Current(), preset, out Dictionary<string, string> next, out _, isResolvable: null));
        Assert.Equal("happy.png", next["face"]);
    }

    // ── Preset parse ──

    [Fact]
    public void PresetParse_ValidJson_MapsSlots()
    {
        Assert.True(CharacterLayersService.TryParsePreset(
            "{\"face\": \"happy.png\", \"body\": \"\"}",
            out Dictionary<string, string> slots));
        Assert.Equal("happy.png", slots["face"]);
        Assert.Equal(string.Empty, slots["body"]);
    }

    [Fact]
    public void PresetParse_BrokenInput_FailsClosed()
    {
        Assert.False(CharacterLayersService.TryParsePreset("{ not json", out Dictionary<string, string> malformed));
        Assert.Empty(malformed);
        Assert.False(CharacterLayersService.TryParsePreset("[\"face\"]", out Dictionary<string, string> array));
        Assert.Empty(array);
        Assert.False(CharacterLayersService.TryParsePreset(null, out _));
        Assert.False(CharacterLayersService.TryParsePreset("  ", out _));
        // Bilinmeyen slot anahtarı TÜM kaydı reddeder.
        Assert.False(CharacterLayersService.TryParsePreset("{\"tail\": \"t.png\"}", out Dictionary<string, string> unknown));
        Assert.Empty(unknown);
        // Non-string değer TÜM kaydı reddeder.
        Assert.False(CharacterLayersService.TryParsePreset("{\"face\": 42}", out Dictionary<string, string> mistyped));
        Assert.Empty(mistyped);
        // Aşırı asset TÜM kaydı reddeder.
        Assert.False(CharacterLayersService.TryParsePreset(
            "{\"face\": \"" + new string('y', CharacterLayersService.MaxAssetPathBytes + 1) + "\"}",
            out Dictionary<string, string> oversized));
        Assert.Empty(oversized);
    }

    [Fact]
    public void PresetParse_Broken_ReturnsEmptyWithoutThrow()
    {
        var snapshot = CharacterLayersService.ParsePreset("{ broken");
        Assert.NotNull(snapshot);
        Assert.Empty(snapshot);
    }

    // ── Layers parse ──

    [Fact]
    public void LayersParse_ShorthandAndObjectForms()
    {
        Assert.True(CharacterLayersService.TryParseLayers(
            "{\"face\": \"f.png\", \"outfit\": {\"asset\": \"o.png\", \"opacity\": 0.5, \"visible\": true}}",
            out Dictionary<string, string> slots));
        Assert.Equal("f.png", slots["face"]);
        Assert.Equal("o.png", slots["outfit"]);
    }

    [Fact]
    public void LayersParse_UnknownKeysIgnored_MistypedRejected()
    {
        Assert.True(CharacterLayersService.TryParseLayers(
            "{\"face\": \"f.png\", \"tail\": \"t.png\"}",
            out Dictionary<string, string> slots));
        Assert.Equal("f.png", slots["face"]);
        Assert.False(slots.ContainsKey("tail"));

        Assert.False(CharacterLayersService.TryParseLayers(
            "{\"face\": 42}", out Dictionary<string, string> rejected));
        Assert.Empty(rejected);
        Assert.False(CharacterLayersService.TryParseLayers(null, out _));
    }

    // ── Asset clamp / sözdizimi ──

    [Fact]
    public void AssetSyntax_EmptyValid_NulAndOversizedRejected()
    {
        Assert.True(CharacterLayersService.IsAssetPathSyntaxValid(null));
        Assert.True(CharacterLayersService.IsAssetPathSyntaxValid(string.Empty));
        Assert.True(CharacterLayersService.IsAssetPathSyntaxValid("images/hero.png"));
        Assert.False(CharacterLayersService.IsAssetPathSyntaxValid("a\0b.png"));
        Assert.False(CharacterLayersService.IsAssetPathSyntaxValid(
            new string('z', CharacterLayersService.MaxAssetPathBytes + 1)));
        Assert.True(CharacterLayersService.IsAssetPathSyntaxValid(
            new string('z', CharacterLayersService.MaxAssetPathBytes)));
    }

    [Fact]
    public void ClampAssetPath_TruncatesTo4096BytesWithoutSplittingUtf8()
    {
        string clamped = CharacterLayersService.ClampAssetPath(new string('a', 5000));
        Assert.Equal(CharacterLayersService.MaxAssetPathBytes, clamped.Length);
        Assert.Equal(CharacterLayersService.MaxAssetPathBytes,
            Encoding.UTF8.GetByteCount(clamped));

        // Çok baytlı karakter bölünmez: 3000 × 'é' (6000 bayt) → 2048 × 'é'.
        string wide = CharacterLayersService.ClampAssetPath(new string('é', 3000));
        Assert.Equal(CharacterLayersService.MaxAssetPathBytes, Encoding.UTF8.GetByteCount(wide));
        Assert.Equal(new string('é', 2048), wide);

        Assert.Equal(string.Empty, CharacterLayersService.ClampAssetPath(null));
        Assert.Equal("short.png", CharacterLayersService.ClampAssetPath("short.png"));
    }

    [Fact]
    public void PresetName_EmptyAndOversizedRejected()
    {
        Assert.True(CharacterLayersService.IsPresetNameValid("happy"));
        Assert.False(CharacterLayersService.IsPresetNameValid(null));
        Assert.False(CharacterLayersService.IsPresetNameValid(string.Empty));
        Assert.False(CharacterLayersService.IsPresetNameValid(
            new string('n', CharacterLayersService.MaxPresetNameBytes + 1)));
    }

    [Fact]
    public void AcceptOpacity_NonFiniteIgnored_FiniteClamped()
    {
        Assert.Equal(0.4f, CharacterLayersService.AcceptOpacity(0.4f, float.NaN));
        Assert.Equal(0.4f, CharacterLayersService.AcceptOpacity(0.4f, float.PositiveInfinity));
        Assert.Equal(1.0f, CharacterLayersService.AcceptOpacity(0.4f, 2.0f));
        Assert.Equal(0.0f, CharacterLayersService.AcceptOpacity(0.4f, -1.0f));
        Assert.Equal(0.7f, CharacterLayersService.AcceptOpacity(0.4f, 0.7f), precision: 5);
    }

    // ── Delege okumaları: null/throw fail-closed ──

    [Fact]
    public void Reads_NullOrThrowingDelegate_FailClosed()
    {
        Assert.Equal(string.Empty, CharacterLayersService.ReadSlotAsset(null));
        Assert.Equal(string.Empty, CharacterLayersService.ReadSlotAsset(() => throw new InvalidOperationException()));
        Assert.Equal("a.png", CharacterLayersService.ReadSlotAsset(() => "a.png"));
        Assert.Equal(string.Empty, CharacterLayersService.ReadSlotAsset(() => null));

        Assert.Equal(1.0f, CharacterLayersService.ReadSlotOpacity(null), precision: 5);
        Assert.Equal(0.2f, CharacterLayersService.ReadSlotOpacity(null, 0.2f), precision: 5);
        Assert.Equal(0.2f, CharacterLayersService.ReadSlotOpacity(() => throw new InvalidOperationException(), 0.2f), precision: 5);
        Assert.Equal(0.2f, CharacterLayersService.ReadSlotOpacity(() => float.NaN, 0.2f), precision: 5);
        Assert.Equal(1.0f, CharacterLayersService.ReadSlotOpacity(() => 4.0f), precision: 5);

        Assert.False(CharacterLayersService.ReadSlotVisible(null));
        Assert.False(CharacterLayersService.ReadSlotVisible(() => throw new InvalidOperationException()));
        Assert.True(CharacterLayersService.ReadSlotVisible(() => 1));
        Assert.True(CharacterLayersService.ReadSlotVisible(null, fallback: true));

        Assert.Empty(CharacterLayersService.ReadPresetNames(null));
        Assert.Empty(CharacterLayersService.ReadPresetNames(() => throw new InvalidOperationException()));
        Assert.Empty(CharacterLayersService.ReadPresetNames(() => "{ broken"));
        Assert.Equal(new[] { "happy", "sad" },
            CharacterLayersService.ReadPresetNames(() => "[\"happy\",\"sad\"]").ToArray());

        Assert.Equal(string.Empty, CharacterLayersService.ReadLastError(null));
        Assert.Equal(string.Empty, CharacterLayersService.ReadLastError(() => throw new InvalidOperationException()));
        Assert.Equal("boo", CharacterLayersService.ReadLastError(() => "boo"));
    }

    // ── Rozet ──

    [Fact]
    public void Describe_BrokenInput_StaysHiddenWithoutThrow()
    {
        Assert.False(CharacterLayersService.Describe(null).Visible);
        Assert.False(CharacterLayersService.Describe(string.Empty).Visible);
        var garbage = CharacterLayersService.Describe("{ not json");
        Assert.False(garbage.Visible);
        Assert.Equal(string.Empty, garbage.Text);
        Assert.False(CharacterLayersService.Describe("[]").Visible);
        Assert.False(CharacterLayersService.Describe("[{\"slot\":\"tail\",\"asset\":\"t.png\"}]").Visible);
        var exception = Record.Exception(() => CharacterLayersService.Describe("{ broken"));
        Assert.Null(exception);
    }

    [Fact]
    public void Describe_ValidDrawList_IsVisibleWithCount()
    {
        var description = CharacterLayersService.Describe(
            "[{\"slot\":\"body\",\"asset\":\"b.png\",\"opacity\":1},{\"slot\":\"face\",\"asset\":\"f.png\",\"opacity\":0.5}]");
        Assert.True(description.Visible);
        Assert.Contains("2/4", description.Text);
    }

    // ── Referans toplama ──

    [Fact]
    public void CollectAssetRefs_LayersAndExpressions_LegacySpriteExcluded()
    {
        var data = new Dictionary<string, object?>
        {
            ["sprite"] = "legacy.png",
            ["layers"] = new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["body"] = "b.png",
                ["face"] = string.Empty,
                ["outfit"] = "o.png",
                ["accessory"] = string.Empty,
            },
            ["expressions"] = new List<object>
            {
                new Dictionary<string, object?>
                {
                    ["name"] = "happy",
                    ["slots"] = new Dictionary<string, string>(StringComparer.Ordinal) { ["face"] = "happy.png" },
                },
            },
        };
        var refs = CharacterLayersService.CollectAssetRefs(data);
        Assert.DoesNotContain(refs, r => r.Asset == "legacy.png");
        Assert.Contains(refs, r => r.Location == "layer" && r.Slot == "body" && r.Asset == "b.png");
        Assert.Contains(refs, r => r.Location == "expression 'happy'" && r.Slot == "face" && r.Asset == "happy.png");
        Assert.DoesNotContain(refs, r => string.IsNullOrEmpty(r.Asset));
    }
}
