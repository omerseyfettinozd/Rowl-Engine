using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>Dilim-7 parity (P1-C/D): backlog sunumu + gate'li profil/auto-skip.</summary>
public sealed class EditorPlayerLoopSlice7ParityTests
{
    private const string ReadId = "11111111-2222-3333-4444-555555555555";

    // ── Gate varsayılan-güvende (Dilim-4 mirası + Dilim-7 genişletmesi) ──

    [Fact]
    public void Gate_DefaultDisabled_NoGlobalEnable()
    {
        Assert.False(PlayerProfileAutoSkipGate.Enabled);
    }

    [Fact]
    public void Gate_ApplyToDriversPreserved_EnabledPathSameEffect()
    {
        bool previous = PlayerProfileAutoSkipGate.Enabled;
        try
        {
            var profile = new PlayerProfile { AutoEnabled = true };
            var legacy = new AutoPlayDriver();
            var session = new AutoPlayDriver();
            PlayerProfileAutoSkipGate.ApplyToDrivers(profile, legacy);
            PlayerProfileAutoSkipGate.ApplyToSession(profile, session);
            Assert.True(legacy.Enabled);
            Assert.True(session.Enabled);
            Assert.Equal(legacy.ElapsedSeconds, session.ElapsedSeconds);
            Assert.Equal(legacy.CurrentWaitSeconds, session.CurrentWaitSeconds);
        }
        finally
        {
            PlayerProfileAutoSkipGate.Enabled = previous;
        }
    }

    [Fact]
    public void Gate_PolicyReadsMirrorProfile()
    {
        var auto = new PlayerProfile { AutoEnabled = true, SkipMode = PlayerSkipMode.Off };
        Assert.True(PlayerProfileAutoSkipGate.IsAutoDriving(auto));
        Assert.False(PlayerProfileAutoSkipGate.IsSkipDriving(auto));

        var skip = new PlayerProfile { AutoEnabled = false, SkipMode = PlayerSkipMode.ReadOnly };
        Assert.False(PlayerProfileAutoSkipGate.IsAutoDriving(skip));
        Assert.True(PlayerProfileAutoSkipGate.IsSkipDriving(skip));

        var all = new PlayerProfile { SkipMode = PlayerSkipMode.All };
        Assert.True(PlayerProfileAutoSkipGate.IsSkipDriving(all));
    }

    [Fact]
    public void Gate_DisabledReadTrackingIsNoOp_DoesNotPolluteProfile()
    {
        bool previous = PlayerProfileAutoSkipGate.Enabled;
        PlayerProfileAutoSkipGate.Enabled = false;
        try
        {
            string directory = NewTempDirectory();
            try
            {
                var loop = new PlayerLoopService(new PlayerProfile(), directory);
                int added = PlayerProfileAutoSkipGate.NotePresentedGated(loop, new[] { ReadId });
                Assert.Equal(0, added);
                Assert.False(loop.Profile.IsRead(ReadId));
                Assert.Null(PlayerProfileAutoSkipGate.FlushGated(loop));
                // Flush no-op: dosya yazılmadı.
                Assert.False(File.Exists(Path.Combine(directory, PlayerProfileStore.FileName)));
            }
            finally
            {
                Directory.Delete(directory, recursive: true);
            }
        }
        finally
        {
            PlayerProfileAutoSkipGate.Enabled = previous;
        }
    }

    [Fact]
    public void Gate_EnabledReadTrackingDelegates()
    {
        bool previous = PlayerProfileAutoSkipGate.Enabled;
        PlayerProfileAutoSkipGate.Enabled = true;
        try
        {
            string directory = NewTempDirectory();
            try
            {
                var loop = new PlayerLoopService(new PlayerProfile(), directory);
                int added = PlayerProfileAutoSkipGate.NotePresentedGated(loop, new[] { ReadId });
                Assert.Equal(1, added);
                Assert.True(loop.Profile.IsRead(ReadId));
                Assert.Null(PlayerProfileAutoSkipGate.FlushGated(loop));
                Assert.True(File.Exists(Path.Combine(directory, PlayerProfileStore.FileName)));
            }
            finally
            {
                Directory.Delete(directory, recursive: true);
            }
        }
        finally
        {
            PlayerProfileAutoSkipGate.Enabled = previous;
        }
    }

    // ── Backlog sunumu (mevcut History JSON projeksiyonu üstüne) ──

    [Fact]
    public void Backlog_EmptySpeakerFallsBack_LongLineTruncates()
    {
        var entries = new List<DialogueHistoryEntry>
        {
            new() { node_id = 1, speaker = "", dialogue = new string('ü', 300), read = false, content_id = ReadId },
            new() { node_id = 2, speaker = "Evelyn", dialogue = "Kısa.", read = true, content_id = "" },
        };
        var rows = PlayerBacklogPresenter.FormatRows(entries);
        Assert.Equal(2, rows.Count);
        Assert.Equal("—", rows[0].Speaker);
        Assert.True(rows[0].Truncated);
        Assert.EndsWith(PlayerBacklogPresenter.TruncationSuffix, rows[0].Dialogue);
        Assert.False(rows[0].Read);
        Assert.Equal(ReadId, rows[0].ContentId);
        Assert.Equal("Evelyn", rows[1].Speaker);
        Assert.False(rows[1].Truncated);
    }

    [Fact]
    public void Backlog_BoundedToLastRows_OrderPreserved()
    {
        var entries = new List<DialogueHistoryEntry>();
        for (ulong i = 1; i <= 5; i++)
            entries.Add(new DialogueHistoryEntry { node_id = i, speaker = "S", dialogue = "D" });
        var rows = PlayerBacklogPresenter.FormatRows(entries, maxRows: 3);
        Assert.Equal(3, rows.Count);
        Assert.Equal(3UL, rows[0].NodeId);
        Assert.Equal(5UL, rows[2].NodeId);
        Assert.Empty(PlayerBacklogPresenter.FormatRows(entries, maxRows: 0));
        Assert.Empty(PlayerBacklogPresenter.FormatRows(new List<DialogueHistoryEntry>()));
    }

    [Fact]
    public void Backlog_SurrogatePairsNotSplit()
    {
        // Emoji (vekil çifti) sınırında kesim bölünmemeli.
        string emoji = string.Concat(System.Linq.Enumerable.Repeat("😀", 10));
        var entries = new List<DialogueHistoryEntry>
        {
            new() { node_id = 1, speaker = "S", dialogue = emoji },
        };
        var rows = PlayerBacklogPresenter.FormatRows(entries, maxCharsPerLine: 5);
        Assert.True(rows[0].Truncated);
        Assert.Equal("😀😀😀😀😀" + PlayerBacklogPresenter.TruncationSuffix, rows[0].Dialogue);
    }

    private static string NewTempDirectory()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlSlice7_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return directory;
    }
}
