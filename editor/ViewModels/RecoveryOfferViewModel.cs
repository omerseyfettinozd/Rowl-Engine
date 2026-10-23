using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels;

// Kurtarma teklifini gösterir; dosyaları silmez, yalnız oturumda gizler.
public partial class RecoveryOfferViewModel : ObservableObject
{
    private readonly Func<bool> _restore;
    private readonly Action _dismiss;

    public string SourceLabel { get; }

    public int NodeCount { get; }

    public string Summary { get; }

    public string Reason { get; }

    public IReadOnlyList<string> NodeTitles { get; }

    [ObservableProperty]
    private bool _isInspecting;

    [ObservableProperty]
    private bool _isConfirmingRestore;

    [ObservableProperty]
    private string? _errorText;

    internal RecoveryOfferViewModel(RestoreOffer offer, string? canonicalError, Func<bool> restore, Action dismiss)
    {
        ArgumentNullException.ThrowIfNull(offer);
        _restore = restore ?? throw new ArgumentNullException(nameof(restore));
        _dismiss = dismiss ?? throw new ArgumentNullException(nameof(dismiss));

        if (offer.Source.Equals("journal", StringComparison.Ordinal))
            SourceLabel = EditorStrings.T("Recovery.Source.Journal");
        else if (offer.Source.Equals("last-good", StringComparison.Ordinal))
            SourceLabel = EditorStrings.T("Recovery.Source.LastGood");
        else
            SourceLabel = offer.Source;

        NodeCount = offer.NodeCount;
        Summary = string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Recovery.Summary"), SourceLabel, NodeCount);
        Reason = string.IsNullOrWhiteSpace(canonicalError)
            ? EditorStrings.T("Recovery.Reason.Unknown")
            : canonicalError;
        NodeTitles = ExtractTitles(offer.SnapshotJson);
    }

    // Kök nodes dizisindeki title dizesi; bozuksa boş liste.
    private static IReadOnlyList<string> ExtractTitles(string? snapshotJson)
    {
        if (string.IsNullOrWhiteSpace(snapshotJson))
            return Array.Empty<string>();
        try
        {
            using var doc = JsonDocument.Parse(snapshotJson);
            if (!doc.RootElement.TryGetProperty("nodes", out JsonElement nodes) || nodes.ValueKind != JsonValueKind.Array)
                return Array.Empty<string>();
            int total = nodes.GetArrayLength();
            var titles = new List<string>(Math.Min(total, 51));
            foreach (var node in nodes.EnumerateArray())
            {
                if (node.ValueKind == JsonValueKind.Object
                    && node.TryGetProperty("title", out JsonElement title)
                    && title.ValueKind == JsonValueKind.String)
                {
                    string? s = title.GetString();
                    if (!string.IsNullOrEmpty(s))
                        titles.Add(s);
                }
            }
            int shown = Math.Min(50, titles.Count);
            var result = titles.Take(shown).ToList();
            if (total > shown)
            {
                int rest = total - shown;
                result.Add(string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Recovery.MoreNodes"), rest));
            }
            return result;
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    [RelayCommand]
    public void ToggleInspect() => IsInspecting = !IsInspecting;

    [RelayCommand]
    public void Restore()
    {
        if (!IsConfirmingRestore)
        {
            ErrorText = null;
            IsConfirmingRestore = true;
            return;
        }
        bool ok;
        try
        {
            ok = _restore();
        }
        catch
        {
            ok = false;
        }
        if (ok)
        {
            IsConfirmingRestore = false;
        }
        else
        {
            ErrorText = EditorStrings.T("Recovery.Failed");
            IsConfirmingRestore = false;
        }
    }

    [RelayCommand]
    public void CancelRestore() => IsConfirmingRestore = false;

    /// <summary>Kurtarma dosyaları SİLİNMEZ; yalnız teklif bu oturumda kapanır.</summary>
    [RelayCommand]
    public void Dismiss() => _dismiss();
}
