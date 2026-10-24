using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.Views.Components;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D06-4 — the Inspector cards of the "condition" and "variable" components. Before this the template
/// selector had no view for them and the Inspector printed "Unknown Component", so an author could not
/// edit a condition (D06-1) or a variable from the editor.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorD064ConditionVariableCardTests
{
    private static IReadOnlyList<NodeViewModel> Nodes(params (ulong Id, string Title)[] nodes) =>
        nodes.Select(n => new NodeViewModel(n.Id, n.Title, 0, 0, bare: true)).ToList();

    // ── template selector ────────────────────────────────────────────────────

    [Fact]
    public void TemplateSelector_NeverFallsBackToUnknownComponent_ForAnyRegisteredType()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var selector = new ComponentTemplateSelector();
            var types = ComponentRegistry.AvailableTypes.Concat(new[] { "speaker", "dialogue_box" }).ToList();
            Assert.Contains("condition", types);
            Assert.Contains("variable", types);
            foreach (string type in types)
            {
                var vm = ComponentRegistry.Create(type);
                var control = selector.Build(vm);
                bool plainFallback = control is TextBlock text && text.Text == "Unknown Component";
                Assert.False(plainFallback, $"component type '{type}' ({vm.GetType().Name}) has no Inspector view");
            }
        });
    }

    [Fact]
    public void TemplateSelector_MapsConditionAndVariableToTheirCards()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var selector = new ComponentTemplateSelector();
            Assert.IsType<ConditionComponentView>(selector.Build(new ConditionComponentViewModel()));
            Assert.IsType<VariableComponentView>(selector.Build(new VariableComponentViewModel()));
        });
    }

    // ── condition card view model ────────────────────────────────────────────

    [Fact]
    public void Condition_TargetList_HasEmptyChoiceThenEveryNodeById()
    {
        var vm = new ConditionComponentViewModel { FailTargetNodeId = "" };
        vm.RefreshTargets(Nodes((7, "Gece"), (2, "Giriş"), (5, "")));
        var ids = vm.TargetOptions.Select(o => o.Id).ToArray();
        Assert.Equal(new[] { "", "2", "5", "7" }, ids);
        Assert.Contains("Giriş", vm.TargetOptions[1].Label);
        Assert.Equal("5", vm.TargetOptions[2].Label);
        Assert.Equal("", vm.SelectedTargetOption!.Id);
        Assert.False(vm.TargetMissing);
    }

    [Fact]
    public void Condition_PickingATarget_WritesTheIdAndPickingTheEmptyChoiceClearsIt()
    {
        var vm = new ConditionComponentViewModel { FailTargetNodeId = "" };
        vm.RefreshTargets(Nodes((2, "A"), (5, "B")));
        vm.SelectedTargetOption = vm.TargetOptions.First(o => o.Id == "5");
        Assert.Equal("5", vm.FailTargetNodeId);
        Assert.Equal("5", vm.Serialize()["fail_target_node_id"]);
        vm.SelectedTargetOption = vm.TargetOptions.First(o => o.Id == "");
        Assert.Equal("", vm.FailTargetNodeId);
    }

    [Fact]
    public void Condition_DeletedTarget_IsKeptFlaggedAndNotShownAsAnExistingNode()
    {
        var vm = new ConditionComponentViewModel { FailTargetNodeId = "99" };
        vm.RefreshTargets(Nodes((2, "A")));
        Assert.Equal("99", vm.FailTargetNodeId);
        Assert.True(vm.TargetMissing);
        var selected = vm.SelectedTargetOption;
        Assert.NotNull(selected);
        Assert.Equal("99", selected!.Id);
        Assert.False(selected.Exists);
        Assert.Contains("99", selected.Label);

        // Node 99 appears again (undo of a delete): the warning goes away, the value is the same.
        vm.RefreshTargets(Nodes((2, "A"), (99, "Geri geldi")));
        Assert.False(vm.TargetMissing);
        Assert.Equal("99", vm.FailTargetNodeId);
    }

    [Fact]
    public void Condition_ComboBoxNullWrite_DoesNotEraseTheTarget()
    {
        var vm = new ConditionComponentViewModel { FailTargetNodeId = "5" };
        vm.RefreshTargets(Nodes((2, "A"), (5, "B")));
        // A ComboBox reports null whenever its item list is replaced.
        vm.SelectedTargetOption = null;
        Assert.Equal("5", vm.FailTargetNodeId);
        vm.RefreshTargets(Nodes((2, "A"), (5, "B"), (8, "C")));
        vm.SelectedTargetOption = null;
        Assert.Equal("5", vm.FailTargetNodeId);
    }

    [Fact]
    public void Condition_WithoutAKnownNodeList_KeepsTheValueAndDoesNotWarn()
    {
        var vm = new ConditionComponentViewModel { FailTargetNodeId = "12" };
        Assert.False(vm.TargetMissing);
        Assert.Equal("12", vm.FailTargetNodeId);
        Assert.Single(vm.TargetOptions);
    }

    [Fact]
    public void Condition_RoundTrip_KeepsExpressionAndTarget()
    {
        var vm = new ConditionComponentViewModel { Expression = "gold >= 10 and key == 'x'", FailTargetNodeId = "4" };
        var copy = new ConditionComponentViewModel();
        copy.Deserialize(vm.Serialize().ToDictionary(p => p.Key, p => (object?)p.Value));
        Assert.Equal(vm.Expression, copy.Expression);
        Assert.Equal("4", copy.FailTargetNodeId);
        Assert.Equal(new[] { "expression", "fail_target_node_id" }, copy.Serialize().Keys.OrderBy(k => k).ToArray());
    }

    // ── variable card view model ─────────────────────────────────────────────

    [Fact]
    public void Variable_RoundTrip_KeepsKeyOperationAndValue_WithNativeKeys()
    {
        var vm = new VariableComponentViewModel { Key = "reputation", Operation = "add", Value = "-2.5" };
        var data = vm.Serialize();
        Assert.Equal(new[] { "key", "operation", "value" }, data.Keys.OrderBy(k => k).ToArray());
        Assert.Equal("-2.5", data["value"]);
        var copy = new VariableComponentViewModel();
        copy.Deserialize(data.ToDictionary(p => p.Key, p => (object?)p.Value));
        Assert.Equal(("reputation", "add", "-2.5"), (copy.Key, copy.Operation, copy.Value));
    }

    [Fact]
    public void Variable_OperationPicker_OffersExactlyWhatNativeUnderstands_AndKeepsUnknownOnes()
    {
        Assert.Equal(new[] { "set", "add" }, VariableComponentViewModel.OperationOptions.Select(o => o.Operation).ToArray());
        var vm = new VariableComponentViewModel { Operation = "multiply" }; // hand-edited file
        Assert.Null(vm.SelectedOperationOption);
        vm.SelectedOperationOption = null; // ComboBox null write
        Assert.Equal("multiply", vm.Operation);
        vm.SelectedOperationOption = VariableComponentViewModel.OperationOptions[1];
        Assert.Equal("add", vm.Operation);
    }

    [Fact]
    public void Variable_Warnings_FollowNativeRules()
    {
        var vm = new VariableComponentViewModel { Key = "gold", Operation = "set", Value = "abc" };
        Assert.False(vm.AddValueInvalid); // set takes any text
        vm.Operation = "add";
        Assert.True(vm.AddValueInvalid);  // native: stod fails
        vm.Value = " 3.5 ";
        Assert.False(vm.AddValueInvalid);
        vm.Value = "NaN";
        Assert.True(vm.AddValueInvalid);  // native rejects non-finite
        vm.Value = "";
        Assert.True(vm.AddValueInvalid);
        Assert.False(vm.KeyEmpty);
        vm.Key = "  ";
        Assert.True(vm.KeyEmpty);
    }

    // ── pixels ───────────────────────────────────────────────────────────────

    private static void Shot(Window window, string name)
    {
        string? dir = Environment.GetEnvironmentVariable("ROWL_EYE_OUT");
        if (string.IsNullOrWhiteSpace(dir)) return;
        RowlEyeRenderer.RenderWindow(window, 1.0, Path.Combine(dir, name + ".png"));
    }

    private static void Layout(Window window)
    {
        window.Show();
        window.Measure(new Size(window.Width, window.Height));
        window.Arrange(new Rect(0, 0, window.Width, window.Height));
        Dispatcher.UIThread.RunJobs();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
    }

    private static void AssertInside(Window window, string what)
    {
        var rootBounds = new Rect(0, 0, window.Bounds.Width, window.Bounds.Height);
        foreach (var control in window.GetVisualDescendants().OfType<Control>())
        {
            if (!control.IsVisible || control.Bounds.Width <= 0 || control.Bounds.Height <= 0) continue;
            if (control.FindAncestorOfType<ScrollViewer>() is not null && control is not ScrollViewer) continue;
            var origin = control.TranslatePoint(default, window) ?? default;
            var rect = new Rect(origin, control.Bounds.Size);
            Assert.True(rect.Right <= rootBounds.Right + 1.0 && rect.Bottom <= rootBounds.Bottom + 1.0 && rect.X >= -1.0,
                $"{what}: {control.GetType().Name} '{control.Name}' sticks out: {rect} vs {rootBounds}");
        }
    }

    private static T Named<T>(Window window, string name) where T : Control =>
        window.GetVisualDescendants().OfType<T>().First(c => c.Name == name);

    private static Window HostCard(Control card, double height)
    {
        var window = new Window { Width = 420, Height = height, CanResize = false, Content = new ScrollViewer { Content = card } };
        window.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
        return window;
    }

    [Fact]
    public void ConditionCard_IsAlignedAndNotClipped_AndMissingTargetShowsWarning()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new ConditionComponentViewModel { Expression = "gold >= 10", FailTargetNodeId = "5" };
            vm.RefreshTargets(Nodes((2, "Giriş"), (5, "Dükkân kapalı"), (9, "Son")));
            var window = HostCard(new ConditionComponentView { DataContext = vm }, 330);
            try
            {
                Layout(window);
                var expr = Named<TextBox>(window, "ExpressionBox");
                var picker = Named<ComboBox>(window, "TargetPicker");
                Assert.Equal("gold >= 10", expr.Text);
                Assert.Equal(expr.Bounds.Width, picker.Bounds.Width, 1.0);
                Assert.Equal(expr.TranslatePoint(default, window)!.Value.X, picker.TranslatePoint(default, window)!.Value.X, 1.0);
                Assert.True(picker.Bounds.Width > 250, "fields fill the card");
                Assert.Equal("5", ((ConditionTargetOption)picker.SelectedItem!).Id);
                AssertInside(window, "condition card");
                Shot(window, "condition_card_tr_dark");
            }
            finally { window.Close(); }

            var missing = new ConditionComponentViewModel { Expression = "gold >= 10", FailTargetNodeId = "99" };
            missing.RefreshTargets(Nodes((2, "Giriş")));
            var window2 = HostCard(new ConditionComponentView { DataContext = missing }, 380);
            try
            {
                Layout(window2);
                var picker = Named<ComboBox>(window2, "TargetPicker");
                Assert.Equal("99", ((ConditionTargetOption)picker.SelectedItem!).Id);
                Assert.Equal("99", missing.FailTargetNodeId);
                AssertInside(window2, "condition card (missing target)");
                Shot(window2, "condition_card_missing_tr_dark");
            }
            finally { window2.Close(); }
        });
    }

    [Fact]
    public void VariableCard_IsAlignedAndNotClipped()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new VariableComponentViewModel { Key = "gold", Operation = "add", Value = "10" };
            var window = HostCard(new VariableComponentView { DataContext = vm }, 360);
            try
            {
                Layout(window);
                var key = Named<TextBox>(window, "KeyBox");
                var op = Named<ComboBox>(window, "OperationPicker");
                var value = Named<TextBox>(window, "ValueBox");
                Assert.Equal("gold", key.Text);
                Assert.Equal("add", ((VariableOperationOption)op.SelectedItem!).Operation);
                Assert.Equal("10", value.Text);
                Assert.Equal(key.Bounds.Width, op.Bounds.Width, 1.0);
                Assert.Equal(key.Bounds.Width, value.Bounds.Width, 1.0);
                Assert.True(key.Bounds.Width > 250, "fields fill the card");
                AssertInside(window, "variable card");
                Shot(window, "variable_card_tr_dark");

                // Typing through the controls reaches the view model.
                key.Text = "reputation";
                value.Text = "-1";
                Assert.Equal(("reputation", "-1"), (vm.Key, vm.Value));
            }
            finally { window.Close(); }

            var bad = new VariableComponentViewModel { Key = "", Operation = "add", Value = "x" };
            var window2 = HostCard(new VariableComponentView { DataContext = bad }, 420);
            try
            {
                Layout(window2);
                AssertInside(window2, "variable card (warnings)");
                Shot(window2, "variable_card_warnings_tr_dark");
            }
            finally { window2.Close(); }
        });
    }
}
