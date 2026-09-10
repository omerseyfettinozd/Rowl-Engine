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
        if (issue?.NodeId is not ulong id) return;
        var node = System.Linq.Enumerable.FirstOrDefault(_main.Nodes, item => item.Id == id);
        if (node != null) _main.SelectNodeQuiet(node);
    }
}
