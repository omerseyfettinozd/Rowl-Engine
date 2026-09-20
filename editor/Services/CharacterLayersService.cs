using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Faz 5 Dilim 3 — katmanlı karakter rozet açıklaması.</summary>
public sealed record CharacterLayersDescription(bool Visible, string Text)
{
    public static readonly CharacterLayersDescription Hidden = new(false, string.Empty);
}

/// <summary>Katman kütüphanesinden toplanan tekil asset referansı.</summary>
/// <param name="Location">"layer" ya da "expression '&lt;name&gt;'" gibi okunabilir konum.</param>
/// <param name="Slot">Slot anahtarı (body/face/outfit/accessory).</param>
/// <param name="Asset">Ham asset yolu.</param>
public sealed record CharacterLayerAssetRef(string Location, string Slot, string Asset);

/// <summary>
/// Faz 5 Dilim 3 — katmanlı karakter karar yüzeyi (saf + polling-free,
/// native çağrı YOKTUR). Motorun <c>CharacterLayers</c> /
/// <c>CharacterPresetLibrary</c> aynası: sabit slot sırası, sözdizimi
/// doğrulama, preset parse ve atomik expression kararı.
///
/// <c>EngineHost</c>'a dokunmaz: canlı okuma, çağrıcının verdiği delege
/// üzerinden yapılır (null delege = ölü handle, fail-closed). Kendi
/// timer/thread'i yoktur. Bozuk girdi fail-closed kapanır (throw yok).
/// Kaynak: <c>docs/CHARACTER_LAYERS_CONTRACT.md</c>; çelişirse native
/// kod kazanır.
/// </summary>
public static class CharacterLayersService
{
    /// <summary>Sabit çizim sırası: body (0) &lt; face (1) &lt; outfit (2) &lt; accessory (3).</summary>
    public static readonly string[] SlotOrder = { "body", "face", "outfit", "accessory" };

    /// <summary>Asset yolu sözdizimi tavanı (native <c>kMaxCharacterAssetPathBytes</c> aynası).</summary>
    public const int MaxAssetPathBytes = 4096;

    /// <summary>Preset adı tavanı (native <c>kMaxCharacterPresetNameBytes</c> aynası).</summary>
    public const int MaxPresetNameBytes = 256;

    // Slot parse / validate

    /// <summary>Bilinen slot mu? Karşılaştırma Ordinal'dir ("Body" geçersiz).</summary>
    public static bool IsKnownSlot(string? slot) => TryGetSlotIndex(slot, out _);

    /// <summary>Slot adını çizim sırası indeksine çevirir (body=0..accessory=3).</summary>
    public static bool TryGetSlotIndex(string? slot, out int index)
    {
        index = -1;
        if (string.IsNullOrEmpty(slot))
            return false;
        for (int i = 0; i < SlotOrder.Length; i++)
        {
            if (string.Equals(slot, SlotOrder[i], StringComparison.Ordinal))
            {
                index = i;
                return true;
            }
        }
        return false;
    }

    /// <summary>UTF-8 bayt uzunluğu (clamp + sözdizimi aynası).</summary>
    public static int Utf8ByteCount(string? value) =>
        string.IsNullOrEmpty(value) ? 0 : Encoding.UTF8.GetByteCount(value);

    /// <summary>
    /// Asset yolu sözdizimi: boş (slotu temizler) geçerlidir; dolu yolda
    /// NUL baytı yasaktır ve <see cref="MaxAssetPathBytes"/> aşılamaz.
    /// </summary>
    public static bool IsAssetPathSyntaxValid(string? asset)
    {
        if (string.IsNullOrEmpty(asset))
            return true;
        return asset.IndexOf('\0') < 0 && Utf8ByteCount(asset) <= MaxAssetPathBytes;
    }

    /// <summary>
    /// Preset adı geçerliliği: boş değil ve <see cref="MaxPresetNameBytes"/> tavanında.
    /// </summary>
    public static bool IsPresetNameValid(string? name) =>
        !string.IsNullOrEmpty(name) && Utf8ByteCount(name) <= MaxPresetNameBytes;

    /// <summary>
    /// Asset yolunu <see cref="MaxAssetPathBytes"/> tavanına kırpar
    /// (UTF-8 sekansı bölünmez; geçersiz kırpma geriye doğru aranır).
    /// Null/boş girdi boş döner.
    /// </summary>
    public static string ClampAssetPath(string? asset)
    {
        if (string.IsNullOrEmpty(asset))
            return string.Empty;
        byte[] bytes = Encoding.UTF8.GetBytes(asset);
        if (bytes.Length <= MaxAssetPathBytes)
            return asset;
        // Katı kod çözücüyle geriye doğru geçerli sınırı bul (en fazla
        // 3 adım: UTF-8 sekansı en çok 4 bayttır, ön ek zaten geçerlidir).
        Encoding strict = Encoding.GetEncoding(
            "utf-8", EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback);
        int cut = MaxAssetPathBytes;
        while (cut > 0)
        {
            try
            {
                return strict.GetString(bytes, 0, cut);
            }
            catch (DecoderFallbackException)
            {
                cut--;
            }
        }
        return string.Empty;
    }

    /// <summary>
    /// Opaklık adayı: non-finite ignore (son geçerli korunur), finite [0,1] clamp.
    /// </summary>
    public static float AcceptOpacity(float current, float candidate)
    {
        if (!float.IsFinite(candidate))
            return current;
        return Math.Clamp(candidate, 0.0f, 1.0f);
    }

    // Preset / layers parse

    /// <summary>
    /// Expression preset JSON'unu slot→asset haritasına çevirir. Kök obje
    /// olmalı; anahtarlar bilinen slot, değerler string ve her asset
    /// <see cref="IsAssetPathSyntaxValid"/> olmalıdır. Bozuk girdi
    /// fail-closed boş döner (throw yok).
    /// </summary>
    public static IReadOnlyDictionary<string, string> ParsePreset(string? expressionJson) =>
        TryParsePreset(expressionJson, out Dictionary<string, string> slots)
            ? slots
            : new Dictionary<string, string>(StringComparer.Ordinal);

    /// <summary><see cref="ParsePreset"/> ile aynı katı eşleme; bozuk girdi false + boş döner.</summary>
    public static bool TryParsePreset(string? expressionJson, out Dictionary<string, string> slots)
    {
        slots = new Dictionary<string, string>(StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(expressionJson))
            return false;
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(expressionJson);
        }
        catch (Exception)
        {
            return false;
        }
        using (document)
        {
            if (document.RootElement.ValueKind != JsonValueKind.Object)
                return false;
            foreach (JsonProperty property in document.RootElement.EnumerateObject())
            {
                if (!IsKnownSlot(property.Name) ||
                    property.Value.ValueKind != JsonValueKind.String ||
                    !IsAssetPathSyntaxValid(property.Value.GetString()))
                {
                    slots = new Dictionary<string, string>(StringComparer.Ordinal);
                    return false;
                }
                slots[property.Name] = property.Value.GetString() ?? string.Empty;
            }
            return true;
        }
    }

    /// <summary>
    /// Component <c>layers</c> anahtarını slot→asset haritasına çevirir.
    /// Girdi ham JSON string (hidrasyon), sözlük ya da <see cref="JsonElement"/>
    /// olabilir. Slot başına string kısaltma (<c>"face": "f.png"</c>) ya da
    /// obje (<c>{asset, opacity, visible}</c>) kabul edilir; bilinmeyen
    /// anahtarlar yoksayılır. Tip-bozuk alan tüm parse'ı atomik reddeder
    /// (false + boş, throw yok).
    /// </summary>
    public static bool TryParseLayers(object? layersValue, out Dictionary<string, string> slots)
    {
        slots = new Dictionary<string, string>(StringComparer.Ordinal);
        if (layersValue is null)
            return false;
        JsonElement root;
        JsonDocument? owned = null;
        try
        {
            if (layersValue is string raw)
            {
                if (string.IsNullOrWhiteSpace(raw))
                    return false;
                owned = JsonDocument.Parse(raw);
                root = owned.RootElement;
            }
            else if (layersValue is JsonElement element)
            {
                root = element;
            }
            else if (layersValue is System.Collections.IDictionary dictionary)
            {
                return TryParseLayersDictionary(dictionary, slots);
            }
            else
            {
                return false;
            }

            if (root.ValueKind != JsonValueKind.Object)
                return false;
            foreach (JsonProperty property in root.EnumerateObject())
            {
                if (!IsKnownSlot(property.Name))
                    continue;
                string? asset = ReadSlotAssetValue(property.Value);
                if (asset is null)
                {
                    slots = new Dictionary<string, string>(StringComparer.Ordinal);
                    return false;
                }
                slots[property.Name] = asset;
            }
            return true;
        }
        catch (Exception)
        {
            slots = new Dictionary<string, string>(StringComparer.Ordinal);
            return false;
        }
        finally
        {
            owned?.Dispose();
        }
    }

    /// <summary>
    /// Component <c>expressions</c> anahtarını isim→slot-harita kütüphanesine
    /// çevirir. Girdi ham JSON string (hidrasyon: <c>[{name, slots}]</c>) ya
    /// da liste olabilir. Bozuk TEKİL girdi atlanır (diğer presetler
    /// korunur); tamamı bozuk/null ise boş kütüphane döner (throw yok).
    /// </summary>
    public static IReadOnlyDictionary<string, IReadOnlyDictionary<string, string>> ParseExpressionLibrary(object? expressionsValue)
    {
        var library = new Dictionary<string, IReadOnlyDictionary<string, string>>(StringComparer.Ordinal);
        if (expressionsValue is null)
            return library;
        try
        {
            if (expressionsValue is string raw)
            {
                if (string.IsNullOrWhiteSpace(raw))
                    return library;
                using JsonDocument document = JsonDocument.Parse(raw);
                if (document.RootElement.ValueKind != JsonValueKind.Array)
                    return library;
                foreach (JsonElement item in document.RootElement.EnumerateArray())
                    AddLibraryEntry(library, ReadName(item, "name"), ReadSlotsElement(item));
                return library;
            }
            if (expressionsValue is System.Collections.IEnumerable enumerable)
            {
                foreach (object? item in enumerable)
                    AddLibraryEntry(library, ReadEntryName(item), ReadEntrySlots(item));
                return library;
            }
            return library;
        }
        catch (Exception)
        {
            return new Dictionary<string, IReadOnlyDictionary<string, string>>(StringComparer.Ordinal);
        }
    }

    // Expression atomiklik kararı

    /// <summary>
    /// Atomik expression kararı (native <c>applyExpression</c> aynası):
    /// mevcut slotların TÜMÜ önce doğrulanır (sözdizimi + opsiyonel
    /// resolver; null resolver = yalnız-sözdizimi). Biri bile bozuksa
    /// HİÇBİRİ uygulanmaz: <c>next</c> current'ın birebir kopyasıdır,
    /// <c>error</c> tanı verir, dönüş false'tur. Temiz girdide dönüş
    /// true'tur; preset'te geçmeyen slotlar aynen korunur.
    /// </summary>
    public static bool TryBuildExpressionResult(
        IReadOnlyDictionary<string, string> current,
        IReadOnlyDictionary<string, string> preset,
        out Dictionary<string, string> next,
        out string error,
        Func<string, bool>? isResolvable = null)
    {
        next = CopySlots(current);
        error = string.Empty;
        if (preset is null || preset.Count == 0)
        {
            error = "Unknown or empty expression preset; layers unchanged.";
            return false;
        }
        foreach ((string slot, string asset) in preset)
        {
            if (!IsKnownSlot(slot))
            {
                error = $"Unknown layer slot '{slot}'; layers unchanged.";
                next = CopySlots(current);
                return false;
            }
            if (!IsAssetPathSyntaxValid(asset))
            {
                error = $"Invalid asset for slot '{slot}'; layers unchanged.";
                next = CopySlots(current);
                return false;
            }
            if (isResolvable is not null && !string.IsNullOrEmpty(asset))
            {
                bool ok;
                try
                {
                    ok = isResolvable(asset);
                }
                catch (Exception)
                {
                    ok = false;
                }
                if (!ok)
                {
                    error = $"Unresolvable asset for slot '{slot}'; layers unchanged.";
                    next = CopySlots(current);
                    return false;
                }
            }
        }
        foreach ((string slot, string asset) in preset)
            next[slot] = asset;
        return true;
    }

    // Delege okumaları (null/throw fail-closed)

    /// <summary>Slot asset okuma; null delege/throw fail-closed "" döner.</summary>
    public static string ReadSlotAsset(Func<string?>? read)
    {
        if (read is null)
            return string.Empty;
        try
        {
            return read() ?? string.Empty;
        }
        catch (Exception)
        {
            return string.Empty;
        }
    }

    /// <summary>Opaklık okuma; null/throw/non-finite fail-closed fallback döner ([0,1] normalize).</summary>
    public static float ReadSlotOpacity(Func<float>? read, float fallback = 1.0f)
    {
        if (read is null)
            return fallback;
        try
        {
            float value = read();
            return float.IsFinite(value) ? Math.Clamp(value, 0.0f, 1.0f) : fallback;
        }
        catch (Exception)
        {
            return fallback;
        }
    }

    /// <summary>Görünürlük okuma; null delege/throw fail-closed fallback döner.</summary>
    public static bool ReadSlotVisible(Func<int>? read, bool fallback = false)
    {
        if (read is null)
            return fallback;
        try
        {
            return read() != 0;
        }
        catch (Exception)
        {
            return fallback;
        }
    }

    /// <summary>Preset listesi okuma (JSON string dizisi); bozuk/null/throw fail-closed boş döner.</summary>
    public static IReadOnlyList<string> ReadPresetNames(Func<string?>? readJson)
    {
        if (readJson is null)
            return Array.Empty<string>();
        try
        {
            string? json = readJson();
            if (string.IsNullOrWhiteSpace(json))
                return Array.Empty<string>();
            using JsonDocument document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Array)
                return Array.Empty<string>();
            var names = new List<string>();
            foreach (JsonElement item in document.RootElement.EnumerateArray())
            {
                if (item.ValueKind == JsonValueKind.String &&
                    IsPresetNameValid(item.GetString()))
                    names.Add(item.GetString()!);
            }
            return names;
        }
        catch (Exception)
        {
            return Array.Empty<string>();
        }
    }

    /// <summary>Son karakter tanısı okuma; null/throw fail-closed "" döner.</summary>
    public static string ReadLastError(Func<string?>? read)
    {
        if (read is null)
            return string.Empty;
        try
        {
            return read() ?? string.Empty;
        }
        catch (Exception)
        {
            return string.Empty;
        }
    }

    // Rozet

    /// <summary>
    /// Draw-list JSON'unu (<c>[{slot, asset, opacity}]</c>) rozete çevirir.
    /// Geçerli girdi yoksa/bozuksa gizli döner (throw yok).
    /// </summary>
    public static CharacterLayersDescription Describe(string? drawListJson)
    {
        if (string.IsNullOrWhiteSpace(drawListJson))
            return CharacterLayersDescription.Hidden;
        try
        {
            using JsonDocument document = JsonDocument.Parse(drawListJson);
            if (document.RootElement.ValueKind != JsonValueKind.Array)
                return CharacterLayersDescription.Hidden;
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (JsonElement item in document.RootElement.EnumerateArray())
            {
                if (item.ValueKind != JsonValueKind.Object)
                    return CharacterLayersDescription.Hidden;
                if (!item.TryGetProperty("slot", out JsonElement slotElement) ||
                    slotElement.ValueKind != JsonValueKind.String ||
                    !IsKnownSlot(slotElement.GetString()))
                    return CharacterLayersDescription.Hidden;
                if (!item.TryGetProperty("asset", out JsonElement assetElement) ||
                    assetElement.ValueKind != JsonValueKind.String)
                    return CharacterLayersDescription.Hidden;
                string asset = assetElement.GetString() ?? string.Empty;
                if (!IsAssetPathSyntaxValid(asset))
                    return CharacterLayersDescription.Hidden;
                if (!string.IsNullOrEmpty(asset))
                    seen.Add(slotElement.GetString()!);
            }
            if (seen.Count == 0)
                return CharacterLayersDescription.Hidden;
            return new CharacterLayersDescription(true, $"LAYERS {seen.Count}/4");
        }
        catch (Exception)
        {
            return CharacterLayersDescription.Hidden;
        }
    }

    // Referans toplama (linter Validate + unused + paket)

    /// <summary>
    /// Component datasındaki katman referanslarını toplar: <c>layers</c>
    /// slotları + <c>expressions</c> preset slotları (boş assetler atlanır,
    /// tekrarlar teklenir). Legacy <c>sprite</c> DAHİL DEĞİLDİR (o
    /// <c>AssetKeys</c> yolundan akar; çift rapor engellenir).
    /// </summary>
    public static IReadOnlyList<CharacterLayerAssetRef> CollectAssetRefs<TValue>(IReadOnlyDictionary<string, TValue> data)
    {
        var refs = new List<CharacterLayerAssetRef>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        if (data.TryGetValue("layers", out TValue? layersValue) &&
            TryParseLayers(ToObject(layersValue), out Dictionary<string, string> slots))
        {
            foreach ((string slot, string asset) in slots.OrderBy(pair => SlotDrawIndex(pair.Key)))
            {
                if (string.IsNullOrEmpty(asset) || !seen.Add("layer|" + slot + "|" + asset))
                    continue;
                refs.Add(new CharacterLayerAssetRef("layer", slot, asset));
            }
        }
        if (data.TryGetValue("expressions", out TValue? expressionsValue))
        {
            foreach ((string name, IReadOnlyDictionary<string, string> preset) in
                     ParseExpressionLibrary(ToObject(expressionsValue)).OrderBy(pair => pair.Key, StringComparer.Ordinal))
            {
                foreach ((string slot, string asset) in preset.OrderBy(pair => SlotDrawIndex(pair.Key)))
                {
                    if (string.IsNullOrEmpty(asset) || !seen.Add("expr|" + name + "|" + slot + "|" + asset))
                        continue;
                    refs.Add(new CharacterLayerAssetRef($"expression '{name}'", slot, asset));
                }
            }
        }
        return refs;
    }

    // Özel yardımcılar

    private static object? ToObject<TValue>(TValue? value) =>
        value is null ? null : (object)value;

    private static int SlotDrawIndex(string slot) =>
        TryGetSlotIndex(slot, out int index) ? index : int.MaxValue;

    private static Dictionary<string, string> CopySlots(IReadOnlyDictionary<string, string> current)
    {
        var copy = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string slot in SlotOrder)
            copy[slot] = current.TryGetValue(slot, out string? asset) ? asset ?? string.Empty : string.Empty;
        return copy;
    }

    private static string? ReadSlotAssetValue(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.String)
        {
            string asset = value.GetString() ?? string.Empty;
            return IsAssetPathSyntaxValid(asset) ? asset : null;
        }
        if (value.ValueKind == JsonValueKind.Object &&
            value.TryGetProperty("asset", out JsonElement assetElement) &&
            assetElement.ValueKind == JsonValueKind.String)
        {
            string asset = assetElement.GetString() ?? string.Empty;
            return IsAssetPathSyntaxValid(asset) ? asset : null;
        }
        return null;
    }

    private static bool TryParseLayersDictionary(
        System.Collections.IDictionary dictionary, Dictionary<string, string> slots)
    {
        foreach (System.Collections.DictionaryEntry entry in dictionary)
        {
            if (entry.Key is not string key || !IsKnownSlot(key))
                continue;
            string? asset = ReadSlotAssetBoxed(entry.Value);
            if (asset is null)
            {
                slots.Clear();
                return false;
            }
            slots[key] = asset;
        }
        return true;
    }

    private static string? ReadSlotAssetBoxed(object? value)
    {
        switch (value)
        {
            case null:
                return null;
            case string text:
                return IsAssetPathSyntaxValid(text) ? text : null;
            case JsonElement element:
                return ReadSlotAssetValue(element);
            case System.Collections.IDictionary dictionary:
                foreach (System.Collections.DictionaryEntry entry in dictionary)
                {
                    if (entry.Key is string key &&
                        string.Equals(key, "asset", StringComparison.Ordinal) &&
                        entry.Value is string asset &&
                        IsAssetPathSyntaxValid(asset))
                        return asset;
                }
                return null;
            default:
                return null;
        }
    }

    private static void AddLibraryEntry(
        Dictionary<string, IReadOnlyDictionary<string, string>> library,
        string? name,
        IReadOnlyDictionary<string, string>? preset)
    {
        if (!IsPresetNameValid(name) || preset is null || preset.Count == 0)
            return;
        library[name!] = preset;
    }

    private static string? ReadName(JsonElement item, string property) =>
        item.ValueKind == JsonValueKind.Object &&
        item.TryGetProperty(property, out JsonElement nameElement) &&
        nameElement.ValueKind == JsonValueKind.String
            ? nameElement.GetString()
            : null;

    private static IReadOnlyDictionary<string, string>? ReadSlotsElement(JsonElement item)
    {
        if (item.ValueKind != JsonValueKind.Object ||
            !item.TryGetProperty("slots", out JsonElement slotsElement))
            return null;
        if (slotsElement.ValueKind != JsonValueKind.Object)
            return null;
        var preset = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (JsonProperty property in slotsElement.EnumerateObject())
        {
            if (!IsKnownSlot(property.Name) ||
                property.Value.ValueKind != JsonValueKind.String ||
                !IsAssetPathSyntaxValid(property.Value.GetString()))
                return null;
            preset[property.Name] = property.Value.GetString() ?? string.Empty;
        }
        return preset.Count == 0 ? null : preset;
    }

    private static string? ReadEntryName(object? item)
    {
        switch (item)
        {
            case JsonElement element:
                return ReadName(element, "name");
            case System.Collections.IDictionary dictionary:
                foreach (System.Collections.DictionaryEntry entry in dictionary)
                {
                    if (entry.Key is string key &&
                        string.Equals(key, "name", StringComparison.Ordinal))
                        return entry.Value as string;
                }
                return null;
            case string raw:
                try
                {
                    using JsonDocument document = JsonDocument.Parse(raw);
                    return ReadName(document.RootElement, "name");
                }
                catch (Exception)
                {
                    return null;
                }
            default:
                return null;
        }
    }

    private static IReadOnlyDictionary<string, string>? ReadEntrySlots(object? item)
    {
        switch (item)
        {
            case JsonElement element:
                return ReadSlotsElement(element);
            case System.Collections.IDictionary dictionary:
                foreach (System.Collections.DictionaryEntry entry in dictionary)
                {
                    if (entry.Key is string key &&
                        string.Equals(key, "slots", StringComparison.Ordinal))
                    {
                        if (TryParseLayers(entry.Value, out Dictionary<string, string> slots) && slots.Count > 0)
                            return slots;
                        return null;
                    }
                }
                return null;
            case string raw:
                try
                {
                    using JsonDocument document = JsonDocument.Parse(raw);
                    return ReadSlotsElement(document.RootElement);
                }
                catch (Exception)
                {
                    return null;
                }
            default:
                return null;
        }
    }
}
