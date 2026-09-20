using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Localization;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.ViewModels.Components;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using System.Threading;

namespace RowlEngine.Editor.ViewModels
{
    public partial class MainWindowViewModel : ViewModelBase, IDisposable
    {
        private bool _disposed;
        // Centralized path helpers
        /// <summary>
        /// Resolves the real project root (where Assets/ and editor/ live) by
        /// walking up from the executing assembly location until we find a
        /// directory containing "Assets" or "*.csproj". This fixes the classic
        /// bin/Debug/net10.0 → project root resolution problem.
        /// </summary>
        public static string ProjectRoot { get; set; } = ResolveProjectRoot();

        private static string ResolveProjectRoot()
        {
            // Start from the directory of the executing assembly (bin/Debug/netX.Y)
            string assemblyDirectory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location) ?? ".";
            return ResolveProjectRootFrom(assemblyDirectory);
        }

        internal static string ResolveProjectRootFrom(string startDirectory)
            => ProjectFileSystem.ResolveProjectRootFrom(startDirectory);

        /// <summary>
        /// Returns the assets directory (ProjectRoot/Assets).
        /// </summary>
        public static string AssetsPath => Path.Combine(ProjectRoot, "Assets");

        /// <summary>
        /// Returns the assets/json subdirectory.
        /// </summary>
        public static string AssetsJsonPath => Path.Combine(AssetsPath, "json");

        /// <summary>
        /// Returns the assets/images subdirectory.
        /// </summary>
        public static string AssetsImagesPath => Path.Combine(AssetsPath, "images");

        /// <summary>
        /// Returns the assets/packages subdirectory.
        /// </summary>
        public static string AssetsPackagesPath => Path.Combine(AssetsPath, "packages");

        /// <summary>
        /// The embedded C++ engine host. Exposed publicly so EnginePreviewControl
        /// can register the native surface handle before initialization.
        /// </summary>
        public EngineHost EngineHost { get; } = new EngineHost();

        [ObservableProperty]
        private string _statusText = "Ready — Engine initializing...";

        public string CurrentProjectPath { get; set; } = "";
        public object? TopLevelHint { get; set; }

        public SettingsViewModel Settings { get; } = new();
        internal ProjectRuntimeSettings ProjectRuntimeSettings { get; private set; } = new();
        private readonly string _playerSettingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "RowlEngine", "player-settings.json");
        private readonly string _editorSettingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "RowlEngine", "editor-settings.json");
        [ObservableProperty] private bool _isProjectDirty;
        public ToastService Toast => ToastService.Instance;
        public UndoRedoService UndoRedo => UndoRedoService.Instance;

        [ObservableProperty] private bool _isBuilding;
        [ObservableProperty] private string _buildProgress = "";
        private CancellationTokenSource? _buildCancellation;

        public string CurrentBuildTarget => OperatingSystem.IsWindows() ? "Windows"
            : OperatingSystem.IsMacOS() ? "macOS"
            : "Linux";
        public string BuildButtonText => $"{CurrentBuildTarget} Build";
        public string BuildButtonTooltip => $"Bu host için bağımsız oyun çıktısı üret (Ctrl+B)";

        [RelayCommand]
        private async Task OpenSettings()
        {
            await EditorModalDialogCoordinator.OpenSettingsAsync(Settings, TopLevelHint as Window);
        }

        [RelayCommand]
        public async Task OpenProjectHubAsync()
        {
            await EditorModalDialogCoordinator.OpenProjectHubAsync(
                TopLevelHint as Window,
                () => TopLevelHint is Window curWin ? ResolveUnsavedChangesAsync(curWin) : Task.FromResult(true));
        }

        [RelayCommand]
        public void Undo()
        {
            if (UndoRedo.CanUndo)
            {
                UndoRedo.Undo();
                Toast.Show($"Geri Al: {UndoRedo.UndoDescription}", ToastType.Info, 1500);
            }
        }

        [RelayCommand]
        public void Redo()
        {
            if (UndoRedo.CanRedo)
            {
                UndoRedo.Redo();
                Toast.Show($"Yinele: {UndoRedo.RedoDescription}", ToastType.Info, 1500);
            }
        }

        // Hızlı Arama (Quick Search, Faz 4 Dilim 2)
        // The box text lives here for XAML/shortcut compatibility; every
        // query, filter, result and jump decision lives in SearchViewModel.
        [ObservableProperty]
        private bool _isSearchVisible = false;

        [ObservableProperty]
        private string _searchQuery = "";

        partial void OnSearchQueryChanged(string value)
        {
            Search?.SetQuery(value);
        }

        [RelayCommand]
        private void ToggleSearch()
        {
            IsSearchVisible = !IsSearchVisible;
            if (!IsSearchVisible) SearchQuery = "";
            else Search?.Refresh();
        }

        // Tam Ekran
        [RelayCommand]
        private void ToggleFullscreen()
        {
            if (TopLevelHint is Window win)
            {
                win.WindowState = win.WindowState == WindowState.FullScreen
                    ? WindowState.Normal
                    : WindowState.FullScreen;
            }
        }



        [ObservableProperty]
        private bool _isConnected = false;

        [ObservableProperty]
        private string _logOutput = "[System] Rowl Engine Editor initialized.\n";

        public ObservableCollection<NodeViewModel> SelectedNodes { get; } = new();

        public EditorNotificationService NotificationService { get; } = new();

        [ObservableProperty]
        private NodeViewModel? _selectedNode;

        partial void OnSelectedNodeChanged(NodeViewModel? value)
        {
            if (value != null && EngineHost.IsInitialized)
            {
                PushSceneToEngine(value);
            }
        }

        [ObservableProperty]
        private Point _wireStartPoint = new Point(0, 0);

        [ObservableProperty]
        private Point _wireEndPoint = new Point(0, 0);

        [ObservableProperty]
        private bool _isDraggingWire = false;

        private string _wireDragOptionId = string.Empty;

        [ObservableProperty]
        private double _selectionBoxX = 0;

        [ObservableProperty]
        private double _selectionBoxY = 0;

        [ObservableProperty]
        private double _selectionBoxWidth = 0;

        [ObservableProperty]
        private double _selectionBoxHeight = 0;

        [ObservableProperty]
        private bool _isSelectingBox = false;

        [ObservableProperty]
        private double _panX = 0;

        [ObservableProperty]
        private double _panY = 0;

        [ObservableProperty]
        private double _zoomScale = 1.0;

        [ObservableProperty]
        private bool _isInteractivelyDragging = false;

        public double TargetPanX { get; set; } = 0;
        public double TargetPanY { get; set; } = 0;
        public double TargetZoom { get; set; } = 1.0;

        // Panel visibility (menu toggles). Lightweight, deterministic, mobile-friendly — no floating windows.
        [ObservableProperty]
        private bool _isAssetsPanelVisible = true;

        [ObservableProperty]
        private bool _isInspectorPanelVisible = true;

        [ObservableProperty]
        private bool _isLogPanelVisible = true;

        [ObservableProperty]
        private bool _isBacklogPanelVisible = false;

        [ObservableProperty]
        private bool _isSaveSlotsPanelVisible = false;

        [ObservableProperty]
        private bool _isProjectIssuesPanelVisible = false;

        [ObservableProperty]
        private bool _isHierarchyPanelVisible = true;

        /// <summary>
        /// Faz 6 Dilim 2: alt sekme alanı yalnızca Günlük/Diyalog Geçmişi/
        /// Kayıt Slotları/Sorunlar sekmelerini kapsar. Varlıklar kendi
        /// şeridinde yaşar (<see cref="IsAssetsPanelVisible"/>) ve sekme
        /// alanını açık tutmaz.
        /// </summary>
        public bool IsBottomPanelVisible => IsLogPanelVisible || IsBacklogPanelVisible || IsSaveSlotsPanelVisible || IsProjectIssuesPanelVisible;
        public GridLength BottomPanelHeight => EditorWorkspaceLayoutService.CalculateBottomPanelHeight(IsBottomPanelVisible);
        public GridLength BottomSplitterHeight => EditorWorkspaceLayoutService.CalculateBottomSplitterHeight(IsBottomPanelVisible);
        /// <summary>
        /// Faz 6 Dilim 2: tuvalin altındaki bağımsız Varlıklar şeridi.
        /// Sekme alanından ayrı açılıp kapanır, kendi yüksekliğini korur.
        /// </summary>
        public GridLength AssetsStripHeight => EditorWorkspaceLayoutService.CalculateBottomPanelHeight(IsAssetsPanelVisible, _assetsStripHeightPixels);
        public GridLength AssetsStripSplitterHeight => EditorWorkspaceLayoutService.CalculateBottomSplitterHeight(IsAssetsPanelVisible);
        public GridLength HierarchyPanelWidth => EditorWorkspaceLayoutService.CalculateHierarchyPanelWidth(IsHierarchyPanelVisible);
        public GridLength HierarchySplitterWidth => EditorWorkspaceLayoutService.CalculateHierarchySplitterWidth(IsHierarchyPanelVisible);
        public GridLength InspectorPanelWidth => EditorWorkspaceLayoutService.CalculateInspectorPanelWidth(IsInspectorPanelVisible);
        public GridLength InspectorSplitterWidth => EditorWorkspaceLayoutService.CalculateInspectorSplitterWidth(IsInspectorPanelVisible);

        partial void OnIsAssetsPanelVisibleChanged(bool value) =>
            NotifyBottomPanelLayoutChanged();

        partial void OnAssetsStripHeightPixelsChanged(double value) =>
            OnPropertyChanged(nameof(AssetsStripHeight));
        partial void OnIsLogPanelVisibleChanged(bool value) =>
            NotifyBottomPanelLayoutChanged();

        partial void OnIsBacklogPanelVisibleChanged(bool value) =>
            NotifyBottomPanelLayoutChanged();

        partial void OnIsSaveSlotsPanelVisibleChanged(bool value) =>
            NotifyBottomPanelLayoutChanged();

        partial void OnIsProjectIssuesPanelVisibleChanged(bool value) =>
            NotifyBottomPanelLayoutChanged();

        private void NotifyBottomPanelLayoutChanged()
        {
            OnPropertyChanged(nameof(IsBottomPanelVisible));
            OnPropertyChanged(nameof(BottomPanelHeight));
            OnPropertyChanged(nameof(BottomSplitterHeight));
            OnPropertyChanged(nameof(AssetsStripHeight));
            OnPropertyChanged(nameof(AssetsStripSplitterHeight));
        }

        partial void OnIsHierarchyPanelVisibleChanged(bool value)
        {
            OnPropertyChanged(nameof(HierarchyPanelWidth));
            OnPropertyChanged(nameof(HierarchySplitterWidth));
        }

        partial void OnIsInspectorPanelVisibleChanged(bool value)
        {
            OnPropertyChanged(nameof(InspectorPanelWidth));
            OnPropertyChanged(nameof(InspectorSplitterWidth));
        }

        /// <summary>
        /// Active tab index in the bottom panel: 0 = Log, 1 = Backlog,
        /// 2 = SaveSlots, 3 = ProjectIssues (Faz 6 Dilim 2: Varlıklar
        /// sekmeden ayrı şeride taşındı).
        /// </summary>
        [ObservableProperty]
        private int _bottomPanelActiveTab = 0;

        /// <summary>
        /// Faz 6 Dilim 2: Varlıklar şeridinin piksel yüksekliği (varsayılan 180).
        /// </summary>
        [ObservableProperty]
        private double _assetsStripHeightPixels = 180;

        // Center view: single active tab (radio semantics). Node Graph is default.
        [ObservableProperty]
        private bool _isNodeGraphActive = true;

        [ObservableProperty]
        private bool _isPreviewActive = false;

        [ObservableProperty]
        private bool _isEnginePreviewActive = false;

        // Split-screen mode: both Node Graph + Live Preview visible side-by-side.
        // Toggled via toolbar button; when off, center area goes back to radio toggle.
        [ObservableProperty]
        private int _splitScreenMode = 0; // 0: Off, 1: Horizontal, 2: Vertical

        public bool IsSplitScreenOff => SplitScreenMode == 0;
        public bool IsSplitScreenHorizontal => SplitScreenMode == 1;
        public bool IsSplitScreenVertical => SplitScreenMode == 2;

        public string SplitScreenButtonText => SplitScreenMode > 0 ? $"Bölünmüş: {(SplitScreenMode == 1 ? "H" : "V")}" : "Bölünmüş Ekran";
        public string SplitScreenButtonColor => SplitScreenMode > 0
            ? ThemeFallbackColors.BrushHex("PrimaryText", ThemeFallbackColors.Text)
            : ThemeFallbackColors.BrushHex("ToolbarButtonBg", ThemeFallbackColors.Surface);
        public string SplitScreenButtonForeground => SplitScreenMode > 0
            ? ThemeFallbackColors.BrushHex("AppBackground", ThemeFallbackColors.Surface)
            : ThemeFallbackColors.BrushHex("MutedText", ThemeFallbackColors.Muted);

        partial void OnSplitScreenModeChanged(int value)
        {
            OnPropertyChanged(nameof(IsSplitScreenOff));
            OnPropertyChanged(nameof(IsSplitScreenHorizontal));
            OnPropertyChanged(nameof(IsSplitScreenVertical));
            OnPropertyChanged(nameof(SplitScreenButtonText));
            OnPropertyChanged(nameof(SplitScreenButtonColor));
            OnPropertyChanged(nameof(SplitScreenButtonForeground));
        }

        public string ConnectButtonColor => IsConnected
            ? ThemeFallbackColors.BrushHex("PrimaryText", ThemeFallbackColors.Text)
            : ThemeFallbackColors.BrushHex("DimText", ThemeFallbackColors.Dim);

        private readonly EditorUiTimer _smoothTimer;

        private NodeViewModel? _wireDragSourceNode;
        private ConnectionViewModel? _wireDragRemovedConn;
        private Dictionary<NodeViewModel, (double X, double Y)>? _nodeDragSnapshot;
        public void StartSmoothViewAnimation()
        {
            if (!_smoothTimer.IsEnabled)
            {
                _smoothTimer.Start();
            }
        }

        private void SmoothUpdateStep()
        {
            double currentZoom = ZoomScale;
            double currentPanX = PanX;
            double currentPanY = PanY;

            bool finished = EditorWorkspaceLayoutService.ComputeSmoothStep(
                ref currentZoom,
                ref currentPanX,
                ref currentPanY,
                TargetZoom,
                TargetPanX,
                TargetPanY);

            ZoomScale = currentZoom;
            PanX = currentPanX;
            PanY = currentPanY;

            if (finished)
            {
                _smoothTimer.Stop();
            }
        }

        [RelayCommand]
        public void ResetCanvasView()
        {
            TargetPanX = 0;
            TargetPanY = 0;
            TargetZoom = 1.0;
            StartSmoothViewAnimation();
        }

        public ObservableCollection<NodeViewModel> Nodes { get; } = new();
        public ObservableCollection<ConnectionViewModel> Connections { get; } = new();

        public AssetBrowserViewModel AssetBrowserViewModel { get; }
        public OutputLogViewModel OutputLogViewModel { get; }
        public BacklogViewModel BacklogViewModel { get; }
        public SaveSlotsViewModel SaveSlotsViewModel { get; }
        public ProjectIssuesViewModel ProjectIssuesViewModel { get; }
        public InspectorViewModel InspectorViewModel { get; }
        public NodeGraphViewModel NodeGraphViewModel { get; }
        /// <summary>Faz 4 Dilim 2 — global search + filter bar owner.</summary>
        public SearchViewModel Search { get; }
        /// <summary>Faz 4 Dilim 3 — visual canvas groups (editor metadata).</summary>
        public GroupService Groups { get; }
        /// <summary>Faz 4 Dilim 3 — subgraph depth stack + breadcrumb scope.</summary>
        public SubgraphNavigationService Subgraphs { get; }
        /// <summary>Faz 4 Dilim 3 — chapter definitions + split/merge files.</summary>
        public ChapterStorageService Chapters { get; }
        public LivePreviewViewModel LivePreviewViewModel { get; }
        public HierarchyViewModel HierarchyViewModel { get; }
        /// <summary>Faz 4 Dilim 5 — background scan worker (lint + prefetch).</summary>
        internal AssetScanWorker AssetScanner { get; } = new();

        private RestoreOffer? _pendingRecoveryOffer;

        /// <summary>Faz 4 Dilim 5 — user-approved crash-restore candidate (null = none).</summary>
        internal RestoreOffer? PendingRecoveryOffer
        {
            get => _pendingRecoveryOffer;
            set => SetProperty(ref _pendingRecoveryOffer, value);
        }

        public MainWindowViewModel() : this(string.Empty)
        {
        }

        public MainWindowViewModel(string projectPath, bool connectEngine = true)
        {
            AssetBitmapCache.Clear();

            LoadPlayerSettings();
            LoadEditorSettings();
            Settings.PropertyChanged += OnSettingsPropertyChanged;

            if (!string.IsNullOrWhiteSpace(projectPath) && Directory.Exists(projectPath))
            {
                CurrentProjectPath = projectPath;
                ProjectRoot = projectPath;
                StatusText = $"Proje yüklendi: {Path.GetFileName(projectPath)}";
            }
            else
            {
                ProjectRoot = ResolveProjectRoot();
                CurrentProjectPath = ProjectRoot;
            }

            ProjectRuntimeSettings = ProjectRuntimeSettingsService.Load(
                Path.Combine(ProjectRoot, "project.rowlproj"));
            LoadProjectRuntimeSettingsIntoEditor();

            AssetBrowserViewModel = new AssetBrowserViewModel(this);
            OutputLogViewModel = new OutputLogViewModel(this);
            BacklogViewModel = new BacklogViewModel(this);
            SaveSlotsViewModel = new SaveSlotsViewModel(this);
            ProjectIssuesViewModel = new ProjectIssuesViewModel(this);
            // Structure services precede the Inspector (Dilim 4): its
            // validation + chapter/group assignment bind to them in its ctor.
            Groups = new GroupService();
            Subgraphs = new SubgraphNavigationService();
            Chapters = new ChapterStorageService();
            InspectorViewModel = new InspectorViewModel(this);
            NodeGraphViewModel = new NodeGraphViewModel(this);
            Search = new SearchViewModel(this);
            // Faz 4 Dilim 3 — session structure wiring (logic in services).
            Groups.Attach(Nodes);
            Subgraphs.Attach(Nodes);
            NodeGraphViewModel.AttachGroups(Groups.Groups);
            NodeGraphViewModel.ScopePredicate = node =>
                Subgraphs.IsNodeVisibleInScope(node.Id, node.ChapterId ?? string.Empty);
            Subgraphs.ScopeChanged += (_, _) => NodeGraphViewModel.RefreshScope();
            LivePreviewViewModel = new LivePreviewViewModel(this);
            HierarchyViewModel = new HierarchyViewModel(this);

            _smoothTimer = new EditorUiTimer(
                TimeSpan.FromMilliseconds(16),
                SmoothUpdateStep,
                autoReset: true);

            AudioComponentViewModel.GlobalPreviewAudioAction = (assetPath, channelType, filterType) =>
            {
                EngineHost.PlayAudio(assetPath, channelType, filterType);
                AppendLog($"Ses önizlemesi başlatıldı: '{assetPath}' (Kanal: {channelType}, Filtre: {filterType})");
            };

            AudioComponentViewModel.GlobalStopAudioAction = () =>
            {
                EngineHost.StopBgm();
                AppendLog("Ses önizlemesi durduruldu.");
            };

            DialogueComponentViewModel.GlobalPreviewVoiceBlipAction = (soundPath, pitch, volume, channelType) =>
            {
                EngineHost.PlayVoiceBlip(soundPath, pitch, volume, channelType);
                AppendLog($"Karakter ses blip önizlemesi: '{soundPath}' (Pitch: {pitch:F2}x, Vol: {volume:P0}, Kanal: {channelType})");
            };

            EngineHost.AudioTelemetryPolled += (pL, pR, rL, rR) =>
            {
                var audioComp = SelectedNode?.Components.OfType<AudioComponentViewModel>().FirstOrDefault();
                EditorSceneSyncService.RouteAudioTelemetry(
                    pL, pR, rL, rR,
                    EngineHost.GetAudioChannelPeak,
                    EngineHost.GetAudioChannelRms,
                    LivePreviewViewModel.UpdateAudioTelemetry,
                    audioComp);
            };

            // Try loading saved story graph from project root
            if (!LoadFullStoryGraphFile())
            {
                var node1 = new NodeViewModel(101, "Giriş Sahnesi", 60, 80)
                {
                    Speaker = "Narrator",
                    DialogueText = "Rowl Engine dünyasına hoş geldiniz! Burası hikayenizin başlangıcı.",
                    BackgroundTexture = "",
                    CharacterSprite = "",
                    DspFilter = "Normal"
                };

                node1.PropertyChanged += OnNodePropertyChanged;
                Nodes.Add(node1);
                EnforceSingleOutgoingWireRule();
            }

            SelectedNode = Nodes.FirstOrDefault();
            UpdateStartNodeState();

            // Faz 4 Dilim 5 — crash-recovery check (read-only; never overwrites).
            CheckCrashRecoveryAtStartup();

            // Embedded engine: initialize directly with isolated project VFS
            if (connectEngine)
                _ = ConnectEngineAsync();
        }

        /// <summary>
        /// Faz 4 Dilim 5 — startup recovery probe. Clears a stale dirty flag
        /// when the canonical document is healthy; otherwise stages a
        /// user-approved restore offer (never an automatic overwrite).
        /// </summary>
        private void CheckCrashRecoveryAtStartup()
        {
            RecoveryStatus status;
            try
            {
                status = CrashRecoveryService.CheckAtStartup(AssetsPath, AssetsJsonPath);
            }
            catch
            {
                return;
            }
            if (status.DirtyFlagPresent && status.CanonicalOk)
            {
                CrashRecoveryService.ClearDirty(AssetsJsonPath);
                AppendLog("Önceki oturum kaydedilmeden kapandı; son tamamlanan kayıt sağlam.");
            }
            else if (!status.CanonicalOk && (status.HasLastGood || status.JournalEntries > 0))
            {
                if (CrashRecoveryService.TryBuildRestoreOffer(AssetsPath, AssetsJsonPath, out var offer, out _) &&
                    offer is not null)
                {
                    PendingRecoveryOffer = offer;
                    AppendLog($"Proje dosyası bozuk/eksik ({status.CanonicalError}); kurtarma hazır ({offer.Source}, {offer.NodeCount} düğüm). RestoreFromRecovery ile geri yükleyin.");
                }
            }
        }

        /// <summary>Faz 4 Dilim 5 — applies the staged crash-restore offer (explicit only).</summary>
        [RelayCommand]
        public void RestoreFromRecovery()
        {
            if (PendingRecoveryOffer is null)
                return;
            if (CrashRecoveryService.RestoreOfferToCanonical(AssetsJsonPath, PendingRecoveryOffer) &&
                LoadFullStoryGraphFile())
            {
                AppendLog($"Kurtarma uygulandı ({PendingRecoveryOffer.Source}, {PendingRecoveryOffer.NodeCount} düğüm).");
                PendingRecoveryOffer = null;
            }
            else
            {
                AppendLog("Kurtarma uygulanamadı.");
            }
        }

        public void UpdateStartNodeState()
        {
            StoryGraphLifecycleCoordinator.UpdateStartNodeState(Nodes, Connections);
        }

        private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(NodeViewModel.X) || e.PropertyName == nameof(NodeViewModel.Y) ||
                e.PropertyName == nameof(NodeViewModel.ChoiceOptions))
            {
                foreach (var conn in Connections)
                {
                    conn.UpdatePoints();
                }
            }
            if (e.PropertyName == nameof(NodeViewModel.Objects) ||
                     e.PropertyName == nameof(NodeViewModel.Components) ||
                     e.PropertyName == nameof(NodeViewModel.ChoiceOptions) ||
                     e.PropertyName == nameof(NodeViewModel.ChoiceDataChanged) ||
                     e.PropertyName == nameof(NodeViewModel.Speaker) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueText) ||
                     e.PropertyName == nameof(NodeViewModel.BackgroundTexture) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterSprite) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterPosition) ||
                     e.PropertyName == nameof(NodeViewModel.BackgroundX) ||
                     e.PropertyName == nameof(NodeViewModel.BackgroundY) ||
                     e.PropertyName == nameof(NodeViewModel.BackgroundWidth) ||
                     e.PropertyName == nameof(NodeViewModel.BackgroundHeight) ||
                     e.PropertyName == nameof(NodeViewModel.BackgroundScale) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterX) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterY) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterWidth) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterHeight) ||
                     e.PropertyName == nameof(NodeViewModel.CharacterScale) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueBoxX) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueBoxY) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueBoxWidth) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueBoxHeight) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueBoxScale) ||
                     e.PropertyName == nameof(NodeViewModel.FontSize) ||
                     e.PropertyName == nameof(NodeViewModel.SpeakerFontSize) ||
                     e.PropertyName == nameof(NodeViewModel.TextColor) ||
                     e.PropertyName == nameof(NodeViewModel.SpeakerColor) ||
                     e.PropertyName == nameof(NodeViewModel.BoxOpacity) ||
                     e.PropertyName == nameof(NodeViewModel.BoxColor) ||
                     e.PropertyName == nameof(NodeViewModel.BorderColorHex) ||
                     e.PropertyName == nameof(NodeViewModel.BorderThickness) ||
                     e.PropertyName == nameof(NodeViewModel.CornerRadius) ||
                     e.PropertyName == nameof(NodeViewModel.TextAlignment) ||
                     e.PropertyName == nameof(NodeViewModel.DialogueComponent))
            {
                if (IsInteractivelyDragging)
                {
                    // Fast path: During active mouse dragging/resizing in Edit Frame,
                    // skip synchronous C++ JSON rendering to guarantee smooth 60+ FPS.
                    return;
                }

                ScheduleSave();

                // Coalesce rapid text/drag/property changes instead of serializing
                // JSON and crossing P/Invoke synchronously for every UI event.
                if (sender is NodeViewModel node && node == SelectedNode && EngineHost.IsInitialized)
                    ScheduleEnginePreviewUpdate(node);
            }
        }

        public void EnforceSingleOutgoingWireRule()
        {
            StoryGraphCanvasService.EnforceSingleOutgoingWireRule(Connections, UpdateStartNodeState);
        }

        public void StartWireDrag(NodeViewModel sourceNode, Point pinPos, string optionId = "")
        {
            _wireDragSourceNode = sourceNode;
            _wireDragOptionId = optionId;
            _wireDragRemovedConn = null;
            WireStartPoint = new Point(sourceNode.X + 265, sourceNode.Y + sourceNode.GetOutputPortY(optionId));
            WireEndPoint = pinPos;
            IsDraggingWire = true;
            AppendLog($"Started drawing wire from Green Output Pin of Node #{sourceNode.Id}...");
        }

        public void StartUnplugWireDrag(NodeViewModel sourceNode, Point mousePos, string optionId = "", ConnectionViewModel? removedConn = null)
        {
            _wireDragSourceNode = sourceNode;
            _wireDragOptionId = optionId;
            _wireDragRemovedConn = removedConn;
            WireStartPoint = new Point(sourceNode.X + 265, sourceNode.Y + sourceNode.GetOutputPortY(optionId));
            WireEndPoint = mousePos;
            IsDraggingWire = true;
            AppendLog($"Unplugged cable from Node #{sourceNode.Id}, re-routing wire...");
        }

        public void UpdateWireDrag(Point currentMousePos)
        {
            if (IsDraggingWire)
            {
                WireEndPoint = currentMousePos;
            }
        }

        public void EndWireDrag(Point releasePos)
        {
            if (!IsDraggingWire || _wireDragSourceNode == null) return;
            IsDraggingWire = false;

            var sourceNode = _wireDragSourceNode;
            var optionId = _wireDragOptionId;
            var unplugged = _wireDragRemovedConn;

            var replacedBefore = Connections
                .Where(c => c.SourceNode == sourceNode &&
                    (string.IsNullOrEmpty(optionId) || c.OptionId == optionId))
                .ToList();
            ulong targetBefore = StoryGraphCanvasService.GetChoiceTarget(sourceNode, optionId);

            var newConn = StoryGraphCanvasService.TryConnectWire(
                sourceNode,
                releasePos,
                Nodes,
                Connections,
                optionId);

            if (newConn != null)
            {
                var replaced = replacedBefore.Where(c => !Connections.Contains(c)).ToList();
                if (unplugged != null && !replaced.Contains(unplugged))
                    replaced.Insert(0, unplugged);
                var changes = new List<ChoiceTargetChange>();
                if (!string.IsNullOrEmpty(optionId))
                {
                    changes.Add(new ChoiceTargetChange(
                        sourceNode, optionId, targetBefore,
                        newConn.TargetNode?.Id ?? 0));
                }
                UndoRedoService.Instance.RecordAction(new ConnectWireAction(
                    Connections, newConn, replaced, changes, UpdateStartNodeState));
                AppendLog($"Connected Wire: Node #{sourceNode.Id} ---> Node #{newConn.TargetNode?.Id} (Total cables: {Connections.Count})");
            }
            else if (unplugged != null)
            {
                UndoRedoService.Instance.RecordAction(
                    new DisconnectCablesUndoAction(Connections, new List<ConnectionViewModel> { unplugged }, UpdateStartNodeState));
                AppendLog("Connection dropped in empty space (cable unplugged / removed).");
            }
            else
            {
                AppendLog("Connection dropped in empty space (cable unplugged / removed).");
            }

            UpdateStartNodeState();
            ScheduleSave();
            _wireDragSourceNode = null;
            _wireDragOptionId = string.Empty;
            _wireDragRemovedConn = null;
        }

        /// <summary>
        /// MS-5: cancels an in-flight wire gesture (Escape / capture loss / focus loss).
        /// A previously unplugged cable is restored silently: no undo record is
        /// produced because the gesture never committed.
        /// Safe to call when no wire drag is active (no-op).
        /// </summary>
        public void CancelWireDrag()
        {
            if (!IsDraggingWire && _wireDragSourceNode == null && _wireDragRemovedConn == null) return;
            IsDraggingWire = false;

            var unplugged = _wireDragRemovedConn;
            if (unplugged != null && !Connections.Contains(unplugged))
            {
                Connections.Add(unplugged);
                UndoChoiceTarget.RestoreFor(new[] { unplugged });
            }

            _wireDragSourceNode = null;
            _wireDragOptionId = string.Empty;
            _wireDragRemovedConn = null;
            UpdateStartNodeState();
            AppendLog("Geri Al: Kablo çekme iptal edildi (değişiklik yok).");
        }

        /// <summary>
        /// MS-5: cancels an in-flight node drag (Escape / capture loss / focus loss).
        /// Positions revert to <see cref="BeginNodeDragSnapshot"/> state and no
        /// undo record is produced because the gesture never committed.
        /// Safe to call when no drag snapshot exists (no-op).
        /// </summary>
        public void CancelNodeDrag()
        {
            var snapshot = _nodeDragSnapshot;
            _nodeDragSnapshot = null;
            if (snapshot == null || snapshot.Count == 0) return;

            foreach (var (node, pos) in snapshot)
            {
                if (!Nodes.Contains(node)) continue;
                node.X = pos.X;
                node.Y = pos.Y;
            }
            AppendLog("Geri Al: Sürükleme iptal edildi, düğümler başlangıç konumuna döndü.");
        }

        /// <summary>MS-5: true while a drag snapshot is pending commit or cancel.</summary>
        public bool HasPendingNodeDrag => _nodeDragSnapshot != null && _nodeDragSnapshot.Count > 0;

        /// <summary>
        /// Snapshots drag-affected node positions. Call on pointer-press before any move.
        /// </summary>
        public void BeginNodeDragSnapshot()
        {
            IEnumerable<NodeViewModel> affected = SelectedNodes.Count > 0
                ? SelectedNodes.ToList()
                : (SelectedNode != null ? new[] { SelectedNode } : Enumerable.Empty<NodeViewModel>());
            _nodeDragSnapshot = affected.ToDictionary(n => n, n => (n.X, n.Y));
        }

        /// <summary>
        /// Records one atomic MoveNodesAction when the gesture actually moved nodes.
        /// </summary>
        public void EndNodeDragSnapshot()
        {
            var snapshot = _nodeDragSnapshot;
            _nodeDragSnapshot = null;
            if (snapshot == null || snapshot.Count == 0) return;

            var before = new List<NodePosition>();
            var after = new List<NodePosition>();
            foreach (var (node, pos) in snapshot)
            {
                if (!Nodes.Contains(node)) continue;
                if (node.X != pos.X || node.Y != pos.Y)
                {
                    before.Add(new NodePosition(node, pos.X, pos.Y));
                    after.Add(new NodePosition(node, node.X, node.Y));
                }
            }
            if (before.Count > 0)
            {
                UndoRedoService.Instance.RecordAction(new MoveNodesAction(before, after));
                ScheduleSave();
            }
        }

        private List<NodePosition> SnapshotNodePositions(IEnumerable<NodeViewModel> nodes)
        {
            return nodes.Select(n => new NodePosition(n, n.X, n.Y)).ToList();
        }

        public void DisconnectNodeInputs(NodeViewModel node)
        {
            var removed = Connections.Where(c => c.TargetNode == node).ToList();
            int count = StoryGraphCanvasService.DisconnectNodeInputs(node, Connections);
            if (count > 0)
            {
                UndoRedoService.Instance.RecordAction(new DisconnectCablesUndoAction(Connections, removed, UpdateStartNodeState));
                ScheduleSave();
                AppendLog($"Disconnected {count} incoming cable(s) from Node #{node.Id}");
            }
        }

        public void DisconnectNodeOutputs(NodeViewModel node, string optionId = "")
        {
            var removed = Connections.Where(c => c.SourceNode == node &&
                (string.IsNullOrEmpty(optionId) || c.OptionId == optionId)).ToList();
            int count = StoryGraphCanvasService.DisconnectNodeOutputs(node, Connections, optionId);
            if (count > 0)
            {
                UndoRedoService.Instance.RecordAction(new DisconnectCablesUndoAction(Connections, removed, UpdateStartNodeState));
                ScheduleSave();
                AppendLog($"Disconnected {count} outgoing cable(s) from Node #{node.Id}");
            }
        }

        private static void SetChoiceTarget(NodeViewModel node, string optionId, ulong targetNodeId)
        {
            StoryGraphCanvasService.SetChoiceTarget(node, optionId, targetNodeId);
        }

        [RelayCommand]
        public void DisconnectSelectedNodeCables()
        {
            if (SelectedNode == null) return;
            DisconnectAllNodeCables(SelectedNode);
        }

        public void DisconnectAllNodeCables(NodeViewModel node)
        {
            var removed = Connections.Where(c => c.SourceNode == node || c.TargetNode == node).ToList();
            int count = StoryGraphCanvasService.DisconnectAllNodeCables(node, Connections, SetChoiceTarget);
            if (count > 0)
            {
                UndoRedoService.Instance.RecordAction(new DisconnectCablesUndoAction(Connections, removed, UpdateStartNodeState));
                ScheduleSave();
                AppendLog($"Disconnected all {count} cable(s) attached to Node #{node.Id}");
            }
        }

        [RelayCommand]
        public void DeleteSelectedNode()
        {
            if (SelectedNode == null) return;
            DeleteNode(SelectedNode);
        }

        public void DeleteNode(NodeViewModel node)
        {
            var attached = Connections
                .Where(c => c.SourceNode == node || c.TargetNode == node)
                .ToList();
            StoryGraphCanvasService.DeleteNode(node, Nodes, Connections, SetChoiceTarget);
            UndoRedoService.Instance.RecordAction(new DeleteNodeUndoAction(this, node, attached));
            AppendLog($"Deleted Node #{node.Id} ({node.Title})");
            SelectedNode = Nodes.FirstOrDefault();
            UpdateStartNodeState();
        }

        [RelayCommand]
        public void AddNode()
        {
            ulong nextId = Nodes.Count > 0 ? Nodes.Max(n => n.Id) + 1 : 101;
            var (spawnX, spawnY) = StoryGraphCanvasService.CalculateSpawnPosition(Nodes.Count, PanX, PanY, ZoomScale);
            var newNode = StoryGraphCanvasService.CreateDefaultNode(nextId, spawnX, spawnY);

            newNode.PropertyChanged += OnNodePropertyChanged;
            Nodes.Add(newNode);
            SelectedNode = newNode;
            UndoRedoService.Instance.RecordAction(new AddNodeUndoAction(this, newNode));
            UpdateStartNodeState();
            AppendLog($"Added new node #{nextId} at visible screen center ({spawnX:F0}, {spawnY:F0})");
        }

        [RelayCommand]
        public async Task ConnectEngineAsync()
        {
            StatusText = "Initializing embedded C++ Engine...";
            AppendLog("[Engine] Starting embedded RowlEngineCore library...");
            // Let the status text paint before the blocking native init below.
            await Task.Yield();

            // Native engine calls, framebuffer access and the DispatcherTimer must
            // remain on the UI thread for the lifetime of this host (the C-API
            // handle is thread-claimed at Init: see claimHandleThread in
            // c_api_lifecycle.cpp). Every await below resumes on the UI thread,
            // so ownership never migrates.
            bool success = EngineHost.Initialize(1920, 1080, true);
            IsConnected = success;
            if (!success)
            {
                StatusText = "Engine Init Failed — Check that libRowlEngineCore.so is built.";
                AppendLog("[Engine] RowlEngineCore initialization failed. Run: cmake --build build");
                return;
            }

            StatusText = "Mounting project VFS...";
            await Task.Yield();
            EngineHost.SetProjectDirectory(ProjectRoot);

            StatusText = "Applying player settings...";
            await Task.Yield();
            ApplyPlayerSettingsToEngine();

            StatusText = "Engine Ready — Embedded C++ Runtime Active";
            AppendLog($"[Engine] RowlEngineCore mounted isolated project: {ProjectRoot}");

            // Push the currently selected node to the engine immediately
            if (SelectedNode != null)
                PushSceneToEngine(SelectedNode);
        }

        private void LoadPlayerSettings()
        {
            EditorSettingsSyncService.LoadPlayerSettings(_playerSettingsPath, Settings);
        }

        private void LoadEditorSettings()
        {
            EditorSettingsSyncService.LoadEditorSettings(_editorSettingsPath, Settings);
        }

        private void LoadProjectRuntimeSettingsIntoEditor()
        {
            EditorSettingsSyncService.LoadProjectRuntimeSettings(ProjectRuntimeSettings, Settings);
        }

        private void OnSettingsPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            EditorSettingsSyncService.HandleSettingsPropertyChanged(
                e.PropertyName,
                Settings,
                _editorSettingsPath,
                _playerSettingsPath,
                ProjectRoot,
                EngineHost,
                () => ProjectRuntimeSettings,
                updated => ProjectRuntimeSettings = updated,
                () => SaveSlotsViewModel?.Refresh());
        }

        private void ApplyPlayerSettingsToEngine(PlayerSettingsProfile? profile = null)
        {
            EditorSettingsSyncService.ApplyPlayerSettingsToEngine(EngineHost, Settings, profile);
        }

        /// <summary>
        /// Compatibility alias — kept so any XAML bindings that reference
        /// ConnectIpcAsync continue to compile during transition.
        /// </summary>
        [RelayCommand]
        public Task ConnectIpcAsync() => ConnectEngineAsync();

        /// <summary>Sends the active node's scene data directly to the engine via P/Invoke.</summary>
        public bool PushSceneToEngine(NodeViewModel node)
        {
            bool updated = EditorSceneSyncService.PushSceneToEngine(EngineHost, node, msg => AppendLog(msg));
            CheckEngineDiagnostics();
            return updated;
        }

        public void CheckEngineDiagnostics()
        {
            if (!EngineHost.IsInitialized) return;
            CheckAudioDeviceStatus(EngineHost.IsAudioDeviceAvailable);
            NotificationService.ReportEngineDiagnostic(
                EngineHost.LastResultCode,
                EngineHost.LastResultOperation,
                EngineHost.LastResultMessage,
                EngineHost.LastResultTarget,
                AppendLog);
        }

        /// <summary>
        /// Edge-triggered audio-device observer. Call with the polled native
        /// device state; toasts only on transitions so a missing device does
        /// not spam on every diagnostics poll. Public and parameter-driven so
        /// headless tests can drive it without a native handle.
        /// </summary>
        public void CheckAudioDeviceStatus(bool deviceAvailable)
        {
            NotificationService.ReportAudioDeviceTransition(deviceAvailable, AppendLog);
        }

        private void ApplyScriptRuntimeDiagnostics(NodeViewModel node)
        {
            EditorSceneSyncService.ApplyScriptRuntimeDiagnostics(EngineHost, node);
        }

        /// <summary>
        /// Loads the story graph from full_story_graph.json.
        /// Supports v3 (GameObject hierarchy), v2 (component-based), and legacy v1 (flat fields) formats.
        /// Returns true if file was loaded successfully, false if file doesn't exist or parsing failed.
        /// </summary>
        public bool LoadFullStoryGraphFile()
        {
            return StoryGraphLifecycleCoordinator.LoadGraphWithRollback(
                AssetsPath,
                AssetsJsonPath,
                Nodes,
                Connections,
                OnNodePropertyChanged,
                EnforceSingleOutgoingWireRule,
                node => SelectNodeQuiet(node),
                AppendLog,
                ApplyLoadedStructure);
        }

        public bool SaveFullStoryGraphFile()
        {
            return StoryGraphLifecycleCoordinator.SaveFullGraph(
                AssetsPath,
                AssetsJsonPath,
                Nodes,
                Connections,
                GetStartNode()?.Id,
                AppendLog,
                CurrentStructure());
        }

        // Faz 4 Dilim 3 — session structure (groups / subgraphs / chapters)
        // The parsed vNext document lands here on load and is composed back on
        // every save path; all frame/stack/file logic lives in the services.

        /// <summary>Applies a parsed vNext structure to the session services.</summary>
        public void ApplyLoadedStructure(GraphStructureDocument structure)
        {
            var safe = structure ?? new GraphStructureDocument();
            Groups.LoadFrom(safe.Groups);
            Subgraphs.LoadDefinitions(safe.Subgraphs);
            Chapters.LoadDefinitions(safe.Chapters);
            Subgraphs.SetAvailableChapters(
                Chapters.EffectiveChapters(Nodes.Select(n => n.ChapterId)));
            NodeGraphViewModel.RefreshScope();
        }

        /// <summary>Composes the session services back into one vNext document.</summary>
        public GraphStructureDocument CurrentStructure()
        {
            var document = new GraphStructureDocument();
            document.Groups.AddRange(Groups.ToRecords());
            document.Subgraphs.AddRange(Subgraphs.Definitions);
            document.Chapters.AddRange(Chapters.ToRecords());
            return document;
        }

        /// <summary>Creates a group framing the current selection (or node).</summary>
        [RelayCommand]
        public void CreateGroupFromSelection()
        {
            var picked = SelectedNodes.Count > 0
                ? SelectedNodes.ToList()
                : (SelectedNode is not null
                    ? new List<NodeViewModel> { SelectedNode }
                    : new List<NodeViewModel>());
            if (picked.Count == 0)
            {
                AppendLog("Grup için önce düğüm seçin.");
                return;
            }
            var group = Groups.CreateFromNodes(
                $"Grup {Groups.Groups.Count + 1}", CanvasGroupViewModel.DefaultColor, picked);
            AppendLog($"Grup oluşturuldu: '{group.Title}' ({picked.Count} düğüm).");
        }

        /// <summary>Enters the subgraph owning the selected node.</summary>
        [RelayCommand]
        public void EnterSelectedSubgraph()
        {
            if (SelectedNode is null)
                return;
            EnterSubgraphForNode(SelectedNode.Id);
        }

        /// <summary>
        /// Faz 4 Dilim 5 — deep-nav issue focus (delegation only; all logic
        /// lives in SubgraphNavigationService + SearchViewModel). Scope
        /// first (breadcrumb sync), then pan: panning before the scope opens
        /// would highlight a still-culled card.
        /// </summary>
        public void FocusIssueNode(ulong? nodeId)
        {
            if (nodeId is not ulong id) return;
            if (Nodes.FirstOrDefault(n => n.Id == id) is not { } node) return;
            Subgraphs.TryEnterForNode(id);
            Search.JumpTo(node);
        }

        /// <summary>Enters the subgraph owning the node (double-click path).</summary>
        public bool EnterSubgraphForNode(ulong nodeId)
        {
            if (!Subgraphs.TryEnterForNode(nodeId))
                return false;
            if (Subgraphs.TryGetEntry(Subgraphs.CurrentSubgraphId ?? string.Empty, out ulong entryId) &&
                Nodes.FirstOrDefault(n => n.Id == entryId) is { } entry)
            {
                var (x, y, w, h) = NodeGraphViewModel.NodeBounds(entry);
                NodeGraphViewModel.PanTo(x + w / 2, y + h / 2);
            }
            return true;
        }

        /// <summary>Leaves the innermost subgraph (breadcrumb "up").</summary>
        [RelayCommand]
        public void ExitSubgraph()
        {
            Subgraphs.Exit();
        }

        /// <summary>Returns to root scope (clears depth + chapter filter).</summary>
        [RelayCommand]
        public void GoToRootScope()
        {
            Subgraphs.GoToRoot();
        }

        /// <summary>Navigates the breadcrumb to a depth (0 = root).</summary>
        [RelayCommand]
        public void GoToScopeDepth(int depth)
        {
            Subgraphs.GoToDepth(depth);
        }

        /// <summary>Splits the live graph into per-chapter files.</summary>
        [RelayCommand]
        public void SplitChaptersToFiles()
        {
            try
            {
                string json = StoryGraphSerializer.SerializeFullStoryGraph(
                    Nodes, Connections, GetStartNode()?.Id ?? 101, CurrentStructure());
                var written = ChapterStorageService.Split(
                    json, Path.Combine(AssetsJsonPath, "chapters"));
                AppendLog($"Bölümlere ayrıldı: {string.Join(", ", written)}.");
            }
            catch (Exception ex)
            {
                AppendLog($"Bölümlere ayırma başarısız: {ex.Message}");
            }
        }

        /// <summary>Merges per-chapter files back into the full graph file.</summary>
        [RelayCommand]
        public void MergeChaptersFromFiles()
        {
            try
            {
                string merged = ChapterStorageService.Merge(
                    Path.Combine(AssetsJsonPath, "chapters"));
                Directory.CreateDirectory(AssetsJsonPath);
                ProjectFileSystem.WriteAllTextAtomically(
                    Path.Combine(AssetsJsonPath, "full_story_graph.json"), merged);
                if (LoadFullStoryGraphFile())
                    AppendLog("Bölümler birleştirildi ve tuval yenilendi.");
            }
            catch (Exception ex)
            {
                AppendLog($"Bölüm birleştirme başarısız: {ex.Message}");
            }
        }

        public bool SaveActiveStoryFile()
        {
            return StoryGraphLifecycleCoordinator.SaveActiveStory(
                AssetsJsonPath,
                SelectedNode,
                Nodes,
                AppendLog);
        }

        [RelayCommand]
        public void SetSquareDialogueBox()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.PresetDialogueBox(SelectedNode, "Square");
            AppendLog($"Set Dialogue Box to Square (500x500) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public void SetStandardDialogueBox()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.PresetDialogueBox(SelectedNode, "Standard");
            AppendLog($"Reset Dialogue Box to Standard Banner (1760x180) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public void ResetBackgroundDimensions()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.FitBackgroundToScreen(SelectedNode);
            AppendLog($"Reset Background to Fullscreen Canvas (1920x1080) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public void ResetCharacterDimensions()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.ResetCharacterDimensions(SelectedNode);
            AppendLog($"Reset Character Sprite size to Default (360x540) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public async Task PushHotReloadPacketAsync()
        {
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();

            if (SelectedNode == null)
            {
                AppendLog("[Hot-Reload] No node selected.");
                return;
            }

            if (!EngineHost.IsInitialized)
            {
                // Engine not yet ready — try to start it
                await ConnectEngineAsync();
                if (!EngineHost.IsInitialized)
                {
                    AppendLog("[Hot-Reload] Engine not running. Build libRowlEngineCore first.");
                    return;
                }
            }

            // Direct P/Invoke call — zero serialization, nanosecond latency
            PushSceneToEngine(SelectedNode);

            // Also reload the full graph so the engine picks up connection changes
            EditorSceneSyncService.ReloadStoryGraphIntoEngine(EngineHost, AssetsJsonPath);

            AppendLog($"[Hot-Reload] Scene pushed directly to engine (P/Invoke) — Node #{SelectedNode.Id}");
            IsConnected = true;
        }

        [ObservableProperty]
        private bool _isPlayingStandalone = false;

        [ObservableProperty]
        private string _playButtonText = "Play";

        [ObservableProperty]
        private string _playButtonColor =
            ThemeFallbackColors.BrushHex("PlayButtonGreenColor", ThemeFallbackColors.Success);

        public NodeViewModel? GetStartNode()
        {
            return StoryGraphLifecycleCoordinator.ResolveStartNode(Nodes, Connections);
        }

        [RelayCommand]
        public void TogglePlayStandalone()
        {
            if (IsPlayingStandalone)
                StopStandaloneGame();
            else
                StartStandaloneGame();
        }

        /// <summary>
        /// Faz 2 standalone player shell entry point. Builds the session
        /// player stack (profile → loop service → view-model) over the
        /// shared engine host and opens the player window; every later
        /// decision lives in <see cref="ViewModels.Player.PlayerViewModel"/>.
        /// </summary>
        [RelayCommand]
        public void OpenPlayerWindow()
        {
            var loop = Services.PlayerLoopService.Load(
                PlayerProfileStore.ResolveDefaultProfileDirectory());
            var player = new ViewModels.Player.PlayerViewModel(
                new ViewModels.Player.EngineHostPlayerAdapter(EngineHost), loop);
            var window = new Views.Player.PlayerWindow(player);
            window.Show();
        }

        private async void StartStandaloneGame()
        {
            bool started = await EditorPlayModeCoordinator.StartPlayModeAsync(
                EngineHost,
                async () =>
                {
                    await ConnectEngineAsync();
                    return EngineHost.IsInitialized;
                },
                () => SaveActiveStoryFile() && SaveFullStoryGraphFile(),
                node => SelectNodeQuiet(node),
                node => PushSceneToEngine(node),
                GetStartNode,
                msg => AppendLog(msg),
                () =>
                {
                    if (SplitScreenMode == 0)
                    {
                        IsEnginePreviewActive = true;
                        IsPreviewActive = false;
                        IsNodeGraphActive = false;
                    }
                },
                AssetsJsonPath);

            if (started)
            {
                IsPlayingStandalone = true;
                PlayButtonText = "Stop";
                PlayButtonColor = ThemeFallbackColors.BrushHex("DangerButtonBg", ThemeFallbackColors.Error);
                StatusText = "Offscreen Play Mode Active";
            }
        }

        private void StopStandaloneGame()
        {
            EditorPlayModeCoordinator.StopPlayMode(
                EngineHost,
                GetStartNode,
                node => PushSceneToEngine(node),
                node => SelectNode(node),
                msg => AppendLog(msg));

            IsPlayingStandalone = false;
            PlayButtonText = "Play";
            PlayButtonColor = ThemeFallbackColors.BrushHex("PlayButtonGreenColor", ThemeFallbackColors.Success);
            StatusText = "Engine Ready — Offscreen C++ Runtime Active";
        }

        public void SelectNode(NodeViewModel node, bool addToSelection = false)
        {
            EditorSelectionCoordinator.SelectNode(node, addToSelection, Nodes, SelectedNodes, p => SelectedNode = p);
            ScheduleSave();
            AppendLog($"Selected Node #{node.Id} ({node.Title}) [Total: {SelectedNodes.Count}]");
        }

        /// <summary>
        /// Selects a node without triggering debounced file saves (used during gameplay for zero-latency node advance).
        /// </summary>
        public void SelectNodeQuiet(NodeViewModel node, bool addToSelection = false)
        {
            EditorSelectionCoordinator.SelectNode(node, addToSelection, Nodes, SelectedNodes, p => SelectedNode = p);
            AppendLog($"Selected Node #{node.Id} ({node.Title})");
        }

        [RelayCommand]
        public void SelectAllNodes()
        {
            EditorSelectionCoordinator.SelectAllNodes(Nodes, SelectedNodes, p => SelectedNode = p);
            AppendLog($"Selected all {SelectedNodes.Count} node(s)");
        }

        [RelayCommand]
        public void ClearNodeSelection()
        {
            EditorSelectionCoordinator.ClearNodeSelection(Nodes, SelectedNodes, p => SelectedNode = p);
            AppendLog("Cleared node selection");
        }

        [RelayCommand]
        public void InvertNodeSelection()
        {
            EditorSelectionCoordinator.InvertNodeSelection(Nodes, SelectedNodes, p => SelectedNode = p);
            AppendLog($"Inverted node selection [Now selected: {SelectedNodes.Count}]");
        }

        public void StartSelectionBox(Point startPoint)
        {
            SelectionBoxX = startPoint.X;
            SelectionBoxY = startPoint.Y;
            SelectionBoxWidth = 0;
            SelectionBoxHeight = 0;
            IsSelectingBox = true;
        }

        public void UpdateSelectionBox(Point startPoint, Point currentPoint, bool append = false)
        {
            var boxRect = EditorSelectionCoordinator.ComputeSelectionRect(startPoint, currentPoint);

            SelectionBoxX = boxRect.X;
            SelectionBoxY = boxRect.Y;
            SelectionBoxWidth = boxRect.Width;
            SelectionBoxHeight = boxRect.Height;

            EditorSelectionCoordinator.SelectNodesInBox(boxRect, Nodes, SelectedNodes, p => SelectedNode = p, append);
        }

        public void EndSelectionBox()
        {
            IsSelectingBox = false;
            SelectionBoxWidth = 0;
            SelectionBoxHeight = 0;
            ScheduleSave();
        }

        public void BatchMoveSelectedNodes(double deltaX, double deltaY)
        {
            if (SelectedNodes.Count == 0 && SelectedNode != null)
            {
                SelectedNodes.Add(SelectedNode);
            }
            EditorBatchOperationService.BatchMoveNodes(SelectedNodes, deltaX, deltaY);
        }

        [RelayCommand]
        public void DeleteSelectedNodes()
        {
            var targets = SelectedNodes.Count > 0 ? SelectedNodes.ToList() : (SelectedNode != null ? new List<NodeViewModel> { SelectedNode } : new List<NodeViewModel>());
            if (targets.Count == 0) return;

            int deletedCount = EditorBatchOperationService.BatchDeleteNodes(
                targets, Nodes, Connections, SetChoiceTarget, UpdateStartNodeState);

            SelectedNodes.Clear();
            SelectedNode = Nodes.FirstOrDefault();
            if (SelectedNode != null)
            {
                SelectedNode.IsSelected = true;
                SelectedNodes.Add(SelectedNode);
            }
            UpdateStartNodeState();
            ScheduleSave();
            AppendLog($"Batch deleted {deletedCount} node(s)");
        }

        [RelayCommand]
        public void DuplicateSelectedNodes()
        {
            var targets = SelectedNodes.Count > 0 ? SelectedNodes.ToList() : (SelectedNode != null ? new List<NodeViewModel> { SelectedNode } : new List<NodeViewModel>());
            if (targets.Count == 0) return;

            var clones = EditorBatchOperationService.BatchDuplicateNodes(
                targets, Nodes, Connections, 40.0, 40.0, UpdateStartNodeState);

            foreach (var n in Nodes) n.IsSelected = false;
            SelectedNodes.Clear();
            foreach (var clone in clones)
            {
                clone.IsSelected = true;
                SelectedNodes.Add(clone);
            }
            SelectedNode = clones.LastOrDefault();
            UpdateStartNodeState();
            ScheduleSave();
            AppendLog($"Batch duplicated {clones.Count} node(s) with internal wires preserved");
        }

        // MS-5: canvas clipboard (Ctrl+C / Ctrl+V)
        private readonly List<NodeViewModel> _nodeClipboard = new();
        private double _pasteOffsetX = 40.0;
        private double _pasteOffsetY = 40.0;

        /// <summary>MS-5: number of nodes currently held in the canvas clipboard.</summary>
        public int ClipboardNodeCount => _nodeClipboard.Count;

        /// <summary>
        /// MS-5: copies the current selection into the canvas clipboard.
        /// Stores references; paste clones them with fresh ids via the batch
        /// duplicate path, so undo integrity (MS-1) is preserved.
        /// </summary>
        [RelayCommand]
        public void CopySelectedNodes()
        {
            _nodeClipboard.Clear();
            var targets = SelectedNodes.Count > 0 ? SelectedNodes.ToList()
                : (SelectedNode != null ? new List<NodeViewModel> { SelectedNode } : new List<NodeViewModel>());
            _nodeClipboard.AddRange(targets);
            _pasteOffsetX = 40.0;
            _pasteOffsetY = 40.0;
            if (_nodeClipboard.Count > 0)
                AppendLog($"Copied {_nodeClipboard.Count} node(s) to clipboard");
        }

        /// <summary>
        /// MS-5: pastes clipboard nodes with a cascading offset. Each paste is
        /// one atomic undo step (BatchDuplicateNodesUndoAction).
        /// </summary>
        [RelayCommand]
        public void PasteClipboardNodes()
        {
            var sources = _nodeClipboard.Where(n => n != null).ToList();
            if (sources.Count == 0) return;

            var clones = EditorBatchOperationService.BatchDuplicateNodes(
                sources, Nodes, Connections, _pasteOffsetX, _pasteOffsetY, UpdateStartNodeState);

            foreach (var n in Nodes) n.IsSelected = false;
            SelectedNodes.Clear();
            foreach (var clone in clones)
            {
                clone.IsSelected = true;
                SelectedNodes.Add(clone);
            }
            SelectedNode = clones.LastOrDefault();

            _pasteOffsetX += 20.0;
            _pasteOffsetY += 20.0;

            UpdateStartNodeState();
            ScheduleSave();
            AppendLog($"Pasted {clones.Count} node(s) from clipboard");
        }

        // MS-5: arrow-key nudge
        /// <summary>
        /// MS-5: moves the selection by a small delta as one atomic undo step.
        /// Used by arrow keys (1px, 10px with Shift).
        /// </summary>
        public void NudgeSelectedNodes(double deltaX, double deltaY)
        {
            var targets = SelectedNodes.Count > 0 ? SelectedNodes.ToList()
                : (SelectedNode != null ? new List<NodeViewModel> { SelectedNode } : new List<NodeViewModel>());
            if (targets.Count == 0) return;
            if (deltaX == 0 && deltaY == 0) return;

            var before = SnapshotNodePositions(targets);
            EditorBatchOperationService.BatchMoveNodes(targets, deltaX, deltaY);
            UndoRedoService.Instance.RecordAction(new MoveNodesAction(before, SnapshotNodePositions(targets)));
            ScheduleSave();
        }

        [RelayCommand]
        public void AlignSelectedNodes(string alignmentStr)
        {
            if (Enum.TryParse<BatchAlignment>(alignmentStr, true, out var alignment))
            {
                var targets = SelectedNodes.Count > 1 ? SelectedNodes.ToList() : Nodes.Where(n => n.IsSelected).ToList();
                if (targets.Count < 2) return;
                var before = SnapshotNodePositions(targets);
                EditorBatchOperationService.BatchAlignNodes(targets, alignment);
                UndoRedoService.Instance.RecordAction(new MoveNodesAction(before, SnapshotNodePositions(targets)));
                ScheduleSave();
                AppendLog($"Aligned {targets.Count} node(s) to {alignment}");
            }
        }

        [RelayCommand]
        public void DistributeSelectedNodes(string distributionStr)
        {
            if (Enum.TryParse<BatchDistribution>(distributionStr, true, out var distribution))
            {
                var targets = SelectedNodes.Count > 2 ? SelectedNodes.ToList() : Nodes.Where(n => n.IsSelected).ToList();
                if (targets.Count < 3) return;
                var before = SnapshotNodePositions(targets);
                EditorBatchOperationService.BatchDistributeNodes(targets, distribution);
                UndoRedoService.Instance.RecordAction(new MoveNodesAction(before, SnapshotNodePositions(targets)));
                ScheduleSave();
                AppendLog($"Distributed {targets.Count} node(s) {distribution}");
            }
        }

        public void NotifyInfo(string message, string? title = null) => NotificationService.ShowInfo(message, title);
        public void NotifySuccess(string message, string? title = null) => NotificationService.ShowSuccess(message, title);
        public void NotifyWarning(string message, string? title = null) => NotificationService.ShowWarning(message, title);
        public void NotifyError(string message, string? title = null) => NotificationService.ShowError(message, title);

        public void SyncEditorToRuntimeNode()
        {
            StoryGraphLifecycleCoordinator.SyncEditorToRuntimeNode(EngineHost, Nodes, node => SelectNodeQuiet(node));
        }

        public async Task<bool> ConfirmDeleteSaveSlotAsync(int index)
        {
            return await EditorProjectLifecycleCoordinator.ConfirmDeleteSaveSlotAsync(TopLevelHint as Window, index);
        }

        public async Task<bool> ResolveUnsavedChangesAsync(Window window)
        {
            return await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
                window,
                () => SaveProjectNow(),
                () => IsProjectDirty,
                dirty => IsProjectDirty = dirty);
        }

        [RelayCommand]
        public async Task ImportAssetAsync()
        {
            try
            {
                if (EditorDialogService.GetMainWindow() == null) return;

                var files = await EditorDialogService.PickAssetFilesAsync();

                if (files != null && files.Count > 0)
                {
                    var filePaths = files.Select(f => f.Path.LocalPath);
                    EditorAssetImportService.ImportAssetFiles(filePaths, MainWindowViewModel.AssetsPath, AppendLog);
                    AssetBrowserViewModel.RefreshAssets();
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Failed to import asset: {ex.Message}");
            }
        }

        public void AppendLog(string message)
        {
            if (!Avalonia.Threading.Dispatcher.UIThread.CheckAccess())
            {
                Avalonia.Threading.Dispatcher.UIThread.Post(() => AppendLog(message));
                return;
            }
            LogOutput += $"[{DateTime.Now:HH:mm:ss}] {message}\n";
        }

        // Debounced save to avoid disk thrashing during drag operations
        private EditorUiTimer? _saveDebounceTimer;
        private EditorUiTimer? _enginePreviewDebounceTimer;
        private NodeViewModel? _pendingEnginePreviewNode;
        private bool _pendingEnginePreviewRequiresSelection = true;

        public void ScheduleEnginePreviewUpdate(NodeViewModel node, bool requireSelectedNode = true)
        {
            _pendingEnginePreviewNode = node;
            _pendingEnginePreviewRequiresSelection = requireSelectedNode;
            if (_enginePreviewDebounceTimer == null)
            {
                _enginePreviewDebounceTimer = new EditorUiTimer(
                    TimeSpan.FromMilliseconds(80),
                    () => DeliverScheduledEnginePreview());
            }
            _enginePreviewDebounceTimer.Start();
        }

        // Kept internal so the headless benchmark can measure the same delivery
        // branch without sleeping on the UI dispatcher debounce interval.
        internal bool DeliverScheduledEnginePreview()
        {
            var pending = _pendingEnginePreviewNode;
            bool requiresSelection = _pendingEnginePreviewRequiresSelection;
            _pendingEnginePreviewNode = null;
            if (IsInteractivelyDragging || pending == null ||
                (requiresSelection && pending != SelectedNode) || !EngineHost.IsInitialized)
                return false;
            return PushSceneToEngine(pending);
        }

        public void ScheduleSave()
        {
            IsProjectDirty = true;
            CrashRecoveryService.MarkDirty(AssetsJsonPath);
            if (!Settings.AutoSaveEnabled) return;
            if (_saveDebounceTimer == null)
            {
                _saveDebounceTimer = new EditorUiTimer(
                    TimeSpan.FromSeconds(Settings.AutoSaveIntervalSeconds),
                    SaveProject);
            }
            _saveDebounceTimer.Interval = TimeSpan.FromSeconds(Settings.AutoSaveIntervalSeconds);
            _saveDebounceTimer.Start();
        }

        [RelayCommand]
        public void ShowPanel(string panelName)
        {
            bool isHierarchy = IsHierarchyPanelVisible;
            bool isAssets = IsAssetsPanelVisible;
            bool isInspector = IsInspectorPanelVisible;
            bool isLog = IsLogPanelVisible;
            bool isBacklog = IsBacklogPanelVisible;
            bool isSaveSlots = IsSaveSlotsPanelVisible;
            bool isProjectIssues = IsProjectIssuesPanelVisible;
            int bottomActiveTab = BottomPanelActiveTab;
            bool isNodeGraph = IsNodeGraphActive;
            bool isPreview = IsPreviewActive;
            bool isEnginePreview = IsEnginePreviewActive;
            int splitMode = SplitScreenMode;

            EditorWorkspaceLayoutService.HandlePanelAction(
                panelName,
                ref isHierarchy,
                ref isAssets,
                ref isInspector,
                ref isLog,
                ref isBacklog,
                ref isSaveSlots,
                ref isProjectIssues,
                ref bottomActiveTab,
                ref isNodeGraph,
                ref isPreview,
                ref isEnginePreview,
                ref splitMode,
                () => SaveSlotsViewModel?.Refresh());

            // Faz 6 Dilim 2: eski kayıtlardan/eylemlerden gelen bayat
            // indeksler (ör. Varlıklar döneminden kalan 4) kelepçelenir.
            bottomActiveTab = EditorWorkspaceLayoutService.ClampBottomTab(bottomActiveTab);

            IsHierarchyPanelVisible = isHierarchy;
            IsAssetsPanelVisible = isAssets;
            IsInspectorPanelVisible = isInspector;
            IsLogPanelVisible = isLog;
            IsBacklogPanelVisible = isBacklog;
            IsSaveSlotsPanelVisible = isSaveSlots;
            IsProjectIssuesPanelVisible = isProjectIssues;
            BottomPanelActiveTab = bottomActiveTab;
            IsNodeGraphActive = isNodeGraph;
            IsPreviewActive = isPreview;
            IsEnginePreviewActive = isEnginePreview;
            SplitScreenMode = splitMode;
        }

        // COMPONENT MANAGEMENT
        /// <summary>
        /// Controls visibility of the "Add Component" dropdown menu in the Inspector.
        /// </summary>
        [ObservableProperty]
        private bool _isAddComponentMenuOpen;

        [RelayCommand]
        public void ShowAddComponentMenu()
        {
            IsAddComponentMenuOpen = !IsAddComponentMenuOpen;
        }

        /// <summary>
        /// Adds a new component of the specified type to the currently selected GameObject in Inspector.
        /// </summary>
        [RelayCommand]
        public void AddComponentByType(string typeKey)
        {
            if (SelectedNode == null || string.IsNullOrEmpty(typeKey)) return;

            var targetObj = HierarchyViewModel?.SelectedObject ?? SelectedNode.Objects.FirstOrDefault();
            bool targetCreated = (targetObj == null);

            var component = EditorComponentService.AddComponent(
                SelectedNode,
                targetObj,
                typeKey,
                AppendLog);

            if (component != null)
            {
                if (targetCreated && HierarchyViewModel != null && component.OwnerObject != null)
                {
                    HierarchyViewModel.SelectedObject = component.OwnerObject;
                }
                if (component.OwnerObject != null)
                {
                    UndoRedoService.Instance.RecordAction(new ComponentAddAction(
                        component.OwnerObject, component,
                        component.OwnerObject.Components.IndexOf(component)));
                }
                IsAddComponentMenuOpen = false;
                ScheduleSave();

                // Push updated scene to engine so changes are visible immediately
                if (EngineHost.IsInitialized)
                    PushSceneToEngine(SelectedNode);
            }
        }

        /// <summary>
        /// Removes a specific component from its parent GameObject.
        /// </summary>
        [RelayCommand]
        public void RemoveComponent(NodeComponentViewModel? component)
        {
            if (SelectedNode == null || component == null) return;
            var owner = component.OwnerObject;
            int index = owner?.Components.IndexOf(component) ?? -1;
            if (EditorComponentService.RemoveComponent(SelectedNode, component, AppendLog))
            {
                if (owner != null && index >= 0)
                {
                    UndoRedoService.Instance.RecordAction(new ComponentRemoveAction(owner, component, index));
                }
                ScheduleSave();
                if (EngineHost.IsInitialized)
                    PushSceneToEngine(SelectedNode);
            }
        }

        /// <summary>
        /// Copies an external image file into Assets/images/ if it is not already in the project,
        /// and returns the local relative filename.
        /// </summary>
        public string ImportImageFileToProject(string fullPath)
        {
            return EditorAssetImportService.ImportImageFile(fullPath, MainWindowViewModel.AssetsPath, AppendLog);
        }

        /// <summary>
        /// Copies an external audio file into Assets/audio/ if it is not already in the project,
        /// and returns the local relative filename.
        /// </summary>
        public string ImportAudioFileToProject(string fullPath)
        {
            return EditorAssetImportService.ImportAudioFile(fullPath, MainWindowViewModel.AssetsPath, AppendLog);
        }

        /// <summary>
        /// Opens an OS file picker dialog to let the user select an image file for a visual component.
        /// Automatically copies external images into Assets/images/ for project portability.
        /// </summary>
        [RelayCommand]
        public async Task SelectImageForComponentAsync(NodeComponentViewModel? component)
        {
            var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
            await EditorVisualAssetPickerService.SelectImageForComponentAsync(
                component,
                window,
                MainWindowViewModel.AssetsPath,
                ImportImageFileToProject,
                AssetBrowserViewModel.RefreshAssets,
                ScheduleSave,
                () =>
                {
                    if (EngineHost.IsInitialized && SelectedNode != null)
                        PushSceneToEngine(SelectedNode);
                },
                AppendLog);
        }

        /// <summary>
        /// Opens an OS file picker dialog to let the user select an audio file for an audio component.
        /// Automatically copies external audio into Assets/audio/ for project portability.
        /// </summary>
        [RelayCommand]
        public async Task SelectAudioForComponentAsync(AudioComponentViewModel? component)
        {
            var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
            await EditorAudioAssetPickerService.SelectAudioForComponentAsync(
                component,
                "bgm",
                window,
                MainWindowViewModel.AssetsPath,
                ImportAudioFileToProject,
                AssetBrowserViewModel.RefreshAssets,
                ScheduleSave,
                AppendLog);
        }

        /// <summary>
        /// Moves a component up in the render order.
        /// </summary>
        [RelayCommand]
        public void MoveComponentUp(NodeComponentViewModel? component)
        {
            if (EditorComponentService.MoveComponentUp(SelectedNode, component))
            {
                ScheduleSave();
            }
        }

        /// <summary>
        /// Moves a component down in the render order.
        /// </summary>
        [RelayCommand]
        public void MoveComponentDown(NodeComponentViewModel? component)
        {
            if (EditorComponentService.MoveComponentDown(SelectedNode, component))
            {
                ScheduleSave();
            }
        }

        [ObservableProperty]
        private bool _isSnapAssistEnabled = true;

        /// <summary>
        /// Toggles magnetic snapping assist in Edit Frame.
        /// </summary>
        [RelayCommand]
        public void ToggleSnapAssist()
        {
            IsSnapAssistEnabled = !IsSnapAssistEnabled;
            AppendLog($"Snap Assist: {(IsSnapAssistEnabled ? "AÇIK (ENABLED)" : "KAPALI (DISABLED)")}");
        }

        /// <summary>
        /// OBS-style Assist: Fits/Stretches background to 1920x1080 canvas.
        /// </summary>
        [RelayCommand]
        public void FitBackgroundToScreen()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.FitBackgroundToScreen(SelectedNode);
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
            AppendLog("OBS Assist: Arka plan 1920x1080 ekrana tam oturtuldu (Fitted to Screen)");
        }

        [RelayCommand]
        public void CenterSelectedElement()
        {
            if (SelectedNode == null) return;
            if (EditorLayoutAssistService.CenterSelectedElement(SelectedNode, out string desc))
            {
                AppendLog($"OBS Assist: {desc}");
            }
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
        }

        [RelayCommand]
        public void AlignCharacterToBottom()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.AlignCharacterToBottom(SelectedNode);
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
            AppendLog("OBS Assist: Karakterler zemin hizasına oturtuldu (Ground Baseline)");
        }

        [RelayCommand]
        public void ResetCharacterSize(CharacterComponentViewModel? charComp)
        {
            if (SelectedNode == null && charComp == null) return;
            EditorLayoutAssistService.ResetCharacterSize(SelectedNode, charComp);
            ScheduleSave();
            if (EngineHost.IsInitialized && SelectedNode != null)
                PushSceneToEngine(SelectedNode);
            AppendLog("OBS Assist: Karakter boyutu standart orana sıfırlandı (600x900)");
        }

        [RelayCommand]
        public void PresetDialogueBox(string preset)
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.PresetDialogueBox(SelectedNode, preset);
            if (preset == "BottomBanner")
                AppendLog("OBS Assist: Diyalog kutusu alt banner olarak ayarlandı (1720x220)");
            else if (preset == "Center")
                AppendLog("OBS Assist: Diyalog kutusu merkeze hizalandı");
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
        }

        [RelayCommand]
        public void ResetRotation()
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.ResetSceneRotation(SelectedNode);
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
            AppendLog("OBS Assist: Sahne rotasyonları sıfırlandı (Rotation Reset)");
        }
        // PROJECT MANAGEMENT & BUILD EXPORT PIPELINE

        /// <summary>
        /// Explicitly saves the current project (graphs, node layouts, and configs).
        /// </summary>
        // MS-2 save sequencing: every save (sync or background) takes the next
        // monotonic number. A background write aborts when a newer save was
        // scheduled meanwhile, so an old save can never overwrite a new one.
        // Only the completion of the LATEST sequence clears the dirty flag.
        private long _saveSequence;
        private long _saveCompletedSequence;

        /// <summary>
        /// UI-thread save entry point (toolbar command, autosave timer). Captures
        /// a detached snapshot synchronously, then serializes and writes off the
        /// UI thread. Never blocks the caller.
        /// </summary>
        [RelayCommand]
        public void SaveProject()
        {
            _saveDebounceTimer?.Stop();
            _ = SaveProjectAsync();
        }

        /// <summary>
        /// Synchronous save for flows that require completion (build, save-as,
        /// unsaved-changes resolve, shutdown). Runs capture + write inline.
        /// </summary>
        public bool SaveProjectNow()
        {
            _saveDebounceTimer?.Stop();
            long sequence = Interlocked.Increment(ref _saveSequence);
            StoryGraphSaveSnapshot snapshot;
            try
            {
                snapshot = StoryGraphSaveService.Capture(
                    Nodes, Connections, GetStartNode()?.Id ?? 101, SelectedNode,
                    CurrentStructure());
            }
            catch (Exception ex)
            {
                AppendLog($"Kayıt anlık görüntüsü alınamadı: {ex.Message}");
                return false;
            }

            bool written = CrashRecoveryService.TryWriteSnapshotWithRecovery(
                snapshot, AssetsJsonPath, sequence, () => Volatile.Read(ref _saveSequence), AppendLog);
            if (written && sequence == Volatile.Read(ref _saveSequence))
            {
                Volatile.Write(ref _saveCompletedSequence, sequence);
                IsProjectDirty = false;
                AppendLog($"[PROJE KAYDEDİLDİ] {Nodes.Count} düğüm ve tüm bileşenler başarıyla kaydedildi ({DateTime.Now:HH:mm:ss})");
            }
            return written;
        }

        /// <summary>
        /// Background save. The snapshot read MUST stay on the UI thread (live
        /// view models); only serialization + disk I/O leave it.
        /// </summary>
        public Task SaveProjectAsync()
        {
            StoryGraphSaveSnapshot snapshot;
            try
            {
                snapshot = StoryGraphSaveService.Capture(
                    Nodes, Connections, GetStartNode()?.Id ?? 101, SelectedNode,
                    CurrentStructure());
            }
            catch (Exception ex)
            {
                AppendLog($"Kayıt anlık görüntüsü alınamadı: {ex.Message}");
                return Task.CompletedTask;
            }

            long sequence = Interlocked.Increment(ref _saveSequence);
            int nodeCount = Nodes.Count;
            string assetsJsonPath = AssetsJsonPath;
            return Task.Run(() =>
            {
                bool written = false;
                try
                {
                    written = CrashRecoveryService.TryWriteSnapshotWithRecovery(
                        snapshot, assetsJsonPath, sequence, () => Volatile.Read(ref _saveSequence), null);
                }
                catch (Exception ex)
                {
                    string message = ex.Message;
                    Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                        AppendLog($"Arka plan kayıt başarısız: {message}"));
                    return;
                }
                Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                {
                    if (sequence != Volatile.Read(ref _saveSequence))
                        return; // superseded: a newer save owns the dirty flag now
                    if (sequence > Volatile.Read(ref _saveCompletedSequence))
                        Volatile.Write(ref _saveCompletedSequence, sequence);
                    if (written)
                    {
                        IsProjectDirty = false;
                        AppendLog($"[PROJE KAYDEDİLDİ] {nodeCount} düğüm ve tüm bileşenler başarıyla kaydedildi ({DateTime.Now:HH:mm:ss})");
                    }
                });
            });
        }

        /// <summary>Test hook: latest scheduled save sequence (monotonic).</summary>
        internal long SaveSequenceForTests => Volatile.Read(ref _saveSequence);

        /// <summary>
        /// Opens the project root folder in the system file manager.
        /// </summary>
        [RelayCommand]
        public void OpenProjectFolder()
        {
            EditorProjectLifecycleCoordinator.OpenProjectFolder(ProjectRoot, AppendLog);
        }

        /// <summary>
        /// Opens a previously saved project from a folder selected by the user.
        /// Looks for Assets/json/full_story_graph.json or Assets/full_story_graph.json.
        /// </summary>
        [RelayCommand]
        public async Task OpenProjectAsync()
        {
            try
            {
                var window = EditorDialogService.GetMainWindow();
                if (window == null) return;
                if (!await ResolveUnsavedChangesAsync(window)) return;

                string? selectedDir = await EditorDialogService.PickFolderAsync(
                    "Proje Klasörünü Seçin (Assets/ içeren klasör)");

                if (selectedDir == null)
                {
                    AppendLog("Proje açma iptal edildi.");
                    return;
                }

                var result = EditorProjectLifecycleCoordinator.ExecuteOpenProject(
                    selectedDir,
                    ProjectRoot,
                    CurrentProjectPath,
                    AssetBitmapCache.Clear,
                    (root, path) =>
                    {
                        ProjectRoot = root;
                        CurrentProjectPath = path;
                    },
                    EngineHost.SetProjectDirectory,
                    LoadFullStoryGraphFile,
                    () =>
                    {
                        foreach (var node in Nodes) node.RefreshBitmaps();
                    },
                    AssetBrowserViewModel.RefreshAssets,
                    AppendLog);

                if (result.Succeeded)
                {
                    ProjectRuntimeSettings = ProjectRuntimeSettingsService.Load(Path.Combine(ProjectRoot, "project.rowlproj"));
                    LoadProjectRuntimeSettingsIntoEditor();
                    SaveSlotsViewModel.Refresh();
                    AppendLog($"   {Nodes.Count} düğüm, {Connections.Count} bağlantı yüklendi.");

                    // Select first node if available
                    if (Nodes.Count > 0)
                        SelectedNode = Nodes[0];
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Proje açma hatası: {ex.Message}");
            }
        }

        /// <summary>
        /// Saves a full copy of the project into a new folder chosen by the user (Save As / Farklı Kaydet).
        /// Includes all Assets, story graph json files, and a project descriptor.
        /// </summary>
        [RelayCommand]
        public async Task SaveProjectAsAsync()
        {
            try
            {
                if (EditorDialogService.GetMainWindow() == null) return;

                string? selectedDir = await EditorDialogService.PickFolderAsync(
                    "Projeyi Farklı Kaydet (Hedef Klasör Seçin)");

                if (selectedDir != null)
                {
                    string targetDir = EditorProjectLifecycleCoordinator.GenerateSaveAsTargetDirectory(selectedDir);
                    SaveProjectToDirectory(targetDir);
                }
                else
                {
                    AppendLog("Farklı kaydetme iptal edildi.");
                    return;
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Farklı kaydetme hatası: {ex.Message}");
            }
        }

        /// <summary>
        /// Helper to copy the entire project and assets to a target directory.
        /// </summary>
        public void SaveProjectToDirectory(string targetDir)
        {
            EditorProjectLifecycleCoordinator.ExecuteSaveAs(
                ProjectRoot,
                targetDir,
                Nodes.Count,
                GetStartNode()?.Id ?? 101,
                () => SaveProjectNow(),
                AppendLog);
        }

        /// <summary>
        /// Builds a standalone, playable game distribution package for PC (Linux/Windows/macOS).
        /// Packages binary engine, VFS assets, story graphs, and generates a run launcher.
        /// </summary>
        [RelayCommand]
        public async Task BuildGameAsync()
        {
            if (IsBuilding) return;
            try
            {
                var window = EditorDialogService.GetMainWindow();

                string defaultBuildDir = ProjectFileSystem.GetDefaultBuildDirectory();

                string buildOutDir = defaultBuildDir;
                if (window != null)
                {
                    string? pickedDir = await EditorDialogService.PickFolderAsync(
                        "Build Al: Dağıtım / Çıktı Klasörünü Seçin");

                    if (pickedDir != null)
                    {
                        buildOutDir = pickedDir;
                    }
                    else
                    {
                        AppendLog("Build işlemi iptal edildi.");
                        return;
                    }
                }

                IsBuilding = true;
                _buildCancellation = new CancellationTokenSource();
                await EditorBuildCoordinator.BuildStandaloneGameAsync(
                    ProjectRoot,
                    AssetsPath,
                    buildOutDir,
                    Nodes,
                    Connections,
                    GetStartNode()?.Id,
                    () => SaveProjectNow(),
                    issues =>
                    {
                        ProjectIssuesViewModel.SetIssues(issues);
                        if (issues.Any(issue => issue.IsError))
                        {
                            IsProjectIssuesPanelVisible = true;
                            BottomPanelActiveTab = 3;
                        }
                    },
                    AppendLog,
                    msg => BuildProgress = msg,
                    _buildCancellation.Token,
                    diagnostic => NotificationService.ReportBuildDiagnostic(diagnostic, AppendLog));
            }
            catch (Exception ex)
            {
                AppendLog($"Build işlemi sırasında hata oluştu: {ex.Message}");
            }
            finally
            {
                _buildCancellation?.Dispose();
                _buildCancellation = null;
                IsBuilding = false;
            }
        }

        [RelayCommand]
        public void CancelBuild() => _buildCancellation?.Cancel();

        /// <summary>
        /// Executes the complete standalone build pipeline.
        /// </summary>
        public void ExecuteBuildPipeline(string buildOutDir)
        {
            EditorBuildCoordinator.ExecuteBuildPipeline(
                MainWindowViewModel.ProjectRoot,
                MainWindowViewModel.AssetsPath,
                buildOutDir,
                Nodes,
                Connections,
                GetStartNode()?.Id,
                () => { SaveActiveStoryFile(); SaveFullStoryGraphFile(); },
                ProjectIssuesViewModel.SetIssues,
                AppendLog,
                diagnostic => NotificationService.ReportBuildDiagnostic(diagnostic, AppendLog));
        }

        /// <summary>
        /// Faz 3 Dilim 4 — opens the Translation Desk. One-line delegation;
        /// all desk logic lives in the Localization services and ViewModel.
        /// </summary>
        [RelayCommand]
        public void OpenLocalizationDesk() =>
            LocalizationDeskCoordinator.OpenDesk(ProjectRoot);

        /// <summary>
        /// Faz 4 Dilim 5 — runs Validate+Lint off the UI thread (single disk
        /// scan, worker-serialized) and posts the merged issues back. The
        /// dirty flag is untouched: analysis never schedules a save.
        /// </summary>
        [RelayCommand]
        public void AnalyzeStoryGraph()
        {
            var nodes = Nodes.ToList();
            var connections = Connections.ToList();
            var startId = GetStartNode()?.Id;
            var structure = CurrentStructure();
            string assetsPath = AssetsPath;
            AppendLog("Ara: [GRAPH LINT] Arka plan taraması başladı...");
            _ = Task.Run(() =>
            {
                IReadOnlyList<ProjectValidationIssue> issues;
                try
                {
                    issues = AssetScanner.Invoke(() =>
                        ProjectLintService.Lint(nodes, connections, assetsPath, startId, structure));
                }
                catch (Exception ex)
                {
                    issues = new[] { new ProjectValidationIssue(false, $"Lint kesintiye uğradı: {ex.Message}") };
                }
                Avalonia.Threading.Dispatcher.UIThread.Post(() => ApplyLintResults(issues));
            });
        }

        private void ApplyLintResults(IReadOnlyList<ProjectValidationIssue> issues)
        {
            ProjectIssuesViewModel.SetIssues(issues);
            IsProjectIssuesPanelVisible = true;
            BottomPanelActiveTab = 3;
            if (issues.Count == 0) AppendLog("[GRAPH CHECK] No blocking asset or route issues found.");
            foreach (var issue in issues)
                AppendLog($"{(issue.IsError ? "Hata" : "Uyarı")} [GRAPH CHECK] {issue.Message}");
        }

        /// <summary>
        /// Packages project assets into a single .rowlpkg binary archive file.
        /// Opens a folder picker so the user can choose the output directory.
        /// </summary>
        [RelayCommand]
        public async Task BuildPackageAsync()
        {
            if (IsBuilding) return;
            try
            {
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;

                // Default output directory
                string defaultOutDir = Path.Combine(MainWindowViewModel.AssetsPath, "packages");
                string outDir = defaultOutDir;

                // Let user choose output directory
                if (window != null)
                {
                    var folders = await window.StorageProvider.OpenFolderPickerAsync(new Avalonia.Platform.Storage.FolderPickerOpenOptions
                    {
                        Title = "Paket (.rowlpkg) Çıktı Klasörünü Seçin",
                        AllowMultiple = false
                    });

                    if (folders != null && folders.Count > 0)
                    {
                        outDir = folders[0].Path.LocalPath;
                    }
                    else
                    {
                        AppendLog("Paket oluşturma iptal edildi.");
                        return;
                    }
                }

                IsBuilding = true;
                BuildProgress = "Paketleme başlatılıyor...";
                _buildCancellation = new CancellationTokenSource();
                var result = await EditorBuildCoordinator.PackageAssetsAsync(
                    MainWindowViewModel.AssetsPath,
                    outDir,
                    AppendLog,
                    _buildCancellation.Token,
                    diagnostic => NotificationService.ReportBuildDiagnostic(diagnostic, AppendLog));
                if (result.Succeeded)
                {
                    AssetBrowserViewModel.RefreshAssets();
                    NotificationService.ShowSuccess("Asset paketi başarıyla oluşturuldu.", "Paketleme");
                }
            }
            catch (Exception ex)
            {
                NotificationService.ReportBuildDiagnostic(new BuildDiagnostic(
                    BuildDiagnosticCode.IoFailure,
                    BuildDiagnosticSeverity.Error,
                    "package_assets",
                    ex.Message,
                    MainWindowViewModel.AssetsPath,
                    Detail: ex.ToString()), AppendLog);
            }
            finally
            {
                _buildCancellation?.Dispose();
                _buildCancellation = null;
                IsBuilding = false;
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            // Faz 4 Dilim 5 — a pending debounce save OR a crash-flag left by
            // ScheduleSave means unsaved edits exist; flush them synchronously.
            bool hasPendingSave = _saveDebounceTimer?.IsEnabled == true ||
                CrashRecoveryService.IsDirty(AssetsJsonPath);
            _saveDebounceTimer?.Stop();
            _enginePreviewDebounceTimer?.Stop();
            _smoothTimer.Stop();

            if (hasPendingSave)
            {
                // Dirty-flag flush (replaces the old IsEnabled-only save):
                // only a fully written pair clears the crash marker.
                bool activeSaved = SaveActiveStoryFile();
                bool fullSaved = SaveFullStoryGraphFile();
                if (activeSaved && fullSaved)
                    CrashRecoveryService.ClearDirty(AssetsJsonPath);
            }

            EngineHost.Dispose();
            AssetBitmapCache.Clear();
            try { AssetScanner.Dispose(); } catch { }
            try { AssetBrowserViewModel.Dispose(); } catch { }
        }
    }
}
