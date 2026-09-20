using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Inspector;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.ViewModels
{
    public partial class InspectorViewModel : ViewModelBase
    {
        /// <summary>Label for "no chapter assigned" in the chapter dropdown.</summary>
        public const string NoChapterLabel = "Yok";

        public MainWindowViewModel MainViewModel { get; }

        /// <summary>
        /// Faz 4 Dilim 4 — reactive field-level validation shared by every
        /// inspector badge. Attached once (live collections survive project
        /// load — the service revalidates on Reset).
        /// </summary>
        public InspectorValidationService Validation { get; } = new();

        private NodeViewModel? _chapterWatchedNode;

        public InspectorViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            Validation.Attach(main.Nodes, main.Connections, () => MainWindowViewModel.AssetsPath);
            Validation.Changed += (_, _) =>
            {
                OnPropertyChanged(nameof(SelectedNodeIssues));
                OnPropertyChanged(nameof(SelectedNodeHasErrors));
            };
            main.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
                {
                    OnPropertyChanged(nameof(SelectedNode));
                    OnPropertyChanged(nameof(SelectedObject));
                    OnPropertyChanged(nameof(HasSelectedObject));
                    OnPropertyChanged(nameof(SelectedNodeIssues));
                    OnPropertyChanged(nameof(SelectedNodeHasErrors));
                    OnPropertyChanged(nameof(SelectedAssetProvenance));
                    OnPropertyChanged(nameof(HasSelectedAssetProvenance));
                    OnPropertyChanged(nameof(SelectedChapterOption));
                    OnPropertyChanged(nameof(MemberGroups));
                    OnPropertyChanged(nameof(HasNoMemberGroups));
                    OnPropertyChanged(nameof(JoinableGroups));
                    WatchChapterNode(SelectedNode);
                }
            };
            MainViewModel.Groups.Groups.CollectionChanged += OnGroupsStructureChanged;
            WatchChapterNode(SelectedNode);
        }

        public NodeViewModel? SelectedNode => MainViewModel.SelectedNode;

        /// <summary>
        /// The GameObject currently selected in the Hierarchy panel.
        /// The Inspector displays this GameObject's properties and attached components.
        /// </summary>
        public FrameObjectViewModel? SelectedObject => MainViewModel.HierarchyViewModel?.SelectedObject;

        public bool HasSelectedObject => SelectedObject != null;

        /// <summary>
        /// Called by HierarchyViewModel when selected GameObject changes.
        /// </summary>
        public void NotifySelectedObjectChanged()
        {
            OnPropertyChanged(nameof(SelectedObject));
            OnPropertyChanged(nameof(HasSelectedObject));
        }

        // ── Faz 4 Dilim 4 — inline issues for the selected node ──

        public IReadOnlyList<ProjectValidationIssue> SelectedNodeIssues =>
            SelectedNode is null
                ? Array.Empty<ProjectValidationIssue>()
                : Validation.IssuesFor(SelectedNode.Id);

        public bool SelectedNodeHasErrors =>
            SelectedNodeIssues.Any(issue => issue.IsError);

        // ── Faz 5 Dilim 5 — dönüştürülmüş asset provenance (salt-okunur) ──

        /// <summary>
        /// Yalnızca testler için native enjeksiyonu (prod'da null; prod ucu
        /// <see cref="ActiveProvenanceEndpoint"/> ile engine'den çözülür).
        /// </summary>
        internal AssetProvenanceService.ProvenanceNativeCall? TestProvenanceNativeCall { get; set; }

        internal IntPtr TestProvenanceEngineHandle { get; set; }

        /// <summary>
        /// Seçili düğümün dönüştürülmüş asset referansları için sidecar'dan
        /// okunan salt-okunur provenance satırları (dönüştürücü adı/sürümü +
        /// kısa kaynak/çıktı hash'leri). Okuma sırası native-önce/disk-sonra:
        /// engine canlıysa gerçek DllImport delegesi verilir, native başarısızsa
        /// disk sidecar'a düşülür (davranış değişmez, yalnızca ölü yol canlanır).
        /// Dönüştürülmemiş referanslar sessiz geçilir; boş liste = gösterilecek
        /// provenance yok.
        /// </summary>
        public IReadOnlyList<string> SelectedAssetProvenance
        {
            get
            {
                if (SelectedNode is null)
                    return Array.Empty<string>();
                var (nativeCall, engineHandle) = ActiveProvenanceEndpoint();
                return AssetProvenanceService.FormatSelectedNodeProvenance(
                    SelectedNode, MainWindowViewModel.AssetsPath, nativeCall, engineHandle);
            }
        }

        /// <summary>
        /// Rozet görünürlüğü: sidecar yoksa false (fail-closed gizli rozet).
        /// </summary>
        public bool HasSelectedAssetProvenance => SelectedAssetProvenance.Count > 0;

        /// <summary>
        /// Prod native ucu: test enjeksiyonu varsa o, yoksa canlı engine
        /// (handle + <see cref="AssetProvenanceService.ProductionNativeCall"/>),
        /// engine ölüyse disk-only (null delege). <c>EngineHost</c>'a dokunmaz
        /// (salt-okunur <c>IsInitialized</c>/<c>Handle</c> okuması).
        /// </summary>
        internal (AssetProvenanceService.ProvenanceNativeCall? Call, IntPtr Handle) ActiveProvenanceEndpoint()
        {
            if (TestProvenanceNativeCall is not null)
                return (TestProvenanceNativeCall, TestProvenanceEngineHandle);
            var host = MainViewModel.EngineHost;
            if (host.IsInitialized && host.Handle != IntPtr.Zero)
                return (AssetProvenanceService.ProductionNativeCall, host.Handle);
            return (null, IntPtr.Zero);
        }

        // ── Faz 4 Dilim 4 — chapter assignment ──

        /// <summary>Chapter dropdown options ("Yok" + effective chapters).</summary>
        public List<string> ChapterAssignmentOptions
        {
            get
            {
                var options = new List<string> { NoChapterLabel };
                foreach (var chapter in MainViewModel.Chapters.EffectiveChapters(
                    MainViewModel.Nodes.Select(n => n.ChapterId)))
                {
                    string label = chapter.Title.Length > 0 ? chapter.Title : chapter.Id;
                    if (!options.Contains(label))
                        options.Add(label);
                }
                return options;
            }
        }

        /// <summary>Selected chapter option ("Yok" = unassigned). Two-way.</summary>
        public string SelectedChapterOption
        {
            get
            {
                string current = SelectedNode?.ChapterId ?? string.Empty;
                if (string.IsNullOrEmpty(current))
                    return NoChapterLabel;
                var match = MainViewModel.Chapters.Definitions.FirstOrDefault(c =>
                    string.Equals(c.Id, current, StringComparison.Ordinal));
                return match is null ? current : (match.Title.Length > 0 ? match.Title : match.Id);
            }
            set
            {
                if (SelectedNode is null)
                    return;
                if (string.Equals(value, NoChapterLabel, StringComparison.Ordinal) ||
                    string.IsNullOrEmpty(value))
                {
                    SelectedNode.ChapterId = string.Empty;
                }
                else
                {
                    var match = MainViewModel.Chapters.Definitions.FirstOrDefault(c =>
                        string.Equals(c.Id, value, StringComparison.Ordinal) ||
                        string.Equals(c.Title, value, StringComparison.Ordinal));
                    SelectedNode.ChapterId = match?.Id ?? value;
                }
                RefreshChapterOptions();
                OnPropertyChanged(nameof(SelectedChapterOption));
            }
        }

        /// <summary>
        /// Rebuilds the canvas chapter-filter options after assignments change
        /// (e.g. the implicit default chapter appears/vanishes).
        /// </summary>
        public void RefreshChapterOptions()
        {
            MainViewModel.Subgraphs.SetAvailableChapters(
                MainViewModel.Chapters.EffectiveChapters(
                    MainViewModel.Nodes.Select(n => n.ChapterId)));
            OnPropertyChanged(nameof(ChapterAssignmentOptions));
            OnPropertyChanged(nameof(SelectedChapterOption));
        }

        private void WatchChapterNode(NodeViewModel? node)
        {
            if (_chapterWatchedNode is not null)
                _chapterWatchedNode.PropertyChanged -= OnWatchedChapterChanged;
            _chapterWatchedNode = node;
            if (_chapterWatchedNode is not null)
                _chapterWatchedNode.PropertyChanged += OnWatchedChapterChanged;
        }

        private void OnWatchedChapterChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(NodeViewModel.ChapterId))
                RefreshChapterOptions();
        }

        // ── Faz 4 Dilim 4 — group assignment ──

        /// <summary>Groups the selected node belongs to (chips with ×).</summary>
        public IReadOnlyList<CanvasGroupViewModel> MemberGroups =>
            SelectedNode is null
                ? Array.Empty<CanvasGroupViewModel>()
                : MainViewModel.Groups.GroupsOf(SelectedNode.Id);

        /// <summary>True when the selected node belongs to no group (drives the empty-state hint).</summary>
        public bool HasNoMemberGroups => MemberGroups.Count == 0;

        /// <summary>Groups the selected node can still join.</summary>
        public IReadOnlyList<CanvasGroupViewModel> JoinableGroups
        {
            get
            {
                if (SelectedNode is null)
                    return Array.Empty<CanvasGroupViewModel>();
                var member = new HashSet<string>(
                    MainViewModel.Groups.GroupsOf(SelectedNode.Id).Select(g => g.GroupId),
                    StringComparer.Ordinal);
                return MainViewModel.Groups.Groups
                    .Where(g => !member.Contains(g.GroupId))
                    .ToList();
            }
        }

        /// <summary>Join-target picked in the "add to group" dropdown.</summary>
        [ObservableProperty]
        private CanvasGroupViewModel? _selectedJoinGroup;

        /// <summary>Joins the selected node to <see cref="SelectedJoinGroup"/>.</summary>
        [RelayCommand]
        public void JoinSelectedJoinGroup()
        {
            if (SelectedJoinGroup is not null)
            {
                JoinSelectedNodeToGroup(SelectedJoinGroup.GroupId);
                SelectedJoinGroup = null;
            }
        }

        [RelayCommand]
        public void JoinSelectedNodeToGroup(string? groupId)
        {
            if (SelectedNode is null || string.IsNullOrEmpty(groupId))
                return;
            var group = MainViewModel.Groups.Find(groupId);
            if (group is null || group.MemberNodeIds.Contains(SelectedNode.Id))
                return;
            group.MemberNodeIds.Add(SelectedNode.Id);
            RaiseGroupProps();
        }

        [RelayCommand]
        public void RemoveSelectedNodeFromGroup(string? groupId)
        {
            if (SelectedNode is null || string.IsNullOrEmpty(groupId))
                return;
            var group = MainViewModel.Groups.Find(groupId);
            if (group is null)
                return;
            group.MemberNodeIds.Remove(SelectedNode.Id);
            RaiseGroupProps();
        }

        private void OnGroupsStructureChanged(object? sender, NotifyCollectionChangedEventArgs e) =>
            RaiseGroupProps();

        private void RaiseGroupProps()
        {
            OnPropertyChanged(nameof(MemberGroups));
            OnPropertyChanged(nameof(HasNoMemberGroups));
            OnPropertyChanged(nameof(JoinableGroups));
        }

        // ── Faz 4 Dilim 4 — content_id maintenance ──

        /// <summary>Assigns a fresh content_id to the first enabled dialogue.</summary>
        [RelayCommand]
        public void RegenerateSelectedContentId()
        {
            var dialogue = SelectedNode?.AllComponents
                .OfType<DialogueComponentViewModel>()
                .FirstOrDefault(d => d.IsEnabled)
                ?? SelectedObject?.Components
                    .OfType<DialogueComponentViewModel>()
                    .FirstOrDefault(d => d.IsEnabled);
            if (dialogue is null)
                return;
            dialogue.ContentId = ContentIdService.NewContentId();
        }
    }
}
