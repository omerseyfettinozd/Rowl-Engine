using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels;

public sealed partial class ProjectIssuesViewModel : ViewModelBase
{
    private readonly MainWindowViewModel _main;
    public ObservableCollection<ProjectValidationIssue> Issues { get; } = new();
    public ProjectIssuesViewModel(MainWindowViewModel main)
    {
        _main = main;
        // Faz 5: boş-durum rozeti koleksiyonla birlikte yaşar (SetIssues
        // Clear/Add üzerinden aynı olayı tetikler).
        Issues.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(HasIssues));
            OnPropertyChanged(nameof(IsEmpty));
        };
    }
    /// <summary>Faz 5: listede sorun var mı?</summary>
    public bool HasIssues => Issues.Count > 0;
    /// <summary>Faz 5: boş-durum paneli görünürlüğü.</summary>
    public bool IsEmpty => !HasIssues;
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
