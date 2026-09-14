using System;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 3 Dilim 2 — managed markup sozlesmesi (etiketler, hata toleransi,
/// token akisi) ve native kopru parity'si. Tum durumlar managed kodda
/// calisir; native kitaplik yoksa kopru managed'e duser (fail-closed).
/// </summary>
public sealed class EditorMarkupParserSlice2Tests
{
    private static void AssertConsistent(MarkupDocument document)
    {
        Assert.Equal(
            string.Concat(document.Chars.Select(c => c.Text)),
            document.PlainText);
        Assert.Equal(document.Chars.Count, document.CharCount);
    }

    // ── Bicimlendirme / renk / boyut ───────────────────────────────────

    [Fact]
    public void Parse_BoldRangeAndRestore()
    {
        MarkupDocument document = MarkupParser.Parse("<b>bold</b> plain");
        Assert.Equal("bold plain", document.PlainText);
        Assert.Equal(10, document.Chars.Count);
        Assert.True(document.Chars[0].Bold);
        Assert.True(document.Chars[3].Bold);
        Assert.False(document.Chars[5].Bold);
        Assert.Empty(document.Diagnostics);
        AssertConsistent(document);
    }

    [Fact]
    public void Parse_NestedStylesAndCaseInsensitiveNames()
    {
        MarkupDocument document = MarkupParser.Parse("<B><i><u>all</u></i></B>");
        Assert.Equal("all", document.PlainText);
        Assert.True(document.Chars[0].Bold);
        Assert.True(document.Chars[0].Italic);
        Assert.True(document.Chars[0].Underline);
        Assert.Empty(document.Diagnostics);
    }

    [Fact]
    public void Parse_ColorFormsResolveToSameRed()
    {
        MarkupDocument document = MarkupParser.Parse(
            "<color=#FF0000>a</color><color=#F00>b</color><color=Red>c</color>");
        Assert.Equal("abc", document.PlainText);
        Assert.Empty(document.Diagnostics);
        foreach (MarkupChar ch in document.Chars)
            Assert.Equal("#FF0000", ch.Color);
    }

    [Fact]
    public void Parse_NestedColorRestoresOuter()
    {
        MarkupDocument document = MarkupParser.Parse(
            "<color=blue>x<color=#00FF00>y</color>z</color>");
        Assert.Equal("xyz", document.PlainText);
        Assert.Equal("#0000FF", document.Chars[0].Color);
        Assert.Equal("#00FF00", document.Chars[1].Color);
        Assert.Equal("#0000FF", document.Chars[2].Color);
    }

    [Fact]
    public void Parse_SizeRange()
    {
        MarkupDocument document = MarkupParser.Parse("<size=24>big</size> ok");
        Assert.Equal("big ok", document.PlainText);
        Assert.Equal(24.0f, document.Chars[0].Size);
        Assert.Null(document.Chars[4].Size);
        Assert.Empty(document.Diagnostics);
    }

    // ── Yapi / zamanlama / efekt ───────────────────────────────────────

    [Fact]
    public void Parse_BreakVariantsAndNewlineNormalization()
    {
        MarkupDocument document = MarkupParser.Parse("a<br/>b<br>c<br />d");
        Assert.Equal("a\nb\nc\nd", document.PlainText);
        Assert.True(document.Chars[1].LineBreak);
        Assert.Empty(document.Diagnostics);

        MarkupDocument crlf = MarkupParser.Parse("one\r\ntwo\rthree");
        Assert.Equal("one\ntwo\nthree", crlf.PlainText);
        Assert.Empty(crlf.Diagnostics);
    }

    [Fact]
    public void Parse_SpeedAppliesAndRestores()
    {
        MarkupDocument document = MarkupParser.Parse(
            "<speed=2.0>fast</speed> normal");
        Assert.Equal("fast normal", document.PlainText);
        Assert.Equal(2.0f, document.Chars[0].Speed);
        Assert.Equal(1.0f, document.Chars[5].Speed);
        Assert.Empty(document.Diagnostics);
    }

    [Fact]
    public void Parse_PauseAttachesToNextCharAndTrailingAccumulates()
    {
        MarkupDocument document = MarkupParser.Parse("a<pause=1.5>b");
        Assert.Equal("ab", document.PlainText);
        Assert.Equal(0.0f, document.Chars[0].PauseBefore);
        Assert.Equal(1.5f, document.Chars[1].PauseBefore);
        Assert.Equal(0.0, document.TrailingPause);

        MarkupDocument trailing = MarkupParser.Parse("a<pause=2.0><pause=1.0>");
        Assert.Equal("a", trailing.PlainText);
        Assert.Equal(3.0, trailing.TrailingPause);
    }

    [Fact]
    public void Parse_ShakeAndWaveParams()
    {
        MarkupDocument document = MarkupParser.Parse(
            "<shake intensity=2.0>sh</shake> " +
            "<wave speed=3.0 amplitude=5.0>wv</wave>");
        Assert.Equal("sh wv", document.PlainText);
        Assert.True(document.Chars[0].Shake);
        Assert.Equal(2.0f, document.Chars[0].ShakeIntensity);
        Assert.False(document.Chars[2].Shake);
        Assert.True(document.Chars[3].Wave);
        Assert.Equal(3.0f, document.Chars[3].WaveSpeed);
        Assert.Equal(5.0f, document.Chars[4].WaveAmplitude);
        Assert.Empty(document.Diagnostics);
    }

    [Fact]
    public void Parse_EffectParamsAcceptAnyOrderAndQuotes()
    {
        MarkupDocument document = MarkupParser.Parse(
            "<wave amplitude=\"5.0\" speed='3.0'>x</wave>");
        Assert.Equal("x", document.PlainText);
        Assert.True(document.Chars[0].Wave);
        Assert.Equal(3.0f, document.Chars[0].WaveSpeed);
        Assert.Equal(5.0f, document.Chars[0].WaveAmplitude);
    }

    [Fact]
    public void Parse_MissingEffectParamsStayLiteral()
    {
        MarkupDocument missing = MarkupParser.Parse("<shake>x</shake>");
        Assert.Contains("<shake>", missing.PlainText, StringComparison.Ordinal);
        Assert.NotEmpty(missing.Diagnostics);
        Assert.False(missing.Chars[0].Shake);

        MarkupDocument wave = MarkupParser.Parse("<wave speed=3.0>x</wave>");
        Assert.Contains("<wave", wave.PlainText, StringComparison.Ordinal);
        Assert.NotEmpty(wave.Diagnostics);
    }

    // ── Hata toleransi ─────────────────────────────────────────────────

    [Fact]
    public void Parse_UnclosedKnownTagStylesToEndWithOneWarning()
    {
        MarkupDocument document = MarkupParser.Parse("<b>hello");
        Assert.Equal("hello", document.PlainText);
        Assert.True(document.Chars[0].Bold);
        Assert.Single(document.Diagnostics);
    }

    [Fact]
    public void Parse_BrokenTagsStayLiteralWithWarnings()
    {
        MarkupDocument document = MarkupParser.Parse(
            "Watch <dragon>out</color>!");
        Assert.Contains("<dragon>", document.PlainText, StringComparison.Ordinal);
        Assert.Contains("</color>", document.PlainText, StringComparison.Ordinal);
        Assert.Equal(2, document.Diagnostics.Count);
        AssertConsistent(document);
    }

    [Fact]
    public void Parse_InvalidValuesStayLiteral()
    {
        MarkupDocument document = MarkupParser.Parse(
            "<color=#GGG>x</color> <size=0>y</size> <speed=0>z</speed>");
        Assert.Contains("<color=#GGG>", document.PlainText, StringComparison.Ordinal);
        Assert.Contains("<size=0>", document.PlainText, StringComparison.Ordinal);
        Assert.Contains("<speed=0>", document.PlainText, StringComparison.Ordinal);
        // 3 bozuk acilis + 3 stray kapatma = 6 uyari.
        Assert.Equal(6, document.Diagnostics.Count);
        AssertConsistent(document);
    }

    [Fact]
    public void Parse_ComparisonOperatorStaysSilent()
    {
        MarkupDocument math = MarkupParser.Parse("a < b and 3 < 5");
        Assert.Equal("a < b and 3 < 5", math.PlainText);
        Assert.Empty(math.Diagnostics);

        MarkupDocument unterminated = MarkupParser.Parse("a <b hello");
        Assert.Equal("a <b hello", unterminated.PlainText);
        Assert.Single(unterminated.Diagnostics);
    }

    [Fact]
    public void Parse_EscapedLessThanStaysLiteral()
    {
        MarkupDocument document = MarkupParser.Parse(@"show \<b> literally");
        Assert.Equal("show <b> literally", document.PlainText);
        Assert.Empty(document.Diagnostics);
        Assert.All(document.Chars, ch => Assert.False(ch.Bold));
    }

    [Fact]
    public void Parse_NullAndEmptyAreFailClosed()
    {
        MarkupDocument nil = MarkupParser.Parse(null);
        Assert.Equal(string.Empty, nil.PlainText);
        Assert.Empty(nil.Chars);
        Assert.Empty(nil.Diagnostics);

        Assert.Equal(string.Empty, MarkupParser.Strip(null));
        Assert.Equal("plain", MarkupParser.Strip("plain"));
        Assert.Equal("a\nb", MarkupParser.Strip("a<br/>b"));
    }

    [Fact]
    public void Parse_OversizedInputIsFailClosed()
    {
        string huge = new('x', MarkupParser.MaxMarkupBytes + 1);
        MarkupDocument document = MarkupParser.Parse(huge);
        Assert.Equal(string.Empty, document.PlainText);
        Assert.Single(document.Diagnostics);

        string atLimit = new('y', MarkupParser.MaxMarkupBytes);
        MarkupDocument ok = MarkupParser.Parse(atLimit);
        Assert.Equal(atLimit, ok.PlainText);
        Assert.Empty(ok.Diagnostics);
    }

    [Fact]
    public void Parse_UnicodeCountsCodePoints()
    {
        MarkupDocument document = MarkupParser.Parse("Röle <b>ışık</b> 日本語");
        Assert.Equal("Röle ışık 日本語", document.PlainText);
        Assert.Empty(document.Diagnostics);
        AssertConsistent(document);
        // "Röle " (5) + "ışık" (4) + " " + 3 CJK (日本語) = 13.
        Assert.Equal(13, document.Chars.Count);
        Assert.True(document.Chars[5].Bold);
        Assert.True(document.Chars[8].Bold);
        Assert.False(document.Chars[9].Bold);
    }

    // ── JSON semasi ────────────────────────────────────────────────────

    [Fact]
    public void ToJson_CarriesContractKeys()
    {
        MarkupDocument document = MarkupParser.Parse("Hi <b>bold</b>!");
        using JsonDocument json = JsonDocument.Parse(document.ToJson());
        JsonElement root = json.RootElement;
        Assert.Equal("Hi bold!", root.GetProperty("plain_text").GetString());
        Assert.Equal(8, root.GetProperty("char_count").GetInt32());
        Assert.Equal(8, root.GetProperty("chars").GetArrayLength());
        Assert.Equal(0, root.GetProperty("diagnostics").GetArrayLength());
        Assert.Equal(0.0, root.GetProperty("trailing_pause").GetDouble());
        Assert.Equal(0, root.GetProperty("omitted_diagnostics").GetInt32());

        JsonElement bold = root.GetProperty("chars")[3];
        Assert.Equal("b", bold.GetProperty("text").GetString());
        Assert.True(bold.GetProperty("bold").GetBoolean());
        Assert.False(bold.GetProperty("italic").GetBoolean());
        Assert.Equal(JsonValueKind.Null, bold.GetProperty("color").ValueKind);
        Assert.Equal(1.0f, bold.GetProperty("speed").GetSingle());
    }

    [Fact]
    public void NativeCapability_MatchesOneShiftEight()
    {
        Assert.Equal(256UL, MarkupParser.NativeCapabilityRichTextMarkup);
    }

    // ── Native kopru (fail-closed parity) ──────────────────────────────

    [Fact]
    public void NativeOrManaged_ParsesIdentically()
    {
        const string markup =
            "Röle <b><color=#FF0000>cızırtıyla</color></b> " +
            "<i>uyanıyor</i><pause=0.5> " +
            "<wave speed=3.0 amplitude=5.0>~</wave><br/>Son.";
        MarkupDocument managed = MarkupParser.Parse(markup);

        Assert.True(MarkupParser.TryParseNativeJson(markup, out string? json),
            "Native markup library must be loaded for the parity gate.");
        Assert.NotNull(json);
        MarkupDocument document = MarkupParser.ParseJson(json);
        Assert.Equal(managed.PlainText, document.PlainText);
        Assert.Equal(managed.Chars.Count, document.Chars.Count);
        Assert.Equal(managed.Diagnostics.Count, document.Diagnostics.Count);
        Assert.Equal(managed.TrailingPause, document.TrailingPause);
        for (int i = 0; i < managed.Chars.Count; i++)
        {
            Assert.Equal(managed.Chars[i].Text, document.Chars[i].Text);
            Assert.Equal(managed.Chars[i].Bold, document.Chars[i].Bold);
            Assert.Equal(managed.Chars[i].Color, document.Chars[i].Color);
            Assert.Equal(managed.Chars[i].Speed, document.Chars[i].Speed);
            Assert.Equal(
                managed.Chars[i].PauseBefore, document.Chars[i].PauseBefore);
        }
    }

    [Fact]
    public void NativeAndManaged_DiagnosticSpansUseUtf8Bytes()
    {
        const string markup = "éş<oops>";
        MarkupDocument managed = MarkupParser.Parse(markup);
        Assert.Single(managed.Diagnostics);
        Assert.Equal(4u, managed.Diagnostics[0].Offset);
        Assert.Equal(6u, managed.Diagnostics[0].Length);

        Assert.True(MarkupParser.TryParseNativeJson(markup, out string? json),
            "Native markup library must be loaded for diagnostic parity.");
        Assert.NotNull(json);
        MarkupDocument native = MarkupParser.ParseJson(json);
        Assert.Single(native.Diagnostics);
        Assert.Equal(managed.Diagnostics[0], native.Diagnostics[0]);
    }

    [Fact]
    public void NativeOrManaged_StripFallsBackToManaged()
    {
        const string markup = "A <b>bold <color=red>red</color></b>!";
        if (MarkupParser.TryStripNative(markup, out string? native))
            Assert.Equal("A bold red!", native);
        else
            Assert.Equal("A bold red!", MarkupParser.Strip(markup));

        // Null giris native'te reddedilir; managed bos dondurur.
        if (!MarkupParser.TryStripNative(null, out _))
            Assert.Equal(string.Empty, MarkupParser.Strip(null));
    }

    [Fact]
    public void NativeOrManaged_NeverThrowsOnHostileInput()
    {
        string[] nasty =
        {
            "<", "<b", "<color=#>", "<size=abc>", "<wave>",
            "<wave speed=1 speed=2>", "<unknown foo='bar", "a<b",
            "<shake intensity=9999>x</shake>", "<pause=61>y</pause>",
        };
        foreach (string input in nasty)
        {
            string json = MarkupParser.ParseNativeOrManagedJson(input);
            MarkupDocument document = MarkupParser.ParseJson(json);
            AssertConsistent(document);
        }
    }
}
