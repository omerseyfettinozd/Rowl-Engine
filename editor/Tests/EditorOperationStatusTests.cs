using System;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

// Kaydet / Farklı kaydet / Dışa aktar sonucu dili.
public sealed class EditorOperationStatusTests
{
    [Fact]
    public void Begin_SetsRunning_AndVisible()
    {
        var vm = new OperationStatusViewModel();
        Assert.Equal(OperationState.Idle, vm.State);
        Assert.False(vm.IsVisible);

        vm.Begin(OperationKind.Save);
        Assert.Equal(OperationState.Running, vm.State);
        Assert.True(vm.IsVisible);
        Assert.True(vm.IsRunning);
        Assert.False(vm.IsSucceeded);
        Assert.False(vm.IsFailed);
    }

    [Fact]
    public void Succeed_SetsOutputPath_AndDetail()
    {
        var vm = new OperationStatusViewModel();
        vm.Begin(OperationKind.Export);
        vm.Succeed(OperationKind.Export, outputPath: "/tmp/out");
        Assert.Equal(OperationState.Succeeded, vm.State);
        Assert.True(vm.IsSucceeded);
        Assert.True(vm.HasOutputPath);
        Assert.Equal("/tmp/out", vm.OutputPath);
        Assert.Equal("/tmp/out", vm.Detail);
        Assert.True(vm.OpenFolderCommand.CanExecute(null));
    }

    [Fact]
    public void Fail_ClearsOutputPath_AndSetsReason()
    {
        var vm = new OperationStatusViewModel();
        vm.Begin(OperationKind.Save);
        vm.Succeed(OperationKind.Save, outputPath: "/tmp/out");
        vm.Fail(OperationKind.Save, "disk dolu");
        Assert.Equal(OperationState.Failed, vm.State);
        Assert.True(vm.IsFailed);
        Assert.Null(vm.OutputPath);
        Assert.False(vm.HasOutputPath);
        Assert.Equal("disk dolu", vm.Detail);
    }

    [Fact]
    public void OldAttempt_Succeed_DoesNotOverwrite_Fail()
    {
        var vm = new OperationStatusViewModel();
        vm.Begin(OperationKind.Save);
        int first = vm.Attempt;
        vm.Begin(OperationKind.Save);
        int second = vm.Attempt;
        Assert.NotEqual(first, second);

        vm.Fail(OperationKind.Save, "yeni hata", attempt: second);
        Assert.Equal(OperationState.Failed, vm.State);

        vm.Succeed(OperationKind.Save, outputPath: "/tmp/eski", attempt: first);
        Assert.Equal(OperationState.Failed, vm.State);
        Assert.Null(vm.OutputPath);
        Assert.Equal("yeni hata", vm.Detail);
    }

    [Fact]
    public void Cancelled_GoesIdle()
    {
        var vm = new OperationStatusViewModel();
        vm.Begin(OperationKind.SaveAs);
        vm.Cancelled(OperationKind.SaveAs);
        Assert.Equal(OperationState.Idle, vm.State);
        Assert.False(vm.IsVisible);
    }

    [Fact]
    public void OpenFolder_RaisesEvent_WithPath()
    {
        var vm = new OperationStatusViewModel();
        vm.Begin(OperationKind.Export);
        vm.Succeed(OperationKind.Export, outputPath: "/tmp/out");
        string? got = null;
        vm.OpenFolderRequested += p => got = p;
        vm.OpenFolderCommand.Execute(null);
        Assert.Equal("/tmp/out", got);
    }

    [Fact]
    public void ShowLog_OnlyWhenFailed()
    {
        var vm = new OperationStatusViewModel();
        int calls = 0;
        vm.ShowLogRequested += () => calls++;

        vm.Begin(OperationKind.Save);
        Assert.False(vm.ShowLogCommand.CanExecute(null));

        vm.Fail(OperationKind.Save, "hata");
        Assert.True(vm.ShowLogCommand.CanExecute(null));
        vm.ShowLogCommand.Execute(null);
        Assert.Equal(1, calls);
    }
}
