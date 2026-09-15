using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels;

public sealed partial class ProjectIssuesViewModel : ViewModelBase
{
    private readonly MainWindowViewModel _main;
    public ObservableCollection<ProjectValidationIssue> Issues { get; } = new();
    public ProjectIssuesViewModel(MainWindowViewModel main) => _main = main;
    public void SetIssues(System.Collections.Generic.IEnumerable<ProjectValidationIssue> issues)
    { Issues.Clear(); foreach (var issue in issues) Issues.Add(issue); }
    [RelayCommand]
    private void Focus(ProjectValidationIssue? issue)
    {
        // Faz 4 Dilim 5 — deep-nav lives in MainWindowViewModel (scope sync
        // + pan + amber highlight); null/global issues are a silent no-op.
        _main.FocusIssueNode(issue?.NodeId);
    }
}
