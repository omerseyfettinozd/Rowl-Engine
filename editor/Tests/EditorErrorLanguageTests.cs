using System.Collections.ObjectModel;
using System.ComponentModel;
using RowlEngine.Editor.Services.Inspector;
using RowlEngine.Editor.Services.Localization;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 9 F1 — hata ve durum dili (B03): Denetçi silinmiş düğüm hedefini satır içi hata
/// olarak gösterir; Çeviri Masası satır durumu rozet bayraklarını canlı yayar.
/// </summary>
public sealed class EditorErrorLanguageTests
{
    private static NodeViewModel BareNode(ulong id, string title) => new(id, title, 0, 0, bare: true);

    private static (ObservableCollection<NodeViewModel> Nodes, InspectorValidationService Service, ChoiceOptionViewModel Option)
        ChoiceGraph(ulong target)
    {
        var nodes = new ObservableCollection<NodeViewModel> { BareNode(1, "A"), BareNode(2, "B") };
        var service = new InspectorValidationService();
        service.Attach(nodes, new ObservableCollection<ConnectionViewModel>(), null);
        var choice = nodes[0].CreateObject("C").AddComponent<ChoiceComponentViewModel>();
        choice.Options.Clear();
        var option = new ChoiceOptionViewModel { Text = "Git", TargetNodeId = target };
        choice.Options.Add(option);
        return (nodes, service, option);
    }

    [Fact]
    public void Inspector_ChoiceTargetDeletedNode_IsInlineError()
    {
        var (_, service, _) = ChoiceGraph(target: 99);
        var issues = service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget);
        var issue = Assert.Single(issues);
        Assert.True(issue.IsError);
        Assert.Contains("silinmiş", issue.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Inspector_ChoiceTargetExisting_NoIssue_AndZeroKeepsOldRule()
    {
        var (_, service, option) = ChoiceGraph(target: 2);
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget));

        option.TargetNodeId = 0;
        var issue = Assert.Single(service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget));
        Assert.Contains("hedef 0", issue.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Inspector_DeletingTargetNode_RaisesErrorOnSource()
    {
        var (nodes, service, _) = ChoiceGraph(target: 2);
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget));

        var removed = nodes[1];
        nodes.RemoveAt(1);
        var issue = Assert.Single(service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget));
        Assert.True(issue.IsError);

        // Silme geri alınınca (düğüm geri gelir) hata kalkar.
        nodes.Add(removed);
        Assert.Empty(service.IssuesFor(1, InspectorFieldDescriptor.Keys.ChoiceOptionTarget));
    }

    [Fact]
    public void TranslationRow_StateChange_RaisesBadgeFlags()
    {
        var row = new TranslationRow();
        Assert.True(row.IsMissing);
        var raised = new List<string?>();
        ((INotifyPropertyChanged)row).PropertyChanged += (_, e) => raised.Add(e.PropertyName);

        row.State = TranslationState.Changed;

        Assert.True(row.IsChanged);
        Assert.False(row.IsMissing);
        Assert.Contains(nameof(TranslationRow.IsChanged), raised);
        Assert.Contains(nameof(TranslationRow.IsMissing), raised);
        Assert.Contains(nameof(TranslationRow.State), raised);
    }
}
