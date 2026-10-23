using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C04 — bozulmuş/kurcalanmış kurtarma günlüğü: kısaltılmış, çöp, yanlış
/// sha256, gelecek sürüm. Hiçbiri kanonik projeyi ezmez, teşhis döner ve
/// başlangıç denetimi (editörün açılışı) atmaz.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorTamperedRecoveryJournalTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "RowlTamperJournal_" + Guid.NewGuid().ToString("N"));
    private string AssetsDir => Path.Combine(_root, "Assets");
    private string JsonDir => Path.Combine(AssetsDir, "json");
    private string Recovery => Path.Combine(JsonDir, CrashRecoveryService.RecoveryDirName);
    private string Canonical => Path.Combine(JsonDir, "full_story_graph.json");
    private const string CanonicalBytes = "not json{{{ canonical";

    public EditorTamperedRecoveryJournalTests()
    {
        Directory.CreateDirectory(Recovery);
        File.WriteAllText(Canonical, CanonicalBytes);
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch (Exception) { }
    }

    private static string Snapshot(int version, int nodes = 2)
    {
        var list = string.Join(",", Enumerable.Range(1, nodes).Select(i => "{\"id\":" + i + "}"));
        return "{\"format_version\":" + version + ",\"nodes\":[" + list + "]}";
    }

    private static string Line(string snapshot, string? sha = null, int version = 5)
        => JsonSerializer.Serialize(new Dictionary<string, object?>
        {
            ["seq"] = 1,
            ["utc"] = DateTime.UtcNow.ToString("o"),
            ["sha256"] = sha ?? CrashRecoveryService.ComputeSha256(snapshot),
            ["format_version"] = version,
            ["snapshot"] = snapshot,
        });

    private void WriteJournal(params string[] lines)
        => File.WriteAllText(Path.Combine(Recovery, "journal-20260101.jsonl"), string.Join("\n", lines) + "\n");

    private void AssertNoOffer_NoOverwrite_StartupOk(string expectedErrorPart)
    {
        var status = CrashRecoveryService.CheckAtStartup(_root, JsonDir);
        Assert.False(status.CanonicalOk);
        Assert.Equal(0, status.JournalEntries);

        Assert.False(CrashRecoveryService.TryBuildRestoreOffer(_root, JsonDir, out var offer, out string? error));
        Assert.Null(offer);
        Assert.False(string.IsNullOrWhiteSpace(error));
        Assert.Contains(expectedErrorPart, error!, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(CanonicalBytes, File.ReadAllText(Canonical));
    }

    [Fact]
    public void TruncatedJournalLine_IsRejected_WithDiagnostic()
    {
        string full = Line(Snapshot(5));
        WriteJournal(full.Substring(0, full.Length / 2));
        AssertNoOffer_NoOverwrite_StartupOk("rejected");
    }

    [Fact]
    public void GarbageJournal_IsRejected_WithDiagnostic()
    {
        File.WriteAllBytes(Path.Combine(Recovery, "journal-20260101.jsonl"),
            new byte[] { 0xFF, 0xFE, 0x00, 0x01, 0x7B, 0x0A, 0xC3, 0x28, 0x0A });
        AssertNoOffer_NoOverwrite_StartupOk("rejected");
    }

    [Fact]
    public void WrongChecksumJournalLine_IsRejected_WithDiagnostic()
    {
        WriteJournal(Line(Snapshot(5), sha: new string('0', 64)));
        AssertNoOffer_NoOverwrite_StartupOk("rejected");
    }

    [Fact]
    public void FutureVersionJournalLine_IsRejected_AndNeverRestored()
    {
        int future = GraphStructureLimits.CurrentVersion + 1;
        WriteJournal(Line(Snapshot(future), version: future));
        AssertNoOffer_NoOverwrite_StartupOk("rejected");

        // Elle kurulmuş bir teklif de kanonik dosyayı ezmemeli.
        var forged = new RestoreOffer("journal", Snapshot(future), future, 2);
        Assert.False(CrashRecoveryService.RestoreOfferToCanonical(JsonDir, forged));
        Assert.Equal(CanonicalBytes, File.ReadAllText(Canonical));
    }

    [Fact]
    public void BadLastGoodHash_IsRejected_AndCanonicalKept()
    {
        File.WriteAllText(Path.Combine(Recovery, CrashRecoveryService.LastGoodName), Snapshot(5));
        File.WriteAllText(Path.Combine(Recovery, CrashRecoveryService.LastGoodHashName), new string('a', 64));
        AssertNoOffer_NoOverwrite_StartupOk("hash");
    }

    [Fact]
    public void FutureVersionLastGood_IsRejected()
    {
        int future = GraphStructureLimits.CurrentVersion + 1;
        string json = Snapshot(future);
        File.WriteAllText(Path.Combine(Recovery, CrashRecoveryService.LastGoodName), json);
        File.WriteAllText(Path.Combine(Recovery, CrashRecoveryService.LastGoodHashName),
            CrashRecoveryService.ComputeSha256(json));
        AssertNoOffer_NoOverwrite_StartupOk("not a valid");
    }

    [Fact]
    public void TamperedLineIsSkipped_ValidNeighbourStillOffered()
    {
        WriteJournal(
            Line(Snapshot(5, 2)),
            Line(Snapshot(5, 3), sha: new string('0', 64)),
            "{ garbage");
        Assert.True(CrashRecoveryService.TryBuildRestoreOffer(_root, JsonDir, out var offer, out _));
        Assert.Equal(2, offer!.NodeCount);
        Assert.Equal(CanonicalBytes, File.ReadAllText(Canonical));
    }
}
