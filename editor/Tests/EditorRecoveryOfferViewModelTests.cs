using System;
using System.Globalization;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

public sealed class EditorRecoveryOfferViewModelTests
{
    private static RecoveryOfferViewModel Make(string source, string json, int nodes, string? error, Func<bool>? restore = null, Action? dismiss = null)
        => new(new RestoreOffer(source, json, 5, nodes), error, restore ?? (() => true), dismiss ?? (() => { }));

    [Fact]
    public void Titles_Parsed()
    {
        var vm = Make("journal", "{\"format_version\":5,\"nodes\":[{\"title\":\"Başla\"},{\"title\":\"Son\"}]}", 2, "bozuk");
        Assert.Equal(new[] { "Başla", "Son" }, vm.NodeTitles.ToArray());
        Assert.Equal(2, vm.NodeCount);
    }

    [Fact]
    public void Titles_InvalidJson_ReturnsEmpty()
    {
        var vm = Make("journal", "{bozuk", 0, null);
        Assert.Empty(vm.NodeTitles);
        Assert.Equal(EditorStrings.T("Recovery.Reason.Unknown"), vm.Reason);
    }

    [Fact]
    public void Restore_TwoStep_FirstCallDoesNotInvoke()
    {
        int calls = 0;
        var vm = Make("journal", "{\"nodes\":[]}", 0, "hata", () => { calls++; return true; });
        vm.RestoreCommand.Execute(null);
        Assert.True(vm.IsConfirmingRestore);
        Assert.Equal(0, calls);
        vm.RestoreCommand.Execute(null);
        Assert.Equal(1, calls);
        Assert.False(vm.IsConfirmingRestore);
        Assert.Null(vm.ErrorText);
    }

    [Fact]
    public void Restore_Failed_SetsErrorText()
    {
        var vm = Make("last-good", "{\"nodes\":[]}", 0, "hata", () => false);
        vm.RestoreCommand.Execute(null);
        vm.RestoreCommand.Execute(null);
        Assert.Equal(EditorStrings.T("Recovery.Failed"), vm.ErrorText);
        Assert.False(vm.IsConfirmingRestore);
    }

    [Fact]
    public void Dismiss_InvokesCallback_And_CancelRestore_Resets()
    {
        bool dismissed = false;
        var vm = Make("journal", "{\"nodes\":[]}", 0, null, dismiss: () => dismissed = true);
        vm.RestoreCommand.Execute(null);
        Assert.True(vm.IsConfirmingRestore);
        vm.CancelRestoreCommand.Execute(null);
        Assert.False(vm.IsConfirmingRestore);
        vm.ToggleInspectCommand.Execute(null);
        Assert.True(vm.IsInspecting);
        vm.ToggleInspectCommand.Execute(null);
        Assert.False(vm.IsInspecting);
        vm.DismissCommand.Execute(null);
        Assert.True(dismissed);
    }

    [Fact]
    public void SourceLabels_And_Summary()
    {
        var journal = Make("journal", "{\"nodes\":[]}", 3, null);
        Assert.Equal(EditorStrings.T("Recovery.Source.Journal"), journal.SourceLabel);
        Assert.Equal(string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Recovery.Summary"), journal.SourceLabel, 3), journal.Summary);

        var lastGood = Make("last-good", "{\"nodes\":[]}", 1, "disk hatası");
        Assert.Equal(EditorStrings.T("Recovery.Source.LastGood"), lastGood.SourceLabel);
        Assert.Equal("disk hatası", lastGood.Reason);
    }
}
