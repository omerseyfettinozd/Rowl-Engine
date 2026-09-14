using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 2 Dilim 2 — read tracking at the advance point, backlog content ids,
/// and the profile-bound skip gate.
/// </summary>
public sealed class EditorPlayerLoopSlice2Tests
{
    private static readonly string ReadId = "11111111-2222-3333-4444-555555555555";
    private static readonly string OtherId = "22222222-3333-4444-5555-666666666666";

    // ── SkipGate matrix ──────────────────────────────────────────────

    [Fact]
    public void SkipGate_OffNeverSkips()
    {
        var profile = new PlayerProfile { SkipMode = PlayerSkipMode.Off };
        profile.MarkRead(ReadId);
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.Off, new[] { ReadId }, profile.IsRead, hasChoices: false));
    }

    [Fact]
    public void SkipGate_AllSkipsWithoutChoices()
    {
        var profile = new PlayerProfile { SkipMode = PlayerSkipMode.All };
        Assert.True(SkipGate.ShouldSkip(PlayerSkipMode.All, new[] { "unread-id-is-fine-here" }, _ => false, hasChoices: false));
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.All, new[] { ReadId }, profile.IsRead, hasChoices: true));
    }

    [Fact]
    public void SkipGate_ReadOnlyNeedsEveryIdRecorded()
    {
        var profile = new PlayerProfile { SkipMode = PlayerSkipMode.ReadOnly };
        profile.MarkRead(ReadId);
        Assert.True(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, new[] { ReadId }, profile.IsRead, hasChoices: false));
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, new[] { ReadId, OtherId }, profile.IsRead, hasChoices: false));
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, new[] { ReadId }, profile.IsRead, hasChoices: true));
    }

    [Fact]
    public void SkipGate_ReadOnlyFailsClosed()
    {
        var profile = new PlayerProfile { SkipMode = PlayerSkipMode.ReadOnly };
        // No presented lines, legacy empty ids and malformed ids never count.
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, Array.Empty<string>(), profile.IsRead, hasChoices: false));
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, new[] { "" }, profile.IsRead, hasChoices: false));
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, new[] { "not-a-uuid" }, profile.IsRead, hasChoices: false));
        Assert.False(SkipGate.ShouldSkip(PlayerSkipMode.ReadOnly, new string?[] { null }, profile.IsRead, hasChoices: false));
    }

    // ── PlayerLoopService tracking ───────────────────────────────────

    [Fact]
    public void AdvanceAndTrack_MarksDepartedLineAndPersists()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(new PlayerProfile(), directory);
            var presented = new List<string?> { ReadId };
            int advances = 0;

            string? error = loop.AdvanceAndTrack(
                () => presented.ToList(),
                _ => { advances++; presented = new List<string?> { OtherId }; });

            Assert.Null(error);
            Assert.Equal(1, advances);
            // The departed line is read; the newly presented one is not yet.
            Assert.True(loop.Profile.IsRead(ReadId));
            Assert.False(loop.Profile.IsRead(OtherId));

            var reloaded = PlayerLoopService.Load(directory);
            Assert.True(reloaded.Profile.IsRead(ReadId));
            Assert.False(reloaded.Profile.IsRead(OtherId));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void AdvanceAndTrack_QueryFailureAbortsBeforeAdvancing()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(new PlayerProfile(), directory);
            int advances = 0;
            string? error = loop.AdvanceAndTrack(
                () => throw new InvalidOperationException("handle lost"),
                _ => advances++);
            Assert.NotNull(error);
            Assert.Equal(0, advances);
            Assert.Empty(loop.Profile.ReadContentIds);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void Flush_IsNoopWithoutNewReads()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(new PlayerProfile(), directory);
            Assert.Null(loop.Flush());
            Assert.False(File.Exists(Path.Combine(directory, PlayerProfileStore.FileName)));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void TrySkipStep_AdvancesOnlyWhenGateAllows()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(
                new PlayerProfile { SkipMode = PlayerSkipMode.ReadOnly }, directory);
            int advances = 0;
            Action<uint> advance = _ => advances++;

            // Unread line: no step.
            Assert.False(loop.TrySkipStep(() => new[] { ReadId }, () => false, advance, out string? firstError));
            Assert.Null(firstError);
            Assert.Equal(0, advances);

            // Read line: one step, and the line stays recorded.
            loop.NotePresented(new[] { ReadId });
            Assert.True(loop.TrySkipStep(() => new[] { ReadId }, () => false, advance, out string? secondError));
            Assert.Null(secondError);
            Assert.Equal(1, advances);

            // Choices stop the skip even for read lines.
            Assert.False(loop.TrySkipStep(() => new[] { ReadId }, () => true, advance, out _));
            Assert.Equal(1, advances);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void TrySkipStep_AllModeSkipsUnreadLines()
    {
        string directory = NewTempDirectory();
        try
        {
            var loop = new PlayerLoopService(
                new PlayerProfile { SkipMode = PlayerSkipMode.All }, directory);
            int advances = 0;
            Assert.True(loop.TrySkipStep(() => new[] { OtherId }, () => false, _ => advances++, out string? error));
            Assert.Null(error);
            Assert.Equal(1, advances);
            Assert.True(loop.Profile.IsRead(OtherId));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── Backlog content ids ──────────────────────────────────────────

    [Fact]
    public void BacklogEntries_CarryContentIds()
    {
        const string json = """
            [
              {"node_id":101,"speaker":"Evelyn","dialogue":"Remember me.","read":true,
               "content_id":"11111111-2222-3333-4444-555555555555"},
              {"node_id":102,"speaker":"Mina","dialogue":"Legacy line.","read":true}
            ]
            """;
        var entries = JsonSerializer.Deserialize<List<DialogueHistoryEntry>>(json);
        Assert.NotNull(entries);
        Assert.Equal(2, entries!.Count);
        Assert.Equal(ReadId, entries[0].content_id);
        // Legacy entries without the key stay loadable with an empty id.
        Assert.Equal(string.Empty, entries[1].content_id);
    }

    private static string NewTempDirectory()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlSlice2_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return directory;
    }
}
