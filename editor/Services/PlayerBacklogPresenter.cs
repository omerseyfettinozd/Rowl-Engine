using System;
using System.Collections.Generic;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Ekrana çıkan tek backlog satırı (yan etkisiz projeksiyon).
/// </summary>
public sealed class BacklogDisplayRow
{
    public BacklogDisplayRow(ulong nodeId, string speaker, string dialogue, bool read, bool truncated, string contentId)
    {
        NodeId = nodeId;
        Speaker = speaker;
        Dialogue = dialogue;
        Read = read;
        Truncated = truncated;
        ContentId = contentId;
    }

    public ulong NodeId { get; }
    public string Speaker { get; }
    public string Dialogue { get; }
    public bool Read { get; }
    public bool Truncated { get; }
    public string ContentId { get; }
}

/// <summary>
/// Dilim-7 parity (P1-C): mevcut C ABI History JSON projeksiyonu
/// (<see cref="DialogueHistoryEntry"/>, EngineHost üzerinden) üstüne ince
/// backlog sunumu. Saf + yan etkisiz: history replay YOK (kayıt yan etkileri
/// asla yeniden çalışmaz), okuma/yazma API'si yok, mevcut
/// BacklogViewModel/PlayerViewModel akışlarına dokunmaz — görünümler bu
/// biçimi bağlama zamanında kullanır.
/// </summary>
public static class PlayerBacklogPresenter
{
    public const int DefaultMaxRows = 100;
    public const int DefaultMaxCharsPerLine = 280;
    public const string EmptySpeakerFallback = "—";
    public const string TruncationSuffix = "...";

    /// <summary>
    /// Son <paramref name="maxRows"/> satırı ekran formuna çevirir (sıra korunur).
    /// Boş konuşmacı "—" olur; uzun satırlar skaler sınırında "..." ekiyle
    /// kısalır (karar verilmez — okundu/atlama kararı <see cref="SkipGate"/> işidir).
    /// </summary>
    public static IReadOnlyList<BacklogDisplayRow> FormatRows(
        IReadOnlyList<DialogueHistoryEntry> entries,
        int maxRows = DefaultMaxRows,
        int maxCharsPerLine = DefaultMaxCharsPerLine)
    {
        ArgumentNullException.ThrowIfNull(entries);
        maxRows = Math.Max(0, maxRows);
        maxCharsPerLine = Math.Max(0, maxCharsPerLine);
        var rows = new List<BacklogDisplayRow>();
        if (entries.Count == 0 || maxRows == 0)
            return rows;
        int start = Math.Max(0, entries.Count - maxRows);
        for (int i = start; i < entries.Count; i++)
        {
            DialogueHistoryEntry entry = entries[i] ?? new DialogueHistoryEntry();
            string speaker = string.IsNullOrWhiteSpace(entry.speaker) ? EmptySpeakerFallback : entry.speaker;
            string dialogue = entry.dialogue ?? string.Empty;
            bool truncated = false;
            if (CountScalars(dialogue) > maxCharsPerLine)
            {
                dialogue = TruncateScalars(dialogue, maxCharsPerLine) + TruncationSuffix;
                truncated = true;
            }
            rows.Add(new BacklogDisplayRow(
                entry.node_id, speaker, dialogue, entry.read, truncated, entry.content_id ?? string.Empty));
        }
        return rows;
    }

    // UTF-16 vekil (surrogate) çiftlerini bölmeden kısaltma: .NET string'i
    // UTF-16'dır; Rune sayımı skaler sınırında keser (native sunumdaki skaler
    // sözleşmesinin yönetilen izdüşümü).
    private static int CountScalars(string text)
    {
        int count = 0;
        for (int i = 0; i < text.Length; i++)
        {
            if (char.IsHighSurrogate(text[i]) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1]))
                i++;
            count++;
        }
        return count;
    }

    private static string TruncateScalars(string text, int maxChars)
    {
        int count = 0;
        int i = 0;
        while (i < text.Length && count < maxChars)
        {
            if (char.IsHighSurrogate(text[i]) && i + 1 < text.Length && char.IsLowSurrogate(text[i + 1]))
                i += 2;
            else
                i++;
            count++;
        }
        return text.Substring(0, i);
    }
}
