using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Tek mantiksal Unicode skalerinin zengin gorunumu. Nihai reveal birimi
/// Dilim 3 shaping cluster hattinda uretilir. Alan adlari native JSON semasiyla
/// birebir eslesir (<c>docs/RICH_TEXT_MARKUP_CONTRACT.md</c> bolum 5).
/// </summary>
public sealed record MarkupChar(
    [property: JsonPropertyName("text")] string Text,
    [property: JsonPropertyName("line_break")] bool LineBreak,
    [property: JsonPropertyName("bold")] bool Bold,
    [property: JsonPropertyName("italic")] bool Italic,
    [property: JsonPropertyName("underline")] bool Underline,
    [property: JsonPropertyName("color")] string? Color,
    [property: JsonPropertyName("size")] float? Size,
    [property: JsonPropertyName("speed")] float Speed,
    [property: JsonPropertyName("pause_before")] float PauseBefore,
    [property: JsonPropertyName("shake")] bool Shake,
    [property: JsonPropertyName("shake_intensity")] float? ShakeIntensity,
    [property: JsonPropertyName("wave")] bool Wave,
    [property: JsonPropertyName("wave_speed")] float? WaveSpeed,
    [property: JsonPropertyName("wave_amplitude")] float? WaveAmplitude);

/// <summary>Her zaman siddeti "warning" olan tani kaydi.</summary>
public sealed record MarkupDiagnostic(
    [property: JsonPropertyName("message")] string Message,
    [property: JsonPropertyName("offset")] uint Offset,
    [property: JsonPropertyName("length")] uint Length);

/// <summary>
/// Cozumlenmis markup belgesi: duz metin + karakter basina token akisi.
/// </summary>
public sealed record MarkupDocument(
    [property: JsonPropertyName("plain_text")] string PlainText,
    [property: JsonPropertyName("chars")] IReadOnlyList<MarkupChar> Chars,
    [property: JsonPropertyName("diagnostics")] IReadOnlyList<MarkupDiagnostic> Diagnostics,
    [property: JsonPropertyName("trailing_pause")] double TrailingPause,
    [property: JsonPropertyName("omitted_diagnostics")] int OmittedDiagnostics)
{
    /// <summary>Native <c>RowlEngine_ParseMarkup</c> semasinda JSON uretir.</summary>
    public string ToJson() =>
        JsonSerializer.Serialize(this, MarkupParser.JsonOptions);

    [JsonPropertyName("char_count")]
    public int CharCount => Chars.Count;
}

/// <summary>
/// Faz 3 Dilim 2 — managed zengin metin markup cozumleyici.
///
/// Native <c>Rowl::Text::markup_parser</c> ile ayni sozlesmeyi uygular:
/// ayni etiket kumesi, ayni deger araliklari, ayni fail-closed kurallari
/// (bozuk etiket literal + uyari, kapanmamis bilinen etiket sona kadar
/// gecerli + uyari, asla throw/crash yok). Davranis kaynagi
/// <c>docs/RICH_TEXT_MARKUP_CONTRACT.md</c>'dir; celiskide native kod
/// kazanir, bu dosya yamanir.
///
/// <c>EngineHost</c> ve <c>MainWindowViewModel</c> buyumez: bu tur saf ve
/// bagimsizdir; native kitapliga erisilemediginde managed cozumlemeye
/// sessizce duser (fail-closed, asla throw etmez).
/// </summary>
public static class MarkupParser
{
    /// <summary>Mirrors ROWL_ENGINE_CAPABILITY_RICH_TEXT_MARKUP (1 &lt;&lt; 8).</summary>
    public const ulong NativeCapabilityRichTextMarkup = 256UL;

    /// <summary>Tasiyici giris siniri (UTF-8 byte). Uzeri fail-closed reddedilir.</summary>
    public const int MaxMarkupBytes = 256 * 1024;

    /// <summary>Saklanan uyari ust siniri; asimi sayacla raporlanir.</summary>
    public const int MaxStoredDiagnostics = 128;

    internal static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = null,
        WriteIndented = false,
    };

    // Sozlesme tablosu (contract bolum 2.3): native kNamedColors ile birebir.
    private static readonly Dictionary<string, (byte R, byte G, byte B)> NamedColors =
        new(StringComparer.Ordinal)
        {
            ["black"] = (0x00, 0x00, 0x00), ["white"] = (0xFF, 0xFF, 0xFF),
            ["red"] = (0xFF, 0x00, 0x00), ["green"] = (0x00, 0xFF, 0x00),
            ["blue"] = (0x00, 0x00, 0xFF), ["yellow"] = (0xFF, 0xFF, 0x00),
            ["cyan"] = (0x00, 0xFF, 0xFF), ["aqua"] = (0x00, 0xFF, 0xFF),
            ["magenta"] = (0xFF, 0x00, 0xFF), ["fuchsia"] = (0xFF, 0x00, 0xFF),
            ["gray"] = (0x80, 0x80, 0x80), ["grey"] = (0x80, 0x80, 0x80),
            ["orange"] = (0xFF, 0xA5, 0x00), ["purple"] = (0x80, 0x00, 0x80),
            ["pink"] = (0xFF, 0xC0, 0xCB), ["brown"] = (0xA5, 0x2A, 0x2A),
            ["lime"] = (0x00, 0xFF, 0x00), ["navy"] = (0x00, 0x00, 0x80),
            ["teal"] = (0x00, 0x80, 0x80), ["olive"] = (0x80, 0x80, 0x00),
        };

    /// <summary>Girdiyi cozer; null/hatali girdi dahil asla throw etmez.</summary>
    public static MarkupDocument Parse(string? markup)
    {
        try
        {
            markup ??= string.Empty;
            if (Encoding.UTF8.GetByteCount(markup) > MaxMarkupBytes)
            {
                return new MarkupDocument(
                    string.Empty,
                    Array.Empty<MarkupChar>(),
                    new[]
                    {
                        new MarkupDiagnostic(
                            $"input exceeds {MaxMarkupBytes} bytes; content dropped",
                            0, 0),
                    },
                    0, 0);
            }
            return new ParserState(markup).Run();
        }
        catch (Exception)
        {
            return new MarkupDocument(
                string.Empty,
                Array.Empty<MarkupChar>(),
                new[] { new MarkupDiagnostic(
                    "internal parser failure; input kept as plain text", 0, 0) },
                0, 0);
        }
    }

    /// <summary>Yalnizca duz metni dondurur; asla throw etmez.</summary>
    public static string Strip(string? markup)
    {
        try
        {
            return Parse(markup).PlainText;
        }
        catch (Exception)
        {
            return markup ?? string.Empty;
        }
    }

    /// <summary>
    /// Native JSON semasindaki belgeyi managed turune cevirir (test ve
    /// parity karsilastirmasi icin). Bozuk JSON bos belgeye duser.
    /// </summary>
    public static MarkupDocument ParseJson(string json)
    {
        try
        {
            return JsonSerializer.Deserialize<MarkupDocument>(
                json, JsonOptions) ?? Empty();
        }
        catch (Exception)
        {
            return Empty();
        }
    }

    // ── Native kopru (handle gerektirmez, fail-closed) ────────────────────

    private delegate NativeBridge.ResultCode CallerStringQuery(
        IntPtr buffer, uint bufferSize, out uint requiredSize);

    private static string? ReadCallerString(CallerStringQuery query)
    {
        try
        {
            if (query(IntPtr.Zero, 0, out uint required) !=
                    NativeBridge.ResultCode.Ok || required == 0)
                return null;
            IntPtr buffer = Marshal.AllocHGlobal((int)required);
            try
            {
                if (query(buffer, required, out _) != NativeBridge.ResultCode.Ok)
                    return null;
                return Marshal.PtrToStringUTF8(buffer);
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        catch (Exception)
        {
            return null;
        }
    }

    /// <summary>
    /// Native parse dener; kitaplik yoksa/Reddederse false (managed'e dusulur).
    /// Asla throw etmez.
    /// </summary>
    public static bool TryParseNativeJson(string? markup, out string? json)
    {
        json = null;
        try
        {
            string? result = ReadCallerString(
                (IntPtr buffer, uint size, out uint required) =>
                    NativeBridge.RowlEngine_ParseMarkup(
                        markup ?? string.Empty, buffer, size, out required));
            if (result is null)
                return false;
            json = result;
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>
    /// Native strip dener; kitaplik yoksa/reddederse false. Asla throw etmez.
    /// </summary>
    public static bool TryStripNative(string? markup, out string? plainText)
    {
        plainText = null;
        try
        {
            string? result = ReadCallerString(
                (IntPtr buffer, uint size, out uint required) =>
                    NativeBridge.RowlEngine_StripMarkup(
                        markup ?? string.Empty, buffer, size, out required));
            if (result is null)
                return false;
            plainText = result;
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>
    /// Native parse'i dener, mumkun degilse managed JSON'a duser.
    /// Asla throw etmez; her yolda gecerli belge JSON'u dondurur.
    /// </summary>
    public static string ParseNativeOrManagedJson(string? markup)
    {
        if (TryParseNativeJson(markup, out string? json) && json is not null)
            return json;
        return Parse(markup).ToJson();
    }

    private static MarkupDocument Empty() => new(
        string.Empty,
        Array.Empty<MarkupChar>(),
        Array.Empty<MarkupDiagnostic>(),
        0, 0);

    // ── Cozumleyici ic durumu (native sinifa birebir port) ────────────────

    private sealed class ParserState
    {
        private readonly string _source;
        private readonly StringBuilder _plain = new();
        private readonly List<MarkupChar> _chars = new();
        private readonly List<MarkupDiagnostic> _diagnostics = new();
        private int _omitted;
        private double _trailing;
        private double _pendingPause;

        private int _bold;
        private int _italic;
        private int _underline;
        private readonly List<(byte R, byte G, byte B)> _colors = new();
        private readonly List<float> _sizes = new();
        private readonly List<float> _speeds = new();
        private readonly List<float> _shakes = new();
        private readonly List<(float Speed, float Amplitude)> _waves = new();
        private readonly List<(string Kind, int Offset, int Length)> _opens = new();

        internal ParserState(string source) => _source = source;

        internal MarkupDocument Run()
        {
            int i = 0;
            while (i < _source.Length)
            {
                if (_source[i] != '<')
                    ConsumeTextRun(ref i);
                else
                    ConsumeTag(ref i);
            }
            if (_pendingPause > 0)
            {
                _trailing += _pendingPause;
                _pendingPause = 0;
            }
            foreach (var open in _opens)
                Warn($"unclosed <{open.Kind}> applies to end of input; kept styling",
                    open.Offset, open.Length);
            _opens.Clear();
            return new MarkupDocument(
                _plain.ToString(), _chars, _diagnostics, _trailing, _omitted);
        }

        private void Warn(string message, int offset, int length)
        {
            if (_diagnostics.Count >= MaxStoredDiagnostics)
            {
                _omitted++;
                return;
            }

            int start = Math.Clamp(offset, 0, _source.Length);
            long requestedEnd = (long)start + Math.Max(0, length);
            int end = (int)Math.Clamp(requestedEnd, start, _source.Length);
            uint utf8Offset = (uint)Encoding.UTF8.GetByteCount(
                _source.AsSpan(0, start));
            uint utf8Length = (uint)Encoding.UTF8.GetByteCount(
                _source.AsSpan(start, end - start));
            _diagnostics.Add(new MarkupDiagnostic(
                message, utf8Offset, utf8Length));
        }

        private void EmitChar(string text, bool lineBreak)
        {
            var top = CurrentStyle();
            top = top with
            {
                Text = text,
                LineBreak = lineBreak,
                PauseBefore = (float)_pendingPause,
            };
            _pendingPause = 0;
            _plain.Append(text);
            _chars.Add(top);
        }

        private MarkupChar CurrentStyle()
        {
            string? color = _colors.Count > 0 ? ToHex(_colors[^1]) : null;
            float? size = _sizes.Count > 0 ? _sizes[^1] : null;
            float? shake = _shakes.Count > 0 ? _shakes[^1] : null;
            (float Speed, float Amplitude)? wave =
                _waves.Count > 0 ? _waves[^1] : null;
            return new MarkupChar(
                string.Empty, false,
                _bold > 0, _italic > 0, _underline > 0,
                color, size,
                _speeds.Count > 0 ? _speeds[^1] : 1.0f, 0.0f,
                shake.HasValue, shake,
                wave.HasValue, wave?.Speed, wave?.Amplitude);
        }

        private static string ToHex((byte R, byte G, byte B) color) =>
            $"#{color.R:X2}{color.G:X2}{color.B:X2}";

        private void ConsumeTextRun(ref int i)
        {
            while (i < _source.Length && _source[i] != '<')
            {
                char c = _source[i];
                if (c == '\\' && i + 1 < _source.Length &&
                    _source[i + 1] == '<')
                {
                    EmitChar("<", false);
                    i += 2;
                    continue;
                }
                if (c == '\r')
                {
                    if (i + 1 < _source.Length && _source[i + 1] == '\n')
                        i += 2;
                    else
                        i++;
                    EmitChar("\n", true);
                    continue;
                }
                if (c == '\n')
                {
                    i++;
                    EmitChar("\n", true);
                    continue;
                }
                if (char.IsHighSurrogate(c))
                {
                    if (i + 1 < _source.Length &&
                        char.IsLowSurrogate(_source[i + 1]))
                    {
                        EmitChar(new string(new[] { c, _source[i + 1] }), false);
                        i += 2;
                    }
                    else
                    {
                        Warn("invalid UTF-16 sequence replaced with U+FFFD", i, 1);
                        EmitChar("\uFFFD", false);
                        i++;
                    }
                    continue;
                }
                if (char.IsLowSurrogate(c))
                {
                    Warn("invalid UTF-16 sequence replaced with U+FFFD", i, 1);
                    EmitChar("\uFFFD", false);
                    i++;
                    continue;
                }
                EmitChar(c.ToString(), false);
                i++;
            }
        }

        private void ConsumeTag(ref int i)
        {
            int n = _source.Length;
            if (i + 1 < n)
            {
                char next = _source[i + 1];
                if (next != '/' && next != '>' && !IsAsciiAlpha(next))
                {
                    EmitChar("<", false);
                    i++;
                    return;
                }
            }
            else
            {
                EmitChar("<", false);
                i++;
                return;
            }

            int close = -1;
            for (int k = i + 1; k < n; k++)
            {
                if (_source[k] == '>')
                {
                    close = k;
                    break;
                }
                if (_source[k] == '<')
                    break;
            }
            if (close < 0)
            {
                Warn("unterminated tag; '<' kept as literal text", i, n - i);
                EmitChar("<", false);
                i++;
                return;
            }

            int tagOffset = i;
            int tagLength = close - i + 1;
            string inner = Trim(_source.Substring(i + 1, close - i - 1));
            i = close + 1;
            if (!ApplyTag(inner, tagOffset, tagLength))
                EmitLiteralText(
                    _source.Substring(tagOffset, tagLength), tagOffset);
        }

        private bool ApplyTag(string inner, int offset, int length)
        {
            if (inner.Length == 0)
            {
                Warn("malformed empty tag <>; kept as literal text", offset, length);
                return false;
            }
            if (inner[0] == '/')
                return ApplyClosingTag(Trim(inner.Substring(1)), offset, length);

            int nameEnd = 0;
            while (nameEnd < inner.Length && IsAsciiAlpha(inner[nameEnd]))
                nameEnd++;
            if (nameEnd == 0)
            {
                Warn("malformed tag; kept as literal text", offset, length);
                return false;
            }
            string name = inner.Substring(0, nameEnd).ToLowerInvariant();
            string tail = Trim(inner.Substring(nameEnd));

            bool selfClosed = tail.Length > 0 && tail[^1] == '/';
            if (selfClosed && (name == "br" || name == "pause"))
                tail = Trim(tail.Substring(0, tail.Length - 1));

            if (name == "b" || name == "i" || name == "u")
            {
                if (tail.Length != 0)
                {
                    Warn($"tag <{name}> takes no attributes; kept literal",
                        offset, length);
                    return false;
                }
                PushOpen(name, offset, length);
                if (name == "b") _bold++;
                else if (name == "i") _italic++;
                else _underline++;
                return true;
            }
            if (name == "br")
            {
                if (tail.Length != 0)
                {
                    Warn("tag <br> takes no attributes; kept literal",
                        offset, length);
                    return false;
                }
                EmitChar("\n", true);
                return true;
            }
            if (name == "color")
            {
                if (!ParseEqualsColor(tail, out var color, offset, length))
                    return false;
                _colors.Add(color);
                PushOpen(name, offset, length);
                return true;
            }
            if (name == "size")
            {
                if (!ParseEqualsFloat(tail, out float size, offset, length, "size"))
                    return false;
                if (!(size >= 1.0f && size <= 512.0f))
                {
                    Warn("tag <size> value out of range [1, 512]; kept literal",
                        offset, length);
                    return false;
                }
                _sizes.Add(size);
                PushOpen(name, offset, length);
                return true;
            }
            if (name == "speed")
            {
                if (!ParseEqualsFloat(tail, out float speed, offset, length, "speed"))
                    return false;
                if (!(speed > 0.0f && speed <= 100.0f))
                {
                    Warn("tag <speed> value out of range (0, 100]; kept literal",
                        offset, length);
                    return false;
                }
                _speeds.Add(speed);
                PushOpen(name, offset, length);
                return true;
            }
            if (name == "pause")
            {
                if (!ParseEqualsFloat(tail, out float seconds, offset, length, "pause"))
                    return false;
                if (!(seconds >= 0.0f && seconds <= 60.0f))
                {
                    Warn("tag <pause> value out of range [0, 60]; kept literal",
                        offset, length);
                    return false;
                }
                _pendingPause += seconds;
                return true;
            }
            if (name == "shake")
            {
                if (!ParseEffectParams(tail,
                        new[] { "intensity" },
                        out var values, offset, length, "shake"))
                    return false;
                if (!(values["intensity"] >= 0.0f && values["intensity"] <= 100.0f))
                {
                    Warn("tag <shake> intensity out of range [0, 100]; kept literal",
                        offset, length);
                    return false;
                }
                _shakes.Add(values["intensity"]);
                PushOpen(name, offset, length);
                return true;
            }
            if (name == "wave")
            {
                if (!ParseEffectParams(tail,
                        new[] { "speed", "amplitude" },
                        out var values, offset, length, "wave"))
                    return false;
                if (!(values["speed"] >= 0.0f && values["speed"] <= 100.0f) ||
                    !(values["amplitude"] >= 0.0f && values["amplitude"] <= 100.0f))
                {
                    Warn("tag <wave> params out of range [0, 100]; kept literal",
                        offset, length);
                    return false;
                }
                _waves.Add((values["speed"], values["amplitude"]));
                PushOpen(name, offset, length);
                return true;
            }
            Warn("unknown tag; kept as literal text", offset, length);
            return false;
        }

        private bool ApplyClosingTag(string body, int offset, int length)
        {
            int nameEnd = 0;
            while (nameEnd < body.Length && IsAsciiAlpha(body[nameEnd]))
                nameEnd++;
            string name = body.Substring(0, nameEnd).ToLowerInvariant();
            string rest = Trim(body.Substring(nameEnd));
            bool closable = name == "b" || name == "i" || name == "u" ||
                name == "color" || name == "size" || name == "speed" ||
                name == "shake" || name == "wave";
            if (nameEnd == 0 || !closable || rest.Length != 0)
            {
                Warn("malformed closing tag; kept as literal text", offset, length);
                return false;
            }
            if (!PopOpen(name))
            {
                Warn($"stray closing tag </{name}> with no open <{name}>; " +
                    "kept as literal text", offset, length);
                return false;
            }
            if (name == "b") _bold--;
            else if (name == "i") _italic--;
            else if (name == "u") _underline--;
            else if (name == "color") _colors.RemoveAt(_colors.Count - 1);
            else if (name == "size") _sizes.RemoveAt(_sizes.Count - 1);
            else if (name == "speed") _speeds.RemoveAt(_speeds.Count - 1);
            else if (name == "shake") _shakes.RemoveAt(_shakes.Count - 1);
            else if (name == "wave") _waves.RemoveAt(_waves.Count - 1);
            return true;
        }

        private bool ParseEqualsColor(string tail,
            out (byte R, byte G, byte B) color, int offset, int length)
        {
            color = default;
            if (tail.Length == 0 || tail[0] != '=')
            {
                Warn("tag <color> needs a value (<color=#RRGGBB|#RGB|named>); " +
                    "kept literal", offset, length);
                return false;
            }
            string value = StripQuotes(Trim(tail.Substring(1)));
            if (value.Length == 0)
            {
                Warn("tag <color> has an empty value; kept literal",
                    offset, length);
                return false;
            }
            if (value[0] == '#')
            {
                if (!TryParseHexColor(value, out color))
                {
                    Warn($"tag <color> has an invalid hex value '{value}'; " +
                        "kept literal", offset, length);
                    return false;
                }
                return true;
            }
            if (!TryParseNamedColor(value, out color))
            {
                Warn($"tag <color> has an unknown color name '{value}'; " +
                    "kept literal", offset, length);
                return false;
            }
            return true;
        }

        private bool ParseEqualsFloat(string tail, out float value,
            int offset, int length, string tagName)
        {
            value = 0;
            if (tail.Length == 0 || tail[0] != '=')
            {
                Warn($"tag <{tagName}> needs a value; kept literal",
                    offset, length);
                return false;
            }
            string text = StripQuotes(Trim(tail.Substring(1)));
            if (!ParseAsciiFloat(text, out value))
            {
                Warn($"tag <{tagName}> has an invalid number '{text}'; " +
                    "kept literal", offset, length);
                return false;
            }
            return true;
        }

        private bool ParseEffectParams(string tail, string[] expected,
            out Dictionary<string, float> values, int offset, int length,
            string tagName)
        {
            values = new Dictionary<string, float>(StringComparer.Ordinal);
            List<string>? tokens = SplitAttributeTokens(Trim(tail));
            if (tokens is null)
            {
                Warn($"tag <{tagName}> has unbalanced quotes; kept literal",
                    offset, length);
                return false;
            }
            if (tokens.Count != expected.Length)
            {
                Warn($"tag <{tagName}> needs exactly {expected.Length} " +
                    "parameter(s); kept literal", offset, length);
                return false;
            }
            foreach (string token in tokens)
            {
                if (!ParseAttribute(token, out string key, out string text) ||
                    Array.IndexOf(expected, key) < 0 ||
                    values.ContainsKey(key) ||
                    !ParseAsciiFloat(text, out float number))
                {
                    Warn($"tag <{tagName}> has a malformed parameter " +
                        $"'{token}'; kept literal", offset, length);
                    return false;
                }
                values[key] = number;
            }
            return true;
        }

        private void PushOpen(string kind, int offset, int length) =>
            _opens.Add((kind, offset, length));

        private bool PopOpen(string kind)
        {
            for (int k = _opens.Count - 1; k >= 0; k--)
            {
                if (_opens[k].Kind == kind)
                {
                    _opens.RemoveAt(k);
                    return true;
                }
            }
            return false;
        }

        private void EmitLiteralText(string raw, int baseOffset = 0)
        {
            for (int k = 0; k < raw.Length;)
            {
                char c = raw[k];
                if (c == '\r')
                {
                    if (k + 1 < raw.Length && raw[k + 1] == '\n')
                        k += 2;
                    else
                        k++;
                    EmitChar("\n", true);
                    continue;
                }
                if (c == '\n')
                {
                    k++;
                    EmitChar("\n", true);
                    continue;
                }
                if (char.IsHighSurrogate(c) && k + 1 < raw.Length &&
                    char.IsLowSurrogate(raw[k + 1]))
                {
                    EmitChar(new string(new[] { c, raw[k + 1] }), false);
                    k += 2;
                    continue;
                }
                if (char.IsSurrogate(c))
                {
                    Warn("invalid UTF-16 sequence replaced with U+FFFD",
                        baseOffset + k, 1);
                    EmitChar("\uFFFD", false);
                    k++;
                    continue;
                }
                EmitChar(c.ToString(), false);
                k++;
            }
        }

        // ── Saf yardimcilar ──

        private static bool IsAsciiAlpha(char c) =>
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');

        private static string Trim(string value) =>
            value.Trim(' ', '\t', '\n', '\r');

        private static string StripQuotes(string value)
        {
            value = Trim(value);
            if (value.Length >= 2 &&
                ((value[0] == '"' && value[^1] == '"') ||
                 (value[0] == '\'' && value[^1] == '\'')))
                return value.Substring(1, value.Length - 2);
            return value;
        }

        private static List<string>? SplitAttributeTokens(string tail)
        {
            var tokens = new List<string>();
            var current = new StringBuilder();
            char quote = '\0';
            bool inToken = false;
            foreach (char c in tail)
            {
                if (quote != '\0')
                {
                    current.Append(c);
                    if (c == quote)
                        quote = '\0';
                    continue;
                }
                if (c == '"' || c == '\'')
                {
                    quote = c;
                    current.Append(c);
                    inToken = true;
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    if (inToken)
                    {
                        tokens.Add(current.ToString());
                        current.Clear();
                        inToken = false;
                    }
                    continue;
                }
                current.Append(c);
                inToken = true;
            }
            if (quote != '\0')
                return null;
            if (inToken)
                tokens.Add(current.ToString());
            return tokens;
        }

        private static bool ParseAttribute(string token,
            out string key, out string value)
        {
            key = string.Empty;
            value = string.Empty;
            int eq = token.IndexOf('=');
            if (eq <= 0 || eq + 1 >= token.Length)
                return false;
            string rawKey = Trim(token.Substring(0, eq));
            string rawValue = StripQuotes(Trim(token.Substring(eq + 1)));
            if (rawKey.Length == 0 || rawValue.Length == 0)
                return false;
            foreach (char c in rawKey)
            {
                if (!IsAsciiAlpha(c) && !char.IsDigit(c) && c != '_' && c != '-')
                    return false;
            }
            key = rawKey.ToLowerInvariant();
            value = rawValue;
            return true;
        }

        private static bool ParseAsciiFloat(string text, out float value)
        {
            value = 0;
            text = Trim(text);
            if (text.Length == 0 || text.Length > 64)
                return false;
            if (!float.TryParse(text,
                    NumberStyles.AllowLeadingSign |
                    NumberStyles.AllowDecimalPoint |
                    NumberStyles.AllowExponent,
                    CultureInfo.InvariantCulture, out float parsed) ||
                float.IsNaN(parsed) || float.IsInfinity(parsed))
                return false;
            value = parsed;
            return true;
        }

        private static bool TryParseHexColor(string value,
            out (byte R, byte G, byte B) color)
        {
            color = default;
            value = Trim(value);
            if (value.Length < 2 || value[0] != '#')
                return false;
            string hex = value.Substring(1);
            if (hex.Length == 3)
            {
                if (!Nibble(hex[0], out byte r) || !Nibble(hex[1], out byte g) ||
                    !Nibble(hex[2], out byte b))
                    return false;
                color = ((byte)(r << 4 | r), (byte)(g << 4 | g), (byte)(b << 4 | b));
                return true;
            }
            if (hex.Length == 6)
            {
                if (!Byte(hex, 0, out byte r) || !Byte(hex, 2, out byte g) ||
                    !Byte(hex, 4, out byte b))
                    return false;
                color = (r, g, b);
                return true;
            }
            return false;

            static bool Nibble(char c, out byte b)
            {
                b = 0;
                int v;
                if (c >= '0' && c <= '9')
                    v = c - '0';
                else if (c >= 'a' && c <= 'f')
                    v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    v = c - 'A' + 10;
                else
                    return false;
                b = (byte)v;
                return true;
            }

            static bool Byte(string hex, int at, out byte b)
            {
                b = 0;
                if (!Nibble(hex[at], out byte hi) || !Nibble(hex[at + 1], out byte lo))
                    return false;
                b = (byte)(hi << 4 | lo);
                return true;
            }
        }

        private static bool TryParseNamedColor(string name,
            out (byte R, byte G, byte B) color)
        {
            color = default;
            string key = Trim(name).ToLowerInvariant();
            if (key.Length == 0 || key.Length > 32)
                return false;
            return NamedColors.TryGetValue(key, out color);
        }
    }
}
