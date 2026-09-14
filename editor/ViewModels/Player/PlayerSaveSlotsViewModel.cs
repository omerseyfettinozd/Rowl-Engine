using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.ViewModels.Player;

/// <summary>Save/load picker mode.</summary>
public enum PlayerSlotsMode
{
    Save,
    Load,
}

/// <summary>
/// One slot row (metadata + raw thumbnail PNG bytes). Views decode the
/// bytes to a Bitmap via <c>PngBytesToBitmapConverter</c> on the UI
/// thread; this VM stays free of platform render services so it remains
/// unit-testable.
/// </summary>
public sealed class PlayerSaveSlotEntry
{
    public PlayerSaveSlotEntry(int index, SaveSlotMetadata? metadata, byte[]? thumbnailPng)
    {
        Index = index;
        Metadata = metadata;
        ThumbnailPng = thumbnailPng;
    }

    public int Index { get; }
    public string Label => $"Slot {Index + 1}";
    public bool Occupied => Metadata is not null;
    public string State => Occupied ? "Dolu" : "Boş";
    public string SavedAt => Metadata?.SavedAt ?? string.Empty;
    public string Chapter => Metadata?.ChapterTitle ?? string.Empty;
    public string Summary => Metadata?.Summary ?? string.Empty;
    public bool HasThumbnailImage => ThumbnailPng is { Length: > 0 };
    public SaveSlotMetadata? Metadata { get; }
    public byte[]? ThumbnailPng { get; }
}

/// <summary>
/// Faz 2 Dilim 5 — paged save/load picker over <see cref="IPlayerEngine"/>.
/// Loads display metadata only for occupied slots on the visible page;
/// thumbnails decode lazily per entry and degrade to null (counted in
/// debug output) instead of breaking the picker.
/// </summary>
public sealed partial class PlayerSaveSlotsViewModel : ViewModelBase
{
    public const int PageSize = 12;
    public const int SlotCount = 100;

    public static int TotalPages => (SlotCount + PageSize - 1) / PageSize;

    private readonly IPlayerEngine _engine;
    private int _thumbnailDecodeErrors;

    public PlayerSaveSlotsViewModel(IPlayerEngine engine)
    {
        _engine = engine ?? throw new ArgumentNullException(nameof(engine));
        Refresh();
    }

    [ObservableProperty]
    private int _page;

    [ObservableProperty]
    private PlayerSlotsMode _mode = PlayerSlotsMode.Save;

    public ObservableCollection<PlayerSaveSlotEntry> Entries { get; } = new();

    public int ThumbnailDecodeErrors => _thumbnailDecodeErrors;

    public void NextPage()
    {
        Page = Math.Min(TotalPages - 1, Page + 1);
        Refresh();
    }

    public void PreviousPage()
    {
        Page = Math.Max(0, Page - 1);
        Refresh();
    }

    public void Refresh()
    {
        Entries.Clear();
        int start = Page * PageSize;
        for (int index = start; index < Math.Min(start + PageSize, SlotCount); index++)
        {
            SaveSlotMetadata? metadata = null;
            if (_engine.HasSlot(index))
                metadata = _engine.GetSlotMetadata(index);
            Entries.Add(new PlayerSaveSlotEntry(index, metadata, DecodeThumbnail(metadata)));
        }
    }

    /// <summary>Most recently saved occupied slot, or null when all are empty.</summary>
    public int? FindLatestOccupied()
    {
        int? latest = null;
        string latestStamp = string.Empty;
        for (int index = 0; index < SlotCount; index++)
        {
            if (!_engine.HasSlot(index))
                continue;
            string stamp = _engine.GetSlotMetadata(index)?.SavedAt ?? string.Empty;
            if (latest is null || string.Compare(stamp, latestStamp, StringComparison.Ordinal) > 0)
            {
                latest = index;
                latestStamp = stamp;
            }
        }
        return latest;
    }

    private static readonly byte[] PngMagic =
        { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    private byte[]? DecodeThumbnail(SaveSlotMetadata? metadata)
    {
        if (metadata is not { HasThumbnail: true } || string.IsNullOrWhiteSpace(metadata.ThumbnailPngBase64))
            return null;
        try
        {
            byte[] bytes = Convert.FromBase64String(metadata.ThumbnailPngBase64);
            if (bytes.Length < PngMagic.Length)
                throw new FormatException("Thumbnail payload is shorter than a PNG header.");
            for (int i = 0; i < PngMagic.Length; i++)
            {
                if (bytes[i] != PngMagic[i])
                    throw new FormatException("Thumbnail payload is not a PNG image.");
            }
            return bytes;
        }
        catch (Exception decodeFailure) when (decodeFailure is FormatException or ArgumentException)
        {
            _thumbnailDecodeErrors++;
            Debug.WriteLine($"Player slot thumbnail decode failed ({_thumbnailDecodeErrors}): {decodeFailure.Message}");
            return null;
        }
    }
}
