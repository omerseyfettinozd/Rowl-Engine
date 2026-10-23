using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels;

// İçe aktarma ilerleme + çakışma kararları (UI iş parçacığı dışında koşar).
public partial class AssetImportViewModel : ObservableObject
{
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(CancelCommand))]
    [NotifyPropertyChangedFor(nameof(HasResult))]
    private bool _isRunning;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ProgressFraction))]
    [NotifyPropertyChangedFor(nameof(ProgressText))]
    private int _completed;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ProgressFraction))]
    [NotifyPropertyChangedFor(nameof(ProgressText))]
    private int _total;

    [ObservableProperty]
    private string _currentFile = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasPendingConflict))]
    [NotifyPropertyChangedFor(nameof(ConflictText))]
    private ImportConflict? _pendingConflict;

    [ObservableProperty]
    private bool _applyToAll;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasResult))]
    private string? _lastResultText;

    /// <summary>Son çalışmada içe aktarılamayan dosyalar (neden ile).</summary>
    public IReadOnlyList<ImportFailure> LastFailures { get; private set; } = Array.Empty<ImportFailure>();

    // Test kancası: dönüştürücü seçenekleri (null = varsayılan araçlar).
    internal MediaConverterService.ConverterOptions? ConverterOptions { get; set; }

    /// <summary>Sonuç kartı: çalışma bittiyse ve gösterilecek sonuç varsa.</summary>
    public bool HasResult => !IsRunning && !string.IsNullOrEmpty(LastResultText);

    private CancellationTokenSource? _cts;
    private TaskCompletionSource<ImportConflictDecision>? _pendingTcs;
    private ImportConflictDecision? _rememberedDecision;

    // Test kancası: null değilse gerçek servis yerine çağrılır.
    internal Func<IReadOnlyList<string>, string, CancellationToken, IProgress<ImportProgress>, Func<ImportConflict, CancellationToken, Task<ImportConflictDecision>>, Task<List<string>>>? ImportCoreOverride { get; set; }

    public double ProgressFraction => Total == 0 ? 0 : (double)Completed / Total;

    public bool HasPendingConflict => PendingConflict is not null;

    public string ProgressText => string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Progress"), Completed, Total);

    public string ConflictText => string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Conflict.Message"), PendingConflict?.DestinationRelative ?? string.Empty);

    public async Task<IReadOnlyList<string>> RunAsync(IReadOnlyList<string> sourcePaths, string assetsRoot, Action<string>? log)
    {
        if (IsRunning)
            return Array.Empty<string>();
        if (sourcePaths is null)
            return Array.Empty<string>();

        IsRunning = true;
        Completed = 0;
        Total = sourcePaths.Count;
        CurrentFile = string.Empty;
        LastResultText = null;
        LastFailures = Array.Empty<ImportFailure>();
        PendingConflict = null;
        ApplyToAll = false;
        _rememberedDecision = null;
        _pendingTcs = null;

        var cts = new CancellationTokenSource();
        _cts = cts;
        try
        {
            // Bildirimler çalışırken UI'ye taşınır; bitişte son bildirim doğrudan uygulanır
            // (kuyruktaki son Post RunAsync dönmeden işlenmeyebilir).
            var progress = new LatestProgress(p => PostToUi(() => ApplyProgress(p)));

            List<string> result;
            var failures = new System.Collections.Concurrent.ConcurrentQueue<ImportFailure>();
            var failureSink = new FailureSink(failures);
            if (ImportCoreOverride is not null)
            {
                result = await Task.Run(() => ImportCoreOverride(sourcePaths, assetsRoot, cts.Token, progress, ResolveConflictAsync), cts.Token);
            }
            else
            {
                result = await Task.Run(() => EditorAssetImportService.ImportAssetFilesAsync(sourcePaths, assetsRoot, ConverterOptions, log, cts.Token, progress, ResolveConflictAsync, failureSink), cts.Token);
            }

            // ConfigureAwait yok: devam çağıranın bağlamında (uygulamada UI iş parçacığı) koşar.
            if (progress.Last is { } last)
                ApplyProgress(last);
            LastFailures = failures.ToArray();
            LastResultText = BuildSummary(result.Count, LastFailures);
            return result;
        }
        catch (OperationCanceledException)
        {
            LastResultText = EditorStrings.T("Import.Result.Cancelled");
            try { log?.Invoke("Import cancelled."); } catch { }
            return Array.Empty<string>();
        }
        catch (Exception ex)
        {
            LastResultText = string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Result.Failed"), ex.Message);
            try { log?.Invoke("Import failed: " + ex.Message); } catch { }
            return Array.Empty<string>();
        }
        finally
        {
            PendingConflict = null;
            _pendingTcs = null;
            IsRunning = false;
            _cts = null;
            try { cts.Dispose(); } catch { }
        }
    }

    private static string BuildSummary(int imported, IReadOnlyList<ImportFailure> failures)
    {
        string done = string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Result.Done"), imported);
        if (failures.Count == 0)
            return done;
        const int shown = 3;
        string detail = string.Join("; ", failures.Take(shown).Select(f => $"{f.FileName} — {f.Reason}"));
        if (failures.Count > shown)
            detail += "; …";
        return string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Result.DoneWithFailures"),
            imported, failures.Count, detail);
    }

    private sealed class FailureSink : ICollection<ImportFailure>
    {
        private readonly System.Collections.Concurrent.ConcurrentQueue<ImportFailure> _queue;
        public FailureSink(System.Collections.Concurrent.ConcurrentQueue<ImportFailure> queue) => _queue = queue;
        public void Add(ImportFailure item) => _queue.Enqueue(item);
        public int Count => _queue.Count;
        public bool IsReadOnly => false;
        public void Clear() { }
        public bool Contains(ImportFailure item) => _queue.Contains(item);
        public void CopyTo(ImportFailure[] array, int arrayIndex) => _queue.CopyTo(array, arrayIndex);
        public bool Remove(ImportFailure item) => false;
        public IEnumerator<ImportFailure> GetEnumerator() => _queue.GetEnumerator();
        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
    }

    public async Task<ImportConflictDecision> ResolveConflictAsync(ImportConflict conflict, CancellationToken ct)
    {
        if (_rememberedDecision is not null)
            return _rememberedDecision;

        var tcs = new TaskCompletionSource<ImportConflictDecision>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pendingTcs = tcs;
        PostToUi(() => PendingConflict = conflict);
        using (ct.Register(() => tcs.TrySetCanceled(ct)))
        {
            return await tcs.Task.ConfigureAwait(false);
        }
    }

    /// <summary>Sonuç kartını kapatır (içe aktarılan dosyalara dokunmaz).</summary>
    [RelayCommand]
    public void DismissResult() => LastResultText = null;

    [RelayCommand]
    public void Overwrite() => CompleteConflict(ImportConflictChoice.Overwrite);

    [RelayCommand]
    public void KeepBoth() => CompleteConflict(ImportConflictChoice.KeepBoth);

    [RelayCommand]
    public void Skip() => CompleteConflict(ImportConflictChoice.Skip);

    [RelayCommand(CanExecute = nameof(IsRunning))]
    public void Cancel()
    {
        try { _cts?.Cancel(); } catch { }
    }

    private void CompleteConflict(ImportConflictChoice choice)
    {
        var tcs = _pendingTcs;
        if (tcs is null)
            return;
        var decision = new ImportConflictDecision(choice, ApplyToAll);
        if (ApplyToAll)
            _rememberedDecision = decision;
        PendingConflict = null;
        tcs.TrySetResult(decision);
    }

    private void ApplyProgress(ImportProgress p)
    {
        Completed = p.Completed;
        Total = p.Total;
        CurrentFile = p.CurrentFile ?? string.Empty;
    }

    /// <summary>Son bildirimi saklayan ilerleme (Progress&lt;T&gt; bağlam yakalar; burada UI'ye taşıma PostToUi'de).</summary>
    private sealed class LatestProgress : IProgress<ImportProgress>
    {
        private readonly Action<ImportProgress> _onReport;
        private ImportProgress? _last;
        public LatestProgress(Action<ImportProgress> onReport) => _onReport = onReport;
        public ImportProgress? Last => Volatile.Read(ref _last);
        public void Report(ImportProgress value)
        {
            Volatile.Write(ref _last, value);
            _onReport(value);
        }
    }

    private static void PostToUi(Action a)
    {
        try
        {
            var app = Avalonia.Application.Current;
            if (app is not null && !Dispatcher.UIThread.CheckAccess())
            {
                Dispatcher.UIThread.Post(a);
                return;
            }
        }
        catch
        {
        }
        a();
    }
}
