using System;
using System.Collections.ObjectModel;
using System.IO;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C04 — kayıt hatasının nedeni (disk dolu / izin / salt okunur) kullanıcıya ulaşır;
/// devralınan (superseded) kayıt sahte "kayıt başarısız" göstermez.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorSaveFailureCauseTests : IDisposable
{
    private readonly string _tmp = Path.Combine(Path.GetTempPath(), "RowlSaveCause_" + Guid.NewGuid().ToString("N"));

    public EditorSaveFailureCauseTests() => RowlEyeHeadlessRuntime.EnsureInitialized();

    public void Dispose()
    {
        try { File.SetUnixFileMode(_tmp, (UnixFileMode)0b111_101_101); } catch (Exception) { }
        try { Directory.Delete(_tmp, true); } catch (Exception) { }
    }

    private static StoryGraphSaveSnapshot Snap()
    {
        var nodes = new ObservableCollection<NodeViewModel> { new(101, "n", 0, 0, bare: true) };
        return StoryGraphSaveService.Capture(nodes, new ObservableCollection<ConnectionViewModel>(), 101, nodes[0]);
    }

    [Theory]
    [InlineData(unchecked((int)0x80070070), SaveFailureClassifier.Cause.DiskFull)]   // Win ERROR_DISK_FULL
    [InlineData(unchecked((int)0x80070027), SaveFailureClassifier.Cause.DiskFull)]   // Win ERROR_HANDLE_DISK_FULL
    [InlineData(28, SaveFailureClassifier.Cause.DiskFull)]                            // ENOSPC
    [InlineData(122, SaveFailureClassifier.Cause.DiskFull)]                           // EDQUOT
    [InlineData(30, SaveFailureClassifier.Cause.ReadOnly)]                            // EROFS
    [InlineData(unchecked((int)0x80070013), SaveFailureClassifier.Cause.ReadOnly)]   // Win write protect
    [InlineData(5, SaveFailureClassifier.Cause.Unknown)]
    public void Classify_IOException_ByHResult(int hresult, SaveFailureClassifier.Cause expected)
        => Assert.Equal(expected, SaveFailureClassifier.Classify(new IOException("x", hresult)));

    [Fact]
    public void Classify_UnauthorizedAndWrappedInner()
    {
        Assert.Equal(SaveFailureClassifier.Cause.PermissionDenied,
            SaveFailureClassifier.Classify(new UnauthorizedAccessException()));
        Assert.Equal(SaveFailureClassifier.Cause.DiskFull,
            SaveFailureClassifier.Classify(new InvalidOperationException("w", new IOException("x", 28))));
        Assert.Equal(SaveFailureClassifier.Cause.Unknown, SaveFailureClassifier.Classify(new Exception("?")));
        Assert.Null(SaveFailureClassifier.Describe(new Exception("?")));
    }

    [Fact]
    public void Describe_UsesSpecificLocalizedText_NotTheGenericFallback()
    {
        string generic = EditorStrings.T("Operation.Save.WriteFailed");
        foreach (var ex in new Exception[] { new IOException("x", 28), new UnauthorizedAccessException(), new IOException("x", 30) })
        {
            string? text = SaveFailureClassifier.Describe(ex);
            Assert.False(string.IsNullOrWhiteSpace(text));
            Assert.NotEqual(generic, text);
            Assert.DoesNotContain("Operation.Save.", text!);
        }
    }

    [Fact]
    public void Superseded_IsNotFailure_AndStaleNeverWrites()
    {
        string jsonDir = Path.Combine(_tmp, "Assets", "json");
        var result = StoryGraphSaveService.TryWriteSnapshotDetailed(Snap(), jsonDir, 1, () => 2);
        Assert.Equal(SaveWriteStatus.Superseded, result.Status);
        Assert.False(File.Exists(Path.Combine(jsonDir, "full_story_graph.json")));
        Assert.False(StoryGraphSaveService.TryWriteSnapshot(Snap(), jsonDir, 1, () => 2)); // bool sözleşmesi aynı
    }

    [Fact]
    public void UnwritableDir_ReturnsFailedWithPermissionCause()
    {
        if (OperatingSystem.IsWindows() || Environment.UserName == "root") return;
        string jsonDir = Path.Combine(_tmp, "Assets", "json");
        Directory.CreateDirectory(jsonDir);
        File.SetUnixFileMode(jsonDir, UnixFileMode.UserRead | UnixFileMode.UserExecute);
        try
        {
            var r = StoryGraphSaveService.TryWriteSnapshotDetailed(Snap(), jsonDir, 1, () => 1);
            Assert.Equal(SaveWriteStatus.Failed, r.Status);
            Assert.Equal(SaveFailureClassifier.Cause.PermissionDenied, SaveFailureClassifier.Classify(r.Error));
            Assert.Equal(SaveFailureClassifier.Describe(r.Error), SaveFailureClassifier.ReasonFor(r));
        }
        finally { File.SetUnixFileMode(jsonDir, (UnixFileMode)0b111_101_101); }
    }

    [Fact]
    public void ReasonFor_BlockedUsesMessage_SupersededAndUnknownAreEmpty()
    {
        Assert.Equal("m", SaveFailureClassifier.ReasonFor(new SaveWriteResult(SaveWriteStatus.Blocked, null, "m")));
        Assert.Equal(string.Empty, SaveFailureClassifier.ReasonFor(new SaveWriteResult(SaveWriteStatus.Failed, new Exception("?"))));
    }

    [Fact]
    public void ViewModel_UnwritableSave_ShowsPermissionReason()
    {
        if (OperatingSystem.IsWindows() || Environment.UserName == "root") return;
        string previous = MainWindowViewModel.ProjectRoot;
        Directory.CreateDirectory(_tmp);
        var created = ProjectFactory.CreateNewProject("CauseTest", _tmp);
        Assert.True(created.Success);
        string root = created.Info!.Path;
        var vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(root, connectEngine: false));
        string jsonDir = MainWindowViewModel.AssetsJsonPath;
        try
        {
            Dispatcher.UIThread.Invoke(() =>
            {
                File.SetUnixFileMode(jsonDir, UnixFileMode.UserRead | UnixFileMode.UserExecute);
                try { Assert.False(vm.SaveProjectNow()); }
                finally { File.SetUnixFileMode(jsonDir, (UnixFileMode)0b111_101_101); }
                Assert.True(vm.OperationStatus.IsFailed);
                Assert.Equal(EditorStrings.T("Operation.Save.PermissionDenied"), vm.OperationStatus.Detail);
            });
        }
        finally
        {
            try { Dispatcher.UIThread.Invoke(() => vm.Dispose()); } catch (Exception) { }
            try { MainWindowViewModel.ProjectRoot = previous; } catch (Exception) { }
        }
    }
}
