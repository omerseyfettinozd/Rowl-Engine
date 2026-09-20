using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Kod-inceleme bulgusu #1 — eski şemalı slot JSON'u (thumbnail_*/
/// summary anahtarları yok) ve yanlış-tipli yükler FromJson içinde
/// fırlatmamalı; çağıran taraftaki dar JsonException filtresi UI
/// threadine sızan KeyNotFoundException'ı yakalayamıyordu.
/// </summary>
public sealed class EditorSaveSlotMetadataTests
{
    private const string LegacyJson =
        """{"saved_at":"2026-01-01T00:00:00Z","playtime_seconds":12.5,"chapter_id":"ch1","chapter_title":"Ch 1"}""";

    private const string WrongTypeJson =
        """{"saved_at":"2026-01-01T00:00:00Z","playtime_seconds":"not-a-number","chapter_id":"ch1","chapter_title":"Ch 1"}""";

    [Fact]
    public void LegacySchema_ParsesWithBackwardCompatibleDefaults()
    {
        SaveSlotMetadata? meta = SaveSlotMetadata.FromJson(0, LegacyJson);

        Assert.NotNull(meta);
        Assert.Equal("2026-01-01T00:00:00Z", meta.SavedAt);
        Assert.Equal(12.5, meta.PlaytimeSeconds);
        Assert.Equal("ch1", meta.ChapterId);
        Assert.Equal(string.Empty, meta.Summary);
        Assert.Equal(0u, meta.ThumbnailWidth);
        Assert.Equal(0u, meta.ThumbnailHeight);
        Assert.False(meta.HasThumbnail);
        Assert.Equal(string.Empty, meta.ThumbnailPngBase64);
    }

    [Fact]
    public void WrongTypePayload_FallsBackToDefaultsInsteadOfThrowing()
    {
        SaveSlotMetadata? meta = SaveSlotMetadata.FromJson(1, WrongTypeJson);

        Assert.NotNull(meta);
        Assert.Equal(0.0, meta.PlaytimeSeconds);
        Assert.Equal("ch1", meta.ChapterId);
    }
}
