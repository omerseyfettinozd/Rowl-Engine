using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Result of the startup canonical-document check (read-only).</summary>
internal sealed record RecoveryStatus(
    bool CanonicalOk,
    string? CanonicalError,
    bool HasLastGood,
    int JournalEntries,
    bool DirtyFlagPresent);

/// <summary>
/// A user-approved restore candidate. Built from the journal or the
/// last-good copy; building one never touches the canonical document.
/// </summary>
internal sealed record RestoreOffer(
    string Source,
    string SnapshotJson,
    int FormatVersion,
    int NodeCount);

/// <summary>
/// Faz 4 Dilim 5 — UI-free crash recovery: dirty flag, append-only journal,
/// last-good snapshot copy and startup validation. Takes a detached
/// <see cref="StoryGraphSaveSnapshot"/> (never live view models), touches
/// only <c>Assets/json/.recovery/</c> plus the canonical graph file on
/// explicit restore, and never throws — every entry point is fail-closed
/// so recovery bookkeeping can never break a save.
/// </summary>
internal static class CrashRecoveryService
{
    internal const string RecoveryDirName = ".recovery";
    internal const string DirtyFlagName = "dirty.flag";
    internal const string LastGoodName = "last-good.json";
    internal const string LastGoodHashName = "last-good.json.sha256";
    internal const int MaxJournalFiles = 50;
    internal const long MaxJournalBytesTotal = 10L * 1024 * 1024;

    internal static string GetRecoveryDir(string assetsJsonPath)
        => Path.Combine(assetsJsonPath, RecoveryDirName);

    // Dirty flag

    public static void MarkDirty(string assetsJsonPath)
    {
        try
        {
            string dir = GetRecoveryDir(assetsJsonPath);
            Directory.CreateDirectory(dir);
            File.WriteAllText(
                Path.Combine(dir, DirtyFlagName),
                DateTime.UtcNow.ToString("o"));
        }
        catch (Exception)
        {
            // Bookkeeping must never break editing.
        }
    }

    public static void ClearDirty(string assetsJsonPath)
    {
        try
        {
            string flag = Path.Combine(GetRecoveryDir(assetsJsonPath), DirtyFlagName);
            if (File.Exists(flag))
                File.Delete(flag);
        }
        catch (Exception)
        {
        }
    }

    public static bool IsDirty(string assetsJsonPath)
    {
        try
        {
            return File.Exists(Path.Combine(GetRecoveryDir(assetsJsonPath), DirtyFlagName));
        }
        catch (Exception)
        {
            return false;
        }
    }

    // Save wrapper
    // Called from the TryWriteSnapshot path (sequence semantics unchanged):
    // marks dirty before the write, journals + refreshes last-good after a
    // successful write, clears dirty last.

    public static bool TryWriteSnapshotWithRecovery(
        StoryGraphSaveSnapshot snapshot,
        string assetsJsonPath,
        long sequence,
        Func<long> latestSequence,
        Action<string>? log = null)
    {
        MarkDirty(assetsJsonPath);
        bool written = StoryGraphSaveService.TryWriteSnapshot(
            snapshot, assetsJsonPath, sequence, latestSequence, log);
        if (written)
            NoteSuccessfulSave(assetsJsonPath, snapshot);
        return written;
    }

    public static void NoteSuccessfulSave(string assetsJsonPath, StoryGraphSaveSnapshot snapshot)
    {
        try
        {
            string snapshotJson = StoryGraphSaveService.SerializeFullGraph(snapshot);
            int formatVersion = DetectFormatVersion(snapshotJson, snapshot);
            AppendJournalEntry(assetsJsonPath, DateTime.UtcNow.Ticks, snapshotJson, formatVersion);
            UpdateLastGood(assetsJsonPath);
            ClearDirty(assetsJsonPath);
        }
        catch (Exception)
        {
        }
    }

    // Journal
    // One JSON object per line: {seq, utc, sha256, format_version,
    // snapshot}. Flushed with fsync; rotated to the newest 50 files /
    // 10 MB total.

    public static bool AppendJournalEntry(
        string assetsJsonPath, long sequence, string snapshotJson, int formatVersion)
    {
        try
        {
            string dir = GetRecoveryDir(assetsJsonPath);
            Directory.CreateDirectory(dir);
            string line = JsonSerializer.Serialize(new Dictionary<string, object?>
            {
                ["seq"] = sequence,
                ["utc"] = DateTime.UtcNow.ToString("o"),
                ["sha256"] = ComputeSha256(snapshotJson),
                ["format_version"] = formatVersion,
                ["snapshot"] = snapshotJson,
            }) + "\n";
            byte[] bytes = Encoding.UTF8.GetBytes(line);
            string journal = Path.Combine(dir, $"journal-{DateTime.Now:yyyyMMdd}.jsonl");
            using (var stream = new FileStream(journal, FileMode.Append, FileAccess.Write, FileShare.Read))
            {
                stream.Write(bytes, 0, bytes.Length);
                stream.Flush(true); // fsync: the entry survives a kill right after.
            }
            RotateJournals(dir);
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    internal static void RotateJournals(string recoveryDir)
    {
        try
        {
            var journals = Directory.GetFiles(recoveryDir, "journal-*.jsonl")
                .OrderBy(p => p, StringComparer.Ordinal)
                .ToList();
            while (journals.Count > MaxJournalFiles)
            {
                TryDelete(journals[0]);
                journals.RemoveAt(0);
            }
            long total = journals.Sum(p => SafeLength(p));
            while (total > MaxJournalBytesTotal && journals.Count > 1)
            {
                total -= SafeLength(journals[0]);
                TryDelete(journals[0]);
                journals.RemoveAt(0);
            }
        }
        catch (Exception)
        {
        }
    }

    /// <summary>All valid journal snapshots, oldest first (replay order).</summary>
    public static IReadOnlyList<string> ReplayJournal(string assetsJsonPath)
    {
        var replay = new List<string>();
        try
        {
            string dir = GetRecoveryDir(assetsJsonPath);
            if (!Directory.Exists(dir))
                return replay;
            foreach (string journal in Directory.GetFiles(dir, "journal-*.jsonl")
                         .OrderBy(p => p, StringComparer.Ordinal))
            {
                foreach (string line in File.ReadLines(journal))
                {
                    if (TryExtractSnapshot(line, out string? snapshot) && snapshot is not null)
                        replay.Add(snapshot);
                }
            }
        }
        catch (Exception)
        {
        }
        return replay;
    }

    // Last-good copy
    // refreshed after every successful write: canonical full_story_graph.json
    // plus a .sha256 sidecar, both written atomically.

    public static bool UpdateLastGood(string assetsJsonPath)
    {
        try
        {
            string canonical = Path.Combine(assetsJsonPath, "full_story_graph.json");
            if (!File.Exists(canonical))
                return false;
            string json = File.ReadAllText(canonical);
            string dir = GetRecoveryDir(assetsJsonPath);
            Directory.CreateDirectory(dir);
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(dir, LastGoodName), json);
            ProjectFileSystem.WriteAllTextAtomically(
                Path.Combine(dir, LastGoodHashName), ComputeSha256(json));
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    // Startup validation + restore offer
    // Validates the canonical document (parse + nodes array + structure
    // shape). A broken/missing canonical never triggers an automatic
    // overwrite; the caller offers the RestoreOffer to the user instead.

    public static RecoveryStatus CheckAtStartup(string assetsPath, string assetsJsonPath)
    {
        bool canonicalOk = false;
        string? canonicalError = null;
        try
        {
            if (StoryGraphDocumentReader.TryRead(
                    assetsPath, assetsJsonPath, out var document, out _, out string? readError))
            {
                using (document!)
                {
                    if (GraphStructureHydrator.TryParse(
                            document!.RootElement, out _, out var structureErrors))
                    {
                        canonicalOk = true;
                    }
                    else
                    {
                        canonicalError = string.Join(" ", structureErrors);
                    }
                }
            }
            else
            {
                canonicalError = readError ?? "Story graph file is missing.";
            }
        }
        catch (Exception error)
        {
            canonicalOk = false;
            canonicalError = error.Message;
        }

        bool hasLastGood = false;
        int journalEntries = 0;
        bool dirty = false;
        try
        {
            string dir = GetRecoveryDir(assetsJsonPath);
            hasLastGood = File.Exists(Path.Combine(dir, LastGoodName));
            dirty = File.Exists(Path.Combine(dir, DirtyFlagName));
            if (Directory.Exists(dir))
            {
                foreach (string journal in Directory.GetFiles(dir, "journal-*.jsonl"))
                {
                    try
                    {
                        foreach (string line in File.ReadLines(journal))
                        {
                            if (TryExtractSnapshot(line, out _))
                                journalEntries++;
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }
        }
        catch (Exception)
        {
        }
        return new RecoveryStatus(canonicalOk, canonicalError, hasLastGood, journalEntries, dirty);
    }

    /// <summary>
    /// Builds a restore candidate: newest valid journal entry first,
    /// hash-verified last-good copy as fallback. Read-only.
    /// </summary>
    public static bool TryBuildRestoreOffer(
        string assetsPath,
        string assetsJsonPath,
        out RestoreOffer? offer,
        out string? error)
    {
        offer = null;
        error = null;
        try
        {
            var replay = ReplayJournal(assetsJsonPath);
            for (int i = replay.Count - 1; i >= 0; i--)
            {
                if (TryDescribeSnapshot(replay[i], out int version, out int nodes))
                {
                    offer = new RestoreOffer("journal", replay[i], version, nodes);
                    return true;
                }
            }

            string dir = GetRecoveryDir(assetsJsonPath);
            string lastGood = Path.Combine(dir, LastGoodName);
            if (File.Exists(lastGood))
            {
                string json = File.ReadAllText(lastGood);
                string sidecar = Path.Combine(dir, LastGoodHashName);
                if (File.Exists(sidecar))
                {
                    string expected = File.ReadAllText(sidecar).Trim();
                    if (!string.Equals(expected, ComputeSha256(json), StringComparison.OrdinalIgnoreCase))
                    {
                        error = "Last-good copy failed hash verification; journal has no valid entry.";
                        return false;
                    }
                }
                if (TryDescribeSnapshot(json, out int version, out int nodes))
                {
                    offer = new RestoreOffer("last-good", json, version, nodes);
                    return true;
                }
                error = "Last-good copy is not a valid story graph.";
                return false;
            }

            error = "No journal entry or last-good copy is available.";
            return false;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }

    /// <summary>
    /// Explicit user-approved restore: atomically replaces the canonical
    /// graph file with the offer content. Never called automatically.
    /// </summary>
    public static bool RestoreOfferToCanonical(string assetsJsonPath, RestoreOffer offer)
    {
        if (offer is null || string.IsNullOrWhiteSpace(offer.SnapshotJson))
            return false;
        try
        {
            if (!TryDescribeSnapshot(offer.SnapshotJson, out _, out _))
                return false;
            ProjectFileSystem.WriteAllTextAtomically(
                Path.Combine(assetsJsonPath, "full_story_graph.json"), offer.SnapshotJson);
            ClearDirty(assetsJsonPath);
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    // Helpers

    internal static string ComputeSha256(string text)
    {
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(text));
        var builder = new StringBuilder(hash.Length * 2);
        foreach (byte b in hash)
            builder.Append(b.ToString("x2"));
        return builder.ToString();
    }

    private static bool TryExtractSnapshot(string line, out string? snapshot)
    {
        snapshot = null;
        try
        {
            if (string.IsNullOrWhiteSpace(line))
                return false;
            using var document = JsonDocument.Parse(line);
            if (!document.RootElement.TryGetProperty("snapshot", out JsonElement snap) ||
                snap.ValueKind != JsonValueKind.String)
                return false;
            string? json = snap.GetString();
            if (string.IsNullOrWhiteSpace(json))
                return false;
            if (!TryDescribeSnapshot(json, out _, out _))
                return false;
            if (document.RootElement.TryGetProperty("sha256", out JsonElement hash) &&
                hash.ValueKind == JsonValueKind.String &&
                !string.Equals(hash.GetString(), ComputeSha256(json), StringComparison.OrdinalIgnoreCase))
                return false;
            snapshot = json;
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    private static bool TryDescribeSnapshot(string snapshotJson, out int formatVersion, out int nodeCount)
    {
        formatVersion = GraphStructureLimits.LegacyVersion;
        nodeCount = 0;
        try
        {
            using var document = JsonDocument.Parse(snapshotJson);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return false;
            if (!root.TryGetProperty("nodes", out JsonElement nodes) ||
                nodes.ValueKind != JsonValueKind.Array)
                return false;
            nodeCount = nodes.GetArrayLength();
            if (root.TryGetProperty("format_version", out JsonElement version) &&
                version.ValueKind == JsonValueKind.Number &&
                version.TryGetInt32(out int parsed))
                formatVersion = parsed;
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    private static int DetectFormatVersion(string snapshotJson, StoryGraphSaveSnapshot snapshot)
    {
        try
        {
            using var document = JsonDocument.Parse(snapshotJson);
            if (document.RootElement.TryGetProperty("format_version", out JsonElement version) &&
                version.ValueKind == JsonValueKind.Number &&
                version.TryGetInt32(out int parsed))
                return parsed;
        }
        catch (Exception)
        {
        }
        var structure = snapshot.Structure ?? new GraphStructureDocument();
        return !structure.IsEmpty || snapshot.Nodes.Any(n => !string.IsNullOrEmpty(n.ChapterId))
            ? GraphStructureLimits.CurrentVersion
            : GraphStructureLimits.LegacyVersion;
    }

    private static long SafeLength(string path)
    {
        try
        {
            return new FileInfo(path).Length;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception)
        {
        }
    }
}
