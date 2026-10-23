using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

public sealed class EditorAssetImportViewModelTests
{
    private static async Task<T> WithTimeout<T>(Task<T> task, int ms = 5000)
    {
        var done = await Task.WhenAny(task, Task.Delay(ms)).ConfigureAwait(false);
        Assert.Same(task, done);
        return await task.ConfigureAwait(false);
    }

    private static async Task WaitUntilAsync(Func<bool> cond, int ms = 5000)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(ms);
        while (!cond())
        {
            if (DateTime.UtcNow >= deadline)
                throw new TimeoutException("Condition not met in time.");
            await Task.Delay(20).ConfigureAwait(false);
        }
    }

    [Fact]
    public async Task Run_ReportsProgress_And_SetsDoneText()
    {
        var vm = new AssetImportViewModel();
        vm.ImportCoreOverride = async (paths, root, ct, progress, resolve) =>
        {
            progress?.Report(new ImportProgress(1, 2, "a.png"));
            await Task.Delay(10, ct).ConfigureAwait(false);
            progress?.Report(new ImportProgress(2, 2, "b.png"));
            return new List<string> { "a.png", "b.png" };
        };

        var result = await WithTimeout(vm.RunAsync(new[] { "/tmp/a.png", "/tmp/b.png" }, "/tmp/assets", null));

        Assert.Equal(2, result.Count);
        Assert.Equal(2, vm.Completed);
        Assert.Equal(2, vm.Total);
        Assert.Equal("b.png", vm.CurrentFile);
        Assert.Equal(1.0, vm.ProgressFraction);
        Assert.Equal(string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Progress"), 2, 2), vm.ProgressText);
        Assert.Equal(string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Import.Result.Done"), 2), vm.LastResultText);
        Assert.False(vm.IsRunning);
    }

    [Fact]
    public async Task Cancel_SetsCancelledResult()
    {
        var vm = new AssetImportViewModel();
        vm.ImportCoreOverride = async (paths, root, ct, progress, resolve) =>
        {
            await Task.Delay(10000, ct).ConfigureAwait(false);
            return new List<string>();
        };

        var run = vm.RunAsync(new[] { "/tmp/a.png" }, "/tmp/assets", null);
        await WaitUntilAsync(() => vm.IsRunning);
        Assert.True(vm.CancelCommand.CanExecute(null));
        vm.CancelCommand.Execute(null);

        await WithTimeout(run);
        Assert.False(vm.IsRunning);
        Assert.Equal(EditorStrings.T("Import.Result.Cancelled"), vm.LastResultText);
        Assert.False(vm.CancelCommand.CanExecute(null));
    }

    [Fact]
    public async Task Conflict_KeepBoth_ReturnsChoiceToCore()
    {
        var vm = new AssetImportViewModel();
        ImportConflictDecision? captured = null;
        vm.ImportCoreOverride = async (paths, root, ct, progress, resolve) =>
        {
            var decision = await resolve(new ImportConflict("/tmp/a.png", "a.png"), ct).ConfigureAwait(false);
            captured = decision;
            return new List<string> { "a.png" };
        };

        var run = vm.RunAsync(new[] { "/tmp/a.png" }, "/tmp/assets", null);
        await WaitUntilAsync(() => vm.PendingConflict is not null);

        Assert.True(vm.HasPendingConflict);
        Assert.Equal("a.png", vm.PendingConflict!.DestinationRelative);
        Assert.Contains("a.png", vm.ConflictText);

        vm.KeepBothCommand.Execute(null);
        var result = await WithTimeout(run);

        Assert.NotNull(captured);
        Assert.Equal(ImportConflictChoice.KeepBoth, captured!.Choice);
        Assert.Single(result);
        Assert.Null(vm.PendingConflict);
        Assert.False(vm.IsRunning);
    }

    [Fact]
    public async Task SecondRun_WhileRunning_ReturnsEmpty()
    {
        var vm = new AssetImportViewModel();
        var gate = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        vm.ImportCoreOverride = async (paths, root, ct, progress, resolve) =>
        {
            try { await gate.Task.WaitAsync(ct).ConfigureAwait(false); }
            catch (OperationCanceledException) { throw; }
            return new List<string>();
        };

        var first = vm.RunAsync(new[] { "x" }, "root", null);
        await WaitUntilAsync(() => vm.IsRunning);

        var second = await WithTimeout(vm.RunAsync(new[] { "y" }, "root", null));
        Assert.Empty(second);

        vm.CancelCommand.Execute(null);
        gate.TrySetResult(true);
        await WithTimeout(first);
        Assert.False(vm.IsRunning);
    }
}
