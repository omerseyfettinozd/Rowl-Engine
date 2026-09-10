using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
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
        // ── Centralized path helpers ──
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
            string dir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location) ?? ".";
            // Walk up to 6 levels looking for the canonical project root.
            // Strategy: prefer the parent that contains BOTH Assets/ AND editor/.
            // editor/ itself may also have an Assets/ stub, so skip up if Assets/
            // appears inside editor/ sub-tree.
            string? best = null;
            for (int i = 0; i < 6; i++)
            {
                bool hasAssets = Directory.Exists(Path.Combine(dir, "Assets"));
                bool hasEditor = Directory.Exists(Path.Combine(dir, "editor")) ||
                                 File.Exists(Path.Combine(dir, "CMakeLists.txt"));
                // Prefer the directory that has BOTH Assets and editor/ or CMakeLists.txt
                if (hasAssets && hasEditor)
                {
                    best = dir;
                    // Keep going up — parent may also qualify (repo root is the highest match)
                }

                var parent = Directory.GetParent(dir);
                if (parent == null) break;
                dir = parent.FullName;
            }
            // Fallback: any dir with Assets/ found along the way
            if (best == null)
            {
                dir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location) ?? ".";
                for (int i = 0; i < 6; i++)
                {
                    if (Directory.Exists(Path.Combine(dir, "Assets")))
                        return dir;
                    var parent = Directory.GetParent(dir);
                    if (parent == null) break;
                    dir = parent.FullName;
                }
            }
            return best ?? Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location) ?? ".";
        }

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

        [ObservableProperty]
        private string _currentBuildTarget = "Linux";

        [ObservableProperty] private bool _isBuilding;
        [ObservableProperty] private string _buildProgress = "";
        private CancellationTokenSource? _buildCancellation;

        public string BuildButtonText => $"🚀 {CurrentBuildTarget} Build";
        public string BuildButtonTooltip => $"{CurrentBuildTarget} için Bağımsız Oyun Çıktısı Üret (Ctrl+B)";
        public string BuildTargetDisplayText => $"🎯 {CurrentBuildTarget} ▾";

        [RelayCommand]
        private void SetBuildTarget(string target)
        {
            CurrentBuildTarget = target;
            OnPropertyChanged(nameof(BuildButtonText));
            OnPropertyChanged(nameof(BuildButtonTooltip));
            OnPropertyChanged(nameof(BuildTargetDisplayText));
        }

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
                Toast.Show($"↩ Geri alındı: {UndoRedo.UndoDescription}", ToastType.Info, 1500);
            }
        }

        [RelayCommand]
        public void Redo()
        {
            if (UndoRedo.CanRedo)
            {
                UndoRedo.Redo();
                Toast.Show($"↪ Yinelendi: {UndoRedo.RedoDescription}", ToastType.Info, 1500);
            }
        }

        // ── Hızlı Arama (Quick Search) ───────────────────────────────
        [ObservableProperty]
        private bool _isSearchVisible = false;

        [ObservableProperty]
        private string _searchQuery = "";

        partial void OnSearchQueryChanged(string value)
        {
            var match = EditorWorkspaceLayoutService.FindMatchingNode(Nodes, value);
            if (match != null)
            {
                SelectedNode = match;
                var (targetX, targetY) = EditorWorkspaceLayoutService.CalculatePanTargetForNode(match, ZoomScale);
                TargetPanX = targetX;
                TargetPanY = targetY;
                StartSmoothViewAnimation();
            }
        }

        [RelayCommand]
        private void ToggleSearch()
        {
            IsSearchVisible = !IsSearchVisible;
            if (!IsSearchVisible) SearchQuery = "";
        }

        // ── Tam Ekran ─────────────────────────────────────────────────
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
        /// The bottom area remains available while either of its independent
        /// tabs is enabled. Assets must not disappear merely because the log
        /// panel was closed.
        /// </summary>
        public bool IsBottomPanelVisible => IsLogPanelVisible || IsAssetsPanelVisible || IsBacklogPanelVisible || IsSaveSlotsPanelVisible || IsProjectIssuesPanelVisible;
        public GridLength BottomPanelHeight => EditorWorkspaceLayoutService.CalculateBottomPanelHeight(IsBottomPanelVisible);
        public GridLength BottomSplitterHeight => EditorWorkspaceLayoutService.CalculateBottomSplitterHeight(IsBottomPanelVisible);
        public GridLength HierarchyPanelWidth => EditorWorkspaceLayoutService.CalculateHierarchyPanelWidth(IsHierarchyPanelVisible);
        public GridLength HierarchySplitterWidth => EditorWorkspaceLayoutService.CalculateHierarchySplitterWidth(IsHierarchyPanelVisible);
        public GridLength InspectorPanelWidth => EditorWorkspaceLayoutService.CalculateInspectorPanelWidth(IsInspectorPanelVisible);
        public GridLength InspectorSplitterWidth => EditorWorkspaceLayoutService.CalculateInspectorSplitterWidth(IsInspectorPanelVisible);

        partial void OnIsAssetsPanelVisibleChanged(bool value) =>
            NotifyBottomPanelLayoutChanged();

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
        /// Active tab index in the bottom panel: 0 = Log, 1 = Assets
        /// </summary>
        [ObservableProperty]
        private int _bottomPanelActiveTab = 0;

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

        public string SplitScreenButtonText => SplitScreenMode > 0 ? $"⇱ ⇲ Split: {(SplitScreenMode == 1 ? "H" : "V")}" : "⊞ Split Screen";
        public string SplitScreenButtonColor => SplitScreenMode > 0 ? "#2563EB" : "#1E293B";
        public string SplitScreenButtonForeground => SplitScreenMode > 0 ? "White" : "#94A3B8";

        partial void OnSplitScreenModeChanged(int value)
        {
            OnPropertyChanged(nameof(IsSplitScreenOff));
            OnPropertyChanged(nameof(IsSplitScreenHorizontal));
            OnPropertyChanged(nameof(IsSplitScreenVertical));
            OnPropertyChanged(nameof(SplitScreenButtonText));
            OnPropertyChanged(nameof(SplitScreenButtonColor));
            OnPropertyChanged(nameof(SplitScreenButtonForeground));
        }

        public string ConnectButtonColor => IsConnected ? "#2563EB" : "#64748B";

        [ObservableProperty]
        private bool _isDarkMode = true;

        public string ThemeButtonText => IsDarkMode ? "🌙 Karanlık Mod" : "☀️ Aydınlık Mod";
        public string ThemeButtonColor => IsDarkMode ? "#2A2A3D" : "#FFEDD5";
        public string ThemeButtonForeground => IsDarkMode ? "#F8FAFC" : "#EA580C";

        [RelayCommand]
        public void ToggleTheme()
        {
            IsDarkMode = !IsDarkMode;
            if (Avalonia.Application.Current != null)
            {
                Avalonia.Application.Current.RequestedThemeVariant = IsDarkMode 
                    ? Avalonia.Styling.ThemeVariant.Dark 
                    : Avalonia.Styling.ThemeVariant.Light;
            }
            OnPropertyChanged(nameof(ThemeButtonText));
            OnPropertyChanged(nameof(ThemeButtonColor));
            OnPropertyChanged(nameof(ThemeButtonForeground));
            AppendLog($"🎨 Tema değiştirildi: {(IsDarkMode ? "Karanlık Mod (Siyah-Beyaz)" : "Aydınlık Mod (Turuncu-Beyaz, Siyah Yazı)")}");
        }

        private readonly DispatcherTimer _smoothTimer;

        private NodeViewModel? _wireDragSourceNode;
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
        public LivePreviewViewModel LivePreviewViewModel { get; }
        public HierarchyViewModel HierarchyViewModel { get; }

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
                StatusText = $"✅ Proje yüklendi: {Path.GetFileName(projectPath)}";
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
            InspectorViewModel = new InspectorViewModel(this);
            NodeGraphViewModel = new NodeGraphViewModel(this);
            LivePreviewViewModel = new LivePreviewViewModel(this);
            HierarchyViewModel = new HierarchyViewModel(this);

            _smoothTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(16)
            };
            _smoothTimer.Tick += (s, e) => SmoothUpdateStep();

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

            // Embedded engine: initialize directly with isolated project VFS
            if (connectEngine)
                _ = ConnectEngineAsync();
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
            WireStartPoint = new Point(sourceNode.X + 265, sourceNode.Y + sourceNode.GetOutputPortY(optionId));
            WireEndPoint = pinPos;
            IsDraggingWire = true;
            AppendLog($"Started drawing wire from Green Output Pin of Node #{sourceNode.Id}...");
        }

        public void StartUnplugWireDrag(NodeViewModel sourceNode, Point mousePos, string optionId = "")
        {
            _wireDragSourceNode = sourceNode;
            _wireDragOptionId = optionId;
            WireStartPoint = new Point(sourceNode.X + 265, sourceNode.Y + sourceNode.GetOutputPortY(optionId));
            WireEndPoint = mousePos;
            IsDraggingWire = true;
            AppendLog($"✂️ Unplugged cable from Node #{sourceNode.Id}, re-routing wire...");
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

            var newConn = StoryGraphCanvasService.TryConnectWire(
                _wireDragSourceNode,
                releasePos,
                Nodes,
                Connections,
                _wireDragOptionId);

            if (newConn != null)
            {
                AppendLog($"✅ Connected Wire: Node #{_wireDragSourceNode.Id} ---> Node #{newConn.TargetNode?.Id} (Total cables: {Connections.Count})");
            }
            else
            {
                AppendLog("✂️ Connection dropped in empty space (cable unplugged / removed).");
            }

            UpdateStartNodeState();
            _wireDragSourceNode = null;
            _wireDragOptionId = string.Empty;
        }

        public void DisconnectNodeInputs(NodeViewModel node)
        {
            int count = StoryGraphCanvasService.DisconnectNodeInputs(node, Connections);
            if (count > 0)
            {
                AppendLog($"✂️ Disconnected {count} incoming cable(s) from Node #{node.Id}");
            }
        }

        public void DisconnectNodeOutputs(NodeViewModel node, string optionId = "")
        {
            int count = StoryGraphCanvasService.DisconnectNodeOutputs(node, Connections, optionId);
            if (count > 0)
            {
                AppendLog($"✂️ Disconnected {count} outgoing cable(s) from Node #{node.Id}");
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
            int count = StoryGraphCanvasService.DisconnectAllNodeCables(node, Connections, SetChoiceTarget);
            if (count > 0)
            {
                AppendLog($"✂️ Disconnected all {count} cable(s) attached to Node #{node.Id}");
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
            StoryGraphCanvasService.DeleteNode(node, Nodes, Connections, SetChoiceTarget);
            AppendLog($"🗑️ Deleted Node #{node.Id} ({node.Title})");
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
            UpdateStartNodeState();
            AppendLog($"✨ Added new node #{nextId} at visible screen center ({spawnX:F0}, {spawnY:F0})");
        }

        [RelayCommand]
        public Task ConnectEngineAsync()
        {
            StatusText = "Initializing embedded C++ Engine...";
            AppendLog("[Engine] Starting embedded RowlEngineCore library...");

            // Native engine calls, framebuffer access and the DispatcherTimer must
            // remain on the UI thread for the lifetime of this host.
            bool success = EngineHost.Initialize(1920, 1080, true);
            IsConnected = success;

            if (success)
            {
                EngineHost.SetProjectDirectory(ProjectRoot);
                ApplyPlayerSettingsToEngine();
                StatusText = "Engine Ready — Embedded C++ Runtime Active";
                AppendLog($"[Engine] RowlEngineCore mounted isolated project: {ProjectRoot}");

                // Push the currently selected node to the engine immediately
                if (SelectedNode != null)
                    PushSceneToEngine(SelectedNode);
            }
            else
            {
                StatusText = "Engine Init Failed — Check that libRowlEngineCore.so is built.";
                AppendLog("[Engine] RowlEngineCore initialization failed. Run: cmake --build build");
            }

            return Task.CompletedTask;
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
            return EditorSceneSyncService.PushSceneToEngine(EngineHost, node, msg => AppendLog(msg));
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
                SelectNodeQuiet,
                AppendLog);
        }

        public bool SaveFullStoryGraphFile()
        {
            return StoryGraphLifecycleCoordinator.SaveFullGraph(
                AssetsPath,
                AssetsJsonPath,
                Nodes,
                Connections,
                GetStartNode()?.Id,
                AppendLog);
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
            string graphPath = System.IO.Path.Combine(AssetsJsonPath, "full_story_graph.json");
            if (System.IO.File.Exists(graphPath))
                EngineHost.LoadStoryGraph(graphPath);

            AppendLog($"[Hot-Reload] Scene pushed directly to engine (P/Invoke) — Node #{SelectedNode.Id}");
            IsConnected = true;
        }

        [ObservableProperty]
        private bool _isPlayingStandalone = false;

        [ObservableProperty]
        private string _playButtonText = "▶ Play";

        [ObservableProperty]
        private string _playButtonColor = "#16A34A";

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
                PlayButtonText = "⏹ Stop";
                PlayButtonColor = "#DC2626";
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
            PlayButtonText = "▶ Play";
            PlayButtonColor = "#16A34A";
            StatusText = "Engine Ready — Offscreen C++ Runtime Active";
        }

        public void SelectNode(NodeViewModel node)
        {
            if (SelectedNode != null) SelectedNode.IsSelected = false;
            SelectedNode = node;
            SelectedNode.IsSelected = true;
            ScheduleSave();
            AppendLog($"Selected Node #{node.Id} ({node.Title})");
        }

        /// <summary>
        /// Selects a node without triggering debounced file saves (used during gameplay for zero-latency node advance).
        /// </summary>
        public void SelectNodeQuiet(NodeViewModel node)
        {
            if (SelectedNode != null) SelectedNode.IsSelected = false;
            SelectedNode = node;
            SelectedNode.IsSelected = true;
            AppendLog($"Selected Node #{node.Id} ({node.Title})");
        }

        public void SyncEditorToRuntimeNode()
        {
            StoryGraphLifecycleCoordinator.SyncEditorToRuntimeNode(EngineHost, Nodes, SelectNodeQuiet);
        }

        public async Task<bool> ConfirmDeleteSaveSlotAsync(int index)
        {
            return await EditorProjectLifecycleCoordinator.ConfirmDeleteSaveSlotAsync(TopLevelHint as Window, index);
        }

        public async Task<bool> ResolveUnsavedChangesAsync(Window window)
        {
            return await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
                window,
                SaveProject,
                () => IsProjectDirty,
                dirty => IsProjectDirty = dirty);
        }

        [RelayCommand]
        public async Task ImportAssetAsync()
        {
            try
            {
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
                if (window == null) return;

                var files = await window.StorageProvider.OpenFilePickerAsync(new Avalonia.Platform.Storage.FilePickerOpenOptions
                {
                    Title = "Import Asset Files into Rowl Engine Project",
                    AllowMultiple = true
                });

                if (files != null && files.Count > 0)
                {
                    var filePaths = files.Select(f => f.Path.LocalPath);
                    EditorAssetImportService.ImportAssetFiles(filePaths, MainWindowViewModel.AssetsPath, AppendLog);
                    AssetBrowserViewModel.RefreshAssets();
                }
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Failed to import asset: {ex.Message}");
            }
        }

        public void AppendLog(string message)
        {
            LogOutput += $"[{DateTime.Now:HH:mm:ss}] {message}\n";
        }

        // Debounced save to avoid disk thrashing during drag operations
        private DispatcherTimer? _saveDebounceTimer;
        private DispatcherTimer? _enginePreviewDebounceTimer;
        private NodeViewModel? _pendingEnginePreviewNode;
        private bool _pendingEnginePreviewRequiresSelection = true;

        public void ScheduleEnginePreviewUpdate(NodeViewModel node, bool requireSelectedNode = true)
        {
            _pendingEnginePreviewNode = node;
            _pendingEnginePreviewRequiresSelection = requireSelectedNode;
            if (_enginePreviewDebounceTimer == null)
            {
                _enginePreviewDebounceTimer = new DispatcherTimer
                {
                    Interval = TimeSpan.FromMilliseconds(80)
                };
                _enginePreviewDebounceTimer.Tick += (_, _) =>
                {
                    _enginePreviewDebounceTimer.Stop();
                    DeliverScheduledEnginePreview();
                };
            }
            _enginePreviewDebounceTimer.Stop();
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
            if (!Settings.AutoSaveEnabled) return;
            if (_saveDebounceTimer == null)
            {
                _saveDebounceTimer = new DispatcherTimer();
                _saveDebounceTimer.Tick += (s, e) =>
                {
                    _saveDebounceTimer.Stop();
                    SaveProject();
                };
            }
            _saveDebounceTimer.Interval = TimeSpan.FromSeconds(Settings.AutoSaveIntervalSeconds);
            _saveDebounceTimer.Stop();
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

        // ══════════════════════════════════════════════════════════════════════
        // ██  COMPONENT MANAGEMENT  ██
        // ══════════════════════════════════════════════════════════════════════
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
            if (EditorComponentService.RemoveComponent(SelectedNode, component, AppendLog))
            {
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
            AppendLog($"🧲 Snap Assist: {(IsSnapAssistEnabled ? "AÇIK (ENABLED)" : "KAPALI (DISABLED)")}");
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
            AppendLog("📐 OBS Assist: Arka plan 1920x1080 ekrana tam oturtuldu (Fitted to Screen)");
        }

        [RelayCommand]
        public void CenterSelectedElement()
        {
            if (SelectedNode == null) return;
            if (EditorLayoutAssistService.CenterSelectedElement(SelectedNode, out string desc))
            {
                AppendLog($"🎯 OBS Assist: {desc}");
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
            AppendLog("⬇️ OBS Assist: Karakterler zemin hizasına oturtuldu (Ground Baseline)");
        }

        [RelayCommand]
        public void ResetCharacterSize(CharacterComponentViewModel? charComp)
        {
            if (SelectedNode == null && charComp == null) return;
            EditorLayoutAssistService.ResetCharacterSize(SelectedNode, charComp);
            ScheduleSave();
            if (EngineHost.IsInitialized && SelectedNode != null)
                PushSceneToEngine(SelectedNode);
            AppendLog("📐 OBS Assist: Karakter boyutu standart orana sıfırlandı (600x900)");
        }

        [RelayCommand]
        public void PresetDialogueBox(string preset)
        {
            if (SelectedNode == null) return;
            EditorLayoutAssistService.PresetDialogueBox(SelectedNode, preset);
            if (preset == "BottomBanner")
                AppendLog("↕ OBS Assist: Diyalog kutusu alt banner olarak ayarlandı (1720x220)");
            else if (preset == "Center")
                AppendLog("🎯 OBS Assist: Diyalog kutusu merkeze hizalandı");
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
        }
        // ── PROJECT MANAGEMENT & BUILD EXPORT PIPELINE ──────────────────────

        /// <summary>
        /// Explicitly saves the current project (graphs, node layouts, and configs).
        /// </summary>
        [RelayCommand]
        public void SaveProject()
        {
            _saveDebounceTimer?.Stop();
            if (!StoryGraphLifecycleCoordinator.SaveProject(
                    AssetsPath,
                    AssetsJsonPath,
                    Nodes,
                    Connections,
                    SelectedNode,
                    GetStartNode()?.Id,
                    AppendLog))
                return;

            IsProjectDirty = false;
            AppendLog($"💾 [PROJE KAYDEDİLDİ] {Nodes.Count} düğüm ve tüm bileşenler başarıyla kaydedildi ({DateTime.Now:HH:mm:ss})");
        }

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
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
                if (window == null) return;
                if (!await ResolveUnsavedChangesAsync(window)) return;

                var folders = await window.StorageProvider.OpenFolderPickerAsync(new Avalonia.Platform.Storage.FolderPickerOpenOptions
                {
                    Title = "Proje Klasörünü Seçin (Assets/ içeren klasör)",
                    AllowMultiple = false
                });

                if (folders == null || folders.Count == 0)
                {
                    AppendLog("ℹ️ Proje açma iptal edildi.");
                    return;
                }

                string selectedDir = folders[0].Path.LocalPath;
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
                    AppendLog($"   📊 {Nodes.Count} düğüm, {Connections.Count} bağlantı yüklendi.");

                    // Select first node if available
                    if (Nodes.Count > 0)
                        SelectedNode = Nodes[0];
                }
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Proje açma hatası: {ex.Message}");
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
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
                if (window == null) return;

                var folders = await window.StorageProvider.OpenFolderPickerAsync(new Avalonia.Platform.Storage.FolderPickerOpenOptions
                {
                    Title = "Projeyi Farklı Kaydet (Hedef Klasör Seçin)",
                    AllowMultiple = false
                });

                if (folders != null && folders.Count > 0)
                {
                    string selectedDir = folders[0].Path.LocalPath;
                    string targetDir = EditorProjectLifecycleCoordinator.GenerateSaveAsTargetDirectory(selectedDir);
                    SaveProjectToDirectory(targetDir);
                }
                else
                {
                    AppendLog("ℹ️ Farklı kaydetme iptal edildi.");
                    return;
                }
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Farklı kaydetme hatası: {ex.Message}");
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
                SaveProject,
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
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
                
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string rootDir = Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", ".."));
                string defaultBuildDir = Path.Combine(rootDir, "Builds", "Standalone_PC");

                string buildOutDir = defaultBuildDir;
                if (window != null)
                {
                    var folders = await window.StorageProvider.OpenFolderPickerAsync(new Avalonia.Platform.Storage.FolderPickerOpenOptions
                    {
                        Title = "Build Al: Dağıtım / Çıktı Klasörünü Seçin",
                        AllowMultiple = false
                    });

                    if (folders != null && folders.Count > 0)
                    {
                        buildOutDir = folders[0].Path.LocalPath;
                    }
                    else
                    {
                        AppendLog("ℹ️ Build işlemi iptal edildi.");
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
                    SaveProject,
                    issues =>
                    {
                        ProjectIssuesViewModel.SetIssues(issues);
                        if (issues.Any(issue => issue.IsError))
                        {
                            IsProjectIssuesPanelVisible = true;
                            BottomPanelActiveTab = 4;
                        }
                    },
                    AppendLog,
                    msg => BuildProgress = msg,
                    _buildCancellation.Token);
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Build işlemi sırasında hata oluştu: {ex.Message}");
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
                AppendLog);
        }

        [RelayCommand]
        public void AnalyzeStoryGraph()
        {
            var issues = ProjectValidationService.Validate(Nodes, Connections, AssetsPath, GetStartNode()?.Id);
            ProjectIssuesViewModel.SetIssues(issues);
            IsProjectIssuesPanelVisible = true;
            BottomPanelActiveTab = 4;
            if (issues.Count == 0) AppendLog("✅ [GRAPH CHECK] No blocking asset or route issues found.");
            foreach (var issue in issues)
                AppendLog($"{(issue.IsError ? "❌" : "⚠️")} [GRAPH CHECK] {issue.Message}");
        }

        /// <summary>
        /// Packages project assets into a single .rowlpkg binary archive file.
        /// Opens a folder picker so the user can choose the output directory.
        /// </summary>
        [RelayCommand]
        public async Task BuildPackageAsync()
        {
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
                        AppendLog("ℹ️ Paket oluşturma iptal edildi.");
                        return;
                    }
                }

                var result = await EditorBuildCoordinator.PackageAssetsAsync(MainWindowViewModel.AssetsPath, outDir, AppendLog);
                if (result.Succeeded)
                {
                    AssetBrowserViewModel.RefreshAssets();
                }
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Paket oluşturma hatası: {ex.Message}");
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;

            bool hasPendingSave = _saveDebounceTimer?.IsEnabled == true;
            _saveDebounceTimer?.Stop();
            _enginePreviewDebounceTimer?.Stop();
            _smoothTimer.Stop();

            if (hasPendingSave)
            {
                SaveActiveStoryFile();
                SaveFullStoryGraphFile();
            }

            EngineHost.Dispose();
            AssetBitmapCache.Clear();
        }
    }
}
