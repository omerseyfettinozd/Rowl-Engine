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
using System.Threading.Tasks;

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
        public ToastService Toast => ToastService.Instance;
        public UndoRedoService UndoRedo => UndoRedoService.Instance;

        [ObservableProperty]
        private string _currentBuildTarget = "Linux";

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
            var dialog = new Views.Dialogs.SettingsDialog(Settings);
            if (TopLevelHint is Window parentWindow)
                await dialog.ShowDialog(parentWindow);
            else
                dialog.Show();
        }

        [RelayCommand]
        public void OpenProjectHub()
        {
            if (Avalonia.Application.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime desktop)
            {
                var hubVm = new ProjectHubViewModel();
                var hubWin = new Views.ProjectHubWindow(hubVm);
                hubVm.ProjectOpened += (path) =>
                {
                    var newMain = new Views.MainWindow(path);
                    desktop.MainWindow = newMain;
                    newMain.Show();
                    hubWin.Close();
                };
                desktop.MainWindow = hubWin;
                hubWin.Show();
                if (TopLevelHint is Window curWin)
                {
                    curWin.Close();
                }
                else
                {
                    desktop.Windows.FirstOrDefault(w => w is Views.MainWindow)?.Close();
                }
            }
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
            if (string.IsNullOrWhiteSpace(value)) return;
            var match = Nodes.FirstOrDefault(n => 
                (n.Title?.Contains(value, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (n.Speaker?.Contains(value, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (n.DialogueText?.Contains(value, StringComparison.OrdinalIgnoreCase) ?? false));
            if (match != null)
            {
                SelectedNode = match;
                TargetPanX = -match.X * ZoomScale + 300;
                TargetPanY = -match.Y * ZoomScale + 200;
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
        private bool _isHierarchyPanelVisible = true;

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
            double zoomDiff = TargetZoom - ZoomScale;
            double panXDiff = TargetPanX - PanX;
            double panYDiff = TargetPanY - PanY;

            if (Math.Abs(zoomDiff) > 0.0001 || Math.Abs(panXDiff) > 0.05 || Math.Abs(panYDiff) > 0.05)
            {
                ZoomScale += zoomDiff * 0.22;
                PanX += panXDiff * 0.22;
                PanY += panYDiff * 0.22;
            }
            else
            {
                ZoomScale = TargetZoom;
                PanX = TargetPanX;
                PanY = TargetPanY;
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

            AssetBrowserViewModel = new AssetBrowserViewModel(this);
            OutputLogViewModel = new OutputLogViewModel(this);
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
            var startNode = Nodes.FirstOrDefault(n => !Connections.Any(c => c.TargetNode == n))
                            ?? Nodes.OrderBy(n => n.Id).FirstOrDefault();

            foreach (var node in Nodes)
            {
                node.IsStartNode = (node == startNode);
            }
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
            // Legacy name retained for bindings. Nodes are now multi-output
            // decision points, so only exact duplicate edges are removed.
            var duplicates = Connections.GroupBy(c => (c.SourceNode, c.TargetNode, c.OptionId))
                .SelectMany(group => group.Skip(1)).ToList();
            foreach (var duplicate in duplicates) Connections.Remove(duplicate);
            UpdateStartNodeState();
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

            NodeViewModel? targetNode = null;
            foreach (var node in Nodes)
            {
                if (node == _wireDragSourceNode) continue;
                Point leftPinPos = new Point(node.X + 10, node.Y + 60);
                double distance = Math.Sqrt(Math.Pow(releasePos.X - leftPinPos.X, 2) + Math.Pow(releasePos.Y - leftPinPos.Y, 2));
                if (distance < 75)
                {
                    targetNode = node;
                    break;
                }
            }

            if (targetNode != null)
            {
                if (!string.IsNullOrEmpty(_wireDragOptionId))
                {
                    // A button has exactly one destination. Reconnecting it
                    // replaces only that button's previous cable, never the
                    // sibling decisions on the same node.
                    foreach (var existing in Connections.Where(connection =>
                        connection.SourceNode == _wireDragSourceNode && connection.OptionId == _wireDragOptionId).ToList())
                    {
                        Connections.Remove(existing);
                    }
                    SetChoiceTarget(_wireDragSourceNode, _wireDragOptionId, targetNode.Id);
                }
                Connections.Add(new ConnectionViewModel(_wireDragSourceNode, targetNode, _wireDragOptionId));
                AppendLog($"✅ Connected Wire: Node #{_wireDragSourceNode.Id} ---> Node #{targetNode.Id} (Total cables: {Connections.Count})");
            }
            else
            {
                AppendLog("✂️ Connection dropped in empty space (cable unplugged / removed).");
            }

            EnforceSingleOutgoingWireRule();
            _wireDragSourceNode = null;
            _wireDragOptionId = string.Empty;
        }

        public void DisconnectNodeInputs(NodeViewModel node)
        {
            var toRemove = Connections.Where(c => c.TargetNode == node).ToList();
            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
                if (conn.SourceNode != null && !string.IsNullOrEmpty(conn.OptionId))
                    SetChoiceTarget(conn.SourceNode, conn.OptionId, 0);
            }
            if (toRemove.Count > 0)
            {
                AppendLog($"✂️ Disconnected {toRemove.Count} incoming cable(s) from Node #{node.Id}");
            }
        }

        public void DisconnectNodeOutputs(NodeViewModel node, string optionId = "")
        {
            var toRemove = Connections.Where(c => c.SourceNode == node &&
                (string.IsNullOrEmpty(optionId) || c.OptionId == optionId)).ToList();
            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
                if (conn.SourceNode != null && !string.IsNullOrEmpty(conn.OptionId))
                    SetChoiceTarget(conn.SourceNode, conn.OptionId, 0);
            }
            if (toRemove.Count > 0)
            {
                AppendLog($"✂️ Disconnected {toRemove.Count} outgoing cable(s) from Node #{node.Id}");
            }
        }

        private static void SetChoiceTarget(NodeViewModel node, string optionId, ulong targetNodeId)
        {
            var option = node.GetComponents<ChoiceComponentViewModel>()
                .SelectMany(choice => choice.Options)
                .FirstOrDefault(candidate => candidate.OptionId == optionId);
            if (option != null) option.TargetNodeId = targetNodeId;
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

        /// <summary>
        /// Compatibility alias — kept so any XAML bindings that reference
        /// ConnectIpcAsync continue to compile during transition.
        /// </summary>
        [RelayCommand]
        public Task ConnectIpcAsync() => ConnectEngineAsync();

        /// <summary>Sends the active node's scene data directly to the engine via P/Invoke.</summary>
        public void PushSceneToEngine(NodeViewModel node)
        {
            if (!EngineHost.IsInitialized) return;

            // Serialize ALL components (including multiple characters) across all active objects as JSON
            // and push via the component-aware API
            try
            {
                EngineHost.UpdateSceneFromComponents(StoryGraphSerializer.SerializePreviewComponents(node));
            }
            catch
            {
                // Fallback to legacy single-character API
                EngineHost.UpdateScene(
                    node.Speaker       ?? "",
                    node.DialogueText  ?? "",
                    node.BackgroundTexture ?? "",
                    (float)node.BackgroundX,  (float)node.BackgroundY,
                    (float)node.BackgroundWidth, (float)node.BackgroundHeight,
                    node.CharacterSprite ?? "",
                    (float)node.CharacterX,   (float)node.CharacterY,
                    (float)node.CharacterWidth, (float)node.CharacterHeight,
                    (float)node.DialogueBoxX,  (float)node.DialogueBoxY,
                    (float)node.DialogueBoxWidth, (float)node.DialogueBoxHeight
                );
            }
        }

        /// <summary>
        /// Loads the story graph from full_story_graph.json.
        /// Supports v3 (GameObject hierarchy), v2 (component-based), and legacy v1 (flat fields) formats.
        /// Returns true if file was loaded successfully, false if file doesn't exist or parsing failed.
        /// </summary>
        public bool LoadFullStoryGraphFile()
        {
            if (!StoryGraphDocumentReader.TryRead(
                    AssetsPath,
                    AssetsJsonPath,
                    out var document,
                    out var filePath,
                    out var readError))
            {
                if (!string.IsNullOrEmpty(readError))
                    AppendLog($"⚠️ Failed to read story graph: {readError}");
                return false;
            }

            var previousNodes = Nodes.ToList();
            var previousConnections = Connections.ToList();

            try
            {
                var parsedDocument = document!;
                using (parsedDocument)
                {
                var root = parsedDocument.RootElement;

                int formatVersion = 0;
                if (root.TryGetProperty("format_version", out var fv))
                    formatVersion = fv.GetInt32();

                if (!root.TryGetProperty("nodes", out var nodesArray) || nodesArray.ValueKind != JsonValueKind.Array)
                    return false;

                // Clear existing nodes
                Nodes.Clear();
                Connections.Clear();

                // Parse nodes
                var nodeMap = new Dictionary<ulong, NodeViewModel>();

                foreach (var nodeJson in nodesArray.EnumerateArray())
                {
                    var node = StoryGraphNodeHydrator.CreateShell(nodeJson, nodeMap.Count);
                    ulong nodeId = node.Id;

                    // ── V3 Format: Objects array (Unity GameObject style) ──
                    if (nodeJson.TryGetProperty("objects", out var objsArray) && objsArray.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var objJson in objsArray.EnumerateArray())
                        {
                            string objName = objJson.TryGetProperty("name", out var onProp) ? onProp.GetString() ?? "GameObject" : "GameObject";
                            var frameObj = node.CreateObject(objName);

                            if (objJson.TryGetProperty("id", out var oidProp))
                                frameObj.Id = oidProp.GetString() ?? frameObj.Id;

                            if (objJson.TryGetProperty("is_active", out var actProp))
                                frameObj.IsActive = actProp.GetBoolean();

                            if (objJson.TryGetProperty("components", out var compsArray) && compsArray.ValueKind == JsonValueKind.Array)
                            {
                                foreach (var compJson in compsArray.EnumerateArray())
                                {
                                    if (StoryGraphComponentHydrator.TryCreate(compJson, out var component, out var unknownType))
                                    {
                                        frameObj.AddComponent(component!);
                                    }
                                    else if (unknownType is not null)
                                    {
                                        AppendLog($"⚠️ Unknown component type '{unknownType}' in Node #{nodeId}, skipping.");
                                    }
                                }
                            }
                        }
                    }
                    // ── V2 Format: Components array directly under node (auto-migrate into objects) ──
                    else if (nodeJson.TryGetProperty("components", out var compsArray) && compsArray.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var compJson in compsArray.EnumerateArray())
                        {
                            if (StoryGraphComponentHydrator.TryCreate(compJson, out var component, out var unknownType))
                            {
                                string objName = component!.DisplayName;
                                var frameObj = node.CreateObject(objName);
                                frameObj.AddComponent(component);
                            }
                            else if (unknownType is not null)
                            {
                                AppendLog($"⚠️ Unknown component type '{unknownType}' in Node #{nodeId}, skipping.");
                            }
                        }
                    }
                    // ── V1 Format: Create objects & components from flat fields ──
                    else
                    {
                        StoryGraphNodeHydrator.PopulateLegacyFields(node, nodeJson);
                    }

                    StoryGraphNodeHydrator.EnsureDefaultObjects(node);

                    node.RefreshBitmaps();
                    node.PropertyChanged += OnNodePropertyChanged;
                    Nodes.Add(node);
                    nodeMap[nodeId] = node;
                }

                foreach (var connection in StoryGraphConnectionHydrator.Create(nodesArray, nodeMap))
                    Connections.Add(connection);
                EnforceSingleOutgoingWireRule();

                var startNode = GetStartNode() ?? Nodes.FirstOrDefault();
                if (startNode != null)
                {
                    SelectNodeQuiet(startNode);
                }

                AppendLog($"📂 Loaded story graph from {filePath} ({Nodes.Count} nodes, {Connections.Count} connections, format v{formatVersion})");
                return true;
                }
            }
            catch (Exception ex)
            {
                // Parsing is transactional from the user's perspective: a bad
                // file must not replace the currently open graph with a partial one.
                Nodes.Clear();
                Connections.Clear();
                foreach (var node in previousNodes) Nodes.Add(node);
                foreach (var connection in previousConnections) Connections.Add(connection);
                UpdateStartNodeState();
                AppendLog($"⚠️ Failed to load story graph: {ex.Message}");
                return false;
            }
        }

        public void SaveFullStoryGraphFile()
        {
            try
            {
                System.IO.Directory.CreateDirectory(AssetsPath);
                System.IO.Directory.CreateDirectory(AssetsJsonPath);
                ulong startId = GetStartNode()?.Id ?? 101;
                string content = StoryGraphSerializer.SerializeFullStoryGraph(Nodes, Connections, startId);
                ProjectFileSystem.WriteAllTextAtomically(System.IO.Path.Combine(AssetsPath, "full_story_graph.json"), content);
                ProjectFileSystem.WriteAllTextAtomically(System.IO.Path.Combine(AssetsJsonPath, "full_story_graph.json"), content);
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Failed to save story graph: {ex.Message}");
            }
        }

        public void SaveActiveStoryFile()
        {
            try
            {
                var node = SelectedNode ?? Nodes.FirstOrDefault();
                if (node != null)
                {
                    System.IO.Directory.CreateDirectory(AssetsJsonPath);
                    string json = StoryGraphSerializer.SerializeActiveStory(node);
                    ProjectFileSystem.WriteAllTextAtomically(System.IO.Path.Combine(AssetsJsonPath, "active_story.json"), json);
                }
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Failed to save active_story.json: {ex.Message}");
            }
        }

        [RelayCommand]
        public void SetSquareDialogueBox()
        {
            if (SelectedNode == null) return;
            SelectedNode.DialogueBoxWidth = 500.0;
            SelectedNode.DialogueBoxHeight = 500.0;
            AppendLog($"Set Dialogue Box to Square (500x500) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public void SetStandardDialogueBox()
        {
            if (SelectedNode == null) return;
            SelectedNode.DialogueBoxWidth = 1760.0;
            SelectedNode.DialogueBoxHeight = 180.0;
            SelectedNode.DialogueBoxX = 80.0;
            SelectedNode.DialogueBoxY = 860.0;
            AppendLog($"Reset Dialogue Box to Standard Banner (1760x180) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public void ResetBackgroundDimensions()
        {
            if (SelectedNode == null) return;
            SelectedNode.BackgroundX = 0.0;
            SelectedNode.BackgroundY = 0.0;
            SelectedNode.BackgroundWidth = 1920.0;
            SelectedNode.BackgroundHeight = 1080.0;
            AppendLog($"Reset Background to Fullscreen Canvas (1920x1080) for Node #{SelectedNode.Id}");
        }

        [RelayCommand]
        public void ResetCharacterDimensions()
        {
            if (SelectedNode == null) return;
            SelectedNode.CharacterWidth = 360.0;
            SelectedNode.CharacterHeight = 540.0;
            SelectedNode.CharacterScale = 1.0;
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
            return Nodes.FirstOrDefault(n => n.IsStartNode)
                   ?? Nodes.FirstOrDefault(n => !Connections.Any(c => c.TargetNode == n))
                   ?? Nodes.OrderBy(n => n.Id).FirstOrDefault();
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
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();
            AppendLog("▶ Starting Offscreen Play Mode...");

            if (!EngineHost.IsInitialized)
            {
                await ConnectEngineAsync();
                if (!EngineHost.IsInitialized)
                {
                    AppendLog("❌ Engine not initialized. Click 'Connect Engine' first.");
                    return;
                }
            }

            // Auto-switch to Game tab so the user sees the live playable game
            if (SplitScreenMode == 0)
            {
                IsEnginePreviewActive = true;
                IsPreviewActive = false;
                IsNodeGraphActive = false;
            }

            // Reload the story graph into the running engine
            string graphPath = System.IO.Path.Combine(AssetsJsonPath, "full_story_graph.json");
            if (System.IO.File.Exists(graphPath))
            {
                EngineHost.LoadStoryGraph(graphPath);
                AppendLog($"[Play] Story graph loaded from: {graphPath}");
            }

            // Activate engine play state FIRST
            EngineHost.SetPlayState(true);

            // Reset engine state to initial start node (first frame) with typewriter starting at 0
            EngineHost.ResetToStartNode();

            var startNode = GetStartNode();
            if (startNode != null)
            {
                SelectNodeQuiet(startNode);
                PushSceneToEngine(startNode);
            }

            IsPlayingStandalone = true;
            PlayButtonText = "⏹ Stop";
            PlayButtonColor = "#DC2626";
            StatusText = "Offscreen Play Mode Active";
            AppendLog("✅ Engine play state activated (Started from first frame).");
        }

        private void StopStandaloneGame()
        {
            EngineHost.SetPlayState(false);
            EngineHost.ResetToStartNode();

            IsPlayingStandalone = false;
            PlayButtonText = "▶ Play";
            PlayButtonColor = "#16A34A";
            StatusText = "Engine Ready — Offscreen C++ Runtime Active";
            AppendLog("⏹ Play mode stopped (Engine reset to first frame).");

            var startNode = GetStartNode();
            if (startNode != null)
            {
                PushSceneToEngine(startNode);
                SelectNode(startNode);
            }
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
                    string dataPath = MainWindowViewModel.AssetsPath;
                    System.IO.Directory.CreateDirectory(dataPath);

                    foreach (var fileItem in files)
                    {
                        string fullPath = fileItem.Path.LocalPath;
                        string fileName = System.IO.Path.GetFileName(fullPath);
                        string ext = System.IO.Path.GetExtension(fileName).ToLowerInvariant();
                        string subDir = ext switch
                        {
                            ".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga" => "images",
                            ".json" or ".lua" => "json",
                            ".rowlpkg" => "packages",
                            _ => ""
                        };
                        string targetDir = string.IsNullOrEmpty(subDir)
                            ? MainWindowViewModel.AssetsPath
                            : System.IO.Path.Combine(MainWindowViewModel.AssetsPath, subDir);
                        System.IO.Directory.CreateDirectory(targetDir);

                        string destPath = System.IO.Path.Combine(targetDir, fileName);
                        if (!string.Equals(System.IO.Path.GetFullPath(fullPath), System.IO.Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
                        {
                            System.IO.File.Copy(fullPath, destPath, true);
                        }
                        AppendLog($"📥 Imported Asset: {fileName} -> Assets/{(string.IsNullOrEmpty(subDir) ? "" : subDir + "/")}{fileName}");
                    }
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
                    var pending = _pendingEnginePreviewNode;
                    bool requiresSelection = _pendingEnginePreviewRequiresSelection;
                    _pendingEnginePreviewNode = null;
                    if (!IsInteractivelyDragging && pending != null &&
                        (!requiresSelection || pending == SelectedNode) && EngineHost.IsInitialized)
                        PushSceneToEngine(pending);
                };
            }
            _enginePreviewDebounceTimer.Stop();
            _enginePreviewDebounceTimer.Start();
        }

        public void ScheduleSave()
        {
            if (_saveDebounceTimer == null)
            {
                _saveDebounceTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
                _saveDebounceTimer.Tick += (s, e) =>
                {
                    _saveDebounceTimer.Stop();
                    SaveActiveStoryFile();
                    SaveFullStoryGraphFile();
                };
            }
            _saveDebounceTimer.Stop();
            _saveDebounceTimer.Start();
        }

        [RelayCommand]
        public void ShowPanel(string panelName)
        {
            switch (panelName)
            {
                case "Hierarchy":
                    IsHierarchyPanelVisible = !IsHierarchyPanelVisible;
                    break;
                case "Assets":
                    // Assets is now a tab in the bottom panel — show bottom panel and switch to Assets tab
                    IsLogPanelVisible = true;
                    BottomPanelActiveTab = 1;
                    break;
                case "Inspector":
                    IsInspectorPanelVisible = !IsInspectorPanelVisible;
                    break;
                case "Log":
                    if (IsLogPanelVisible && BottomPanelActiveTab == 0)
                    {
                        // Already showing log tab — toggle panel off
                        IsLogPanelVisible = !IsLogPanelVisible;
                    }
                    else
                    {
                        // Show bottom panel and switch to Log tab
                        IsLogPanelVisible = true;
                        BottomPanelActiveTab = 0;
                    }
                    break;
                case "NodeGraph":
                    IsNodeGraphActive = true;
                    IsPreviewActive = false;
                    IsEnginePreviewActive = false;
                    SplitScreenMode = 0; // Exits split screen mode to show full single Node Graph
                    break;
                case "Preview":
                    IsPreviewActive = true;
                    IsNodeGraphActive = false;
                    IsEnginePreviewActive = false;
                    break;
                case "EnginePreview":
                    IsEnginePreviewActive = true;
                    IsNodeGraphActive = false;
                    IsPreviewActive = false;
                    break;
                case "SplitScreen":
                    SplitScreenMode = (SplitScreenMode + 1) % 3;
                    if (SplitScreenMode > 0)
                    {
                        IsNodeGraphActive = true;
                    }
                    else
                    {
                        IsNodeGraphActive = true;
                        IsEnginePreviewActive = false;
                        IsPreviewActive = false;
                    }
                    break;
            }
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
            if (targetObj == null)
            {
                targetObj = SelectedNode.CreateObject("GameObject");
                if (HierarchyViewModel != null)
                {
                    HierarchyViewModel.SelectedObject = targetObj;
                }
            }

            try
            {
                var component = ComponentRegistry.Create(typeKey);
                targetObj.AddComponent(component);

                // Refresh bitmap on visual components so the image loads immediately
                if (component is BackgroundComponentViewModel bg) bg.RefreshBitmap();
                else if (component is CharacterComponentViewModel ch) ch.RefreshBitmap();

                IsAddComponentMenuOpen = false;
                AppendLog($"➕ Added {component.DisplayName} component to '{targetObj.Name}' in Node #{SelectedNode.Id}");
                ScheduleSave();

                // Push updated scene to engine so changes are visible immediately
                if (EngineHost.IsInitialized)
                    PushSceneToEngine(SelectedNode);
            }
            catch (KeyNotFoundException)
            {
                AppendLog($"⚠️ Unknown component type: {typeKey}");
            }
        }

        /// <summary>
        /// Removes a specific component from its parent GameObject.
        /// </summary>
        [RelayCommand]
        public void RemoveComponent(NodeComponentViewModel? component)
        {
            if (SelectedNode == null || component == null) return;
            string name = component.DisplayName;

            if (component.OwnerObject != null)
            {
                component.OwnerObject.RemoveComponent(component);
            }
            else
            {
                SelectedNode.RemoveComponent(component);
            }

            AppendLog($"🗑️ Removed {name} component from Node #{SelectedNode.Id}");
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
        }

        /// <summary>
        /// Copies an external image file into Assets/images/ if it is not already in the project,
        /// and returns the local relative filename.
        /// </summary>
        public string ImportImageFileToProject(string fullPath)
        {
            if (string.IsNullOrWhiteSpace(fullPath)) return string.Empty;

            string fileName = System.IO.Path.GetFileName(fullPath);
            string assetsImagesDir = System.IO.Path.Combine(MainWindowViewModel.AssetsPath, "images");
            System.IO.Directory.CreateDirectory(assetsImagesDir);
            string destPath = System.IO.Path.Combine(assetsImagesDir, fileName);

            // If the source is outside the destination path, copy it over
            if (!string.Equals(System.IO.Path.GetFullPath(fullPath), System.IO.Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
            {
                try
                {
                    System.IO.File.Copy(fullPath, destPath, true);
                    AppendLog($"📥 Auto-imported image '{fileName}' into Assets/images/");
                }
                catch (Exception ex)
                {
                    AppendLog($"⚠️ Failed to copy '{fileName}' to Assets/images: {ex.Message}");
                }
            }

            return fileName;
        }

        /// <summary>
        /// Opens an OS file picker dialog to let the user select an image file for a visual component.
        /// Automatically copies external images into Assets/images/ for project portability.
        /// </summary>
        [RelayCommand]
        public async Task SelectImageForComponentAsync(NodeComponentViewModel? component)
        {
            if (component == null) return;

            try
            {
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
                if (window == null) return;

                string assetsImagesFolder = System.IO.Path.Combine(MainWindowViewModel.AssetsPath, "images");
                System.IO.Directory.CreateDirectory(assetsImagesFolder);
                var startFolder = await window.StorageProvider.TryGetFolderFromPathAsync(new Uri(assetsImagesFolder));

                var files = await window.StorageProvider.OpenFilePickerAsync(new Avalonia.Platform.Storage.FilePickerOpenOptions
                {
                    Title = "Select Image Asset (Will auto-copy to project Assets/images)",
                    SuggestedStartLocation = startFolder,
                    AllowMultiple = false,
                    FileTypeFilter = new[]
                    {
                        new Avalonia.Platform.Storage.FilePickerFileType("Image Files (*.png, *.jpg, *.jpeg, *.bmp, *.webp, *.tga)")
                        {
                            Patterns = new[] { "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp", "*.tga" }
                        }
                    }
                });

                if (files != null && files.Count > 0)
                {
                    string fullPath = files[0].Path.LocalPath;
                    string fileName = ImportImageFileToProject(fullPath);

                    if (component is CharacterComponentViewModel charComp)
                    {
                        charComp.Sprite = fileName;
                        charComp.RefreshBitmap();
                        AppendLog($"🖼️ Selected Sprite '{fileName}' for Character Component (Auto-copied to Assets/images)");
                    }
                    else if (component is BackgroundComponentViewModel bgComp)
                    {
                        bgComp.Texture = fileName;
                        bgComp.RefreshBitmap();
                        AppendLog($"🖼️ Selected Texture '{fileName}' for Background Component (Auto-copied to Assets/images)");
                    }

                    AssetBrowserViewModel.RefreshAssets();
                    ScheduleSave();
                    if (EngineHost.IsInitialized && SelectedNode != null)
                        PushSceneToEngine(SelectedNode);
                }
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Failed to pick image file: {ex.Message}");
            }
        }

        /// <summary>
        /// Moves a component up in the render order.
        /// </summary>
        [RelayCommand]
        public void MoveComponentUp(NodeComponentViewModel? component)
        {
            if (SelectedNode == null || component == null) return;
            SelectedNode.MoveComponentUp(component);
            ScheduleSave();
        }

        /// <summary>
        /// Moves a component down in the render order.
        /// </summary>
        [RelayCommand]
        public void MoveComponentDown(NodeComponentViewModel? component)
        {
            if (SelectedNode == null || component == null) return;
            SelectedNode.MoveComponentDown(component);
            ScheduleSave();
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
            SelectedNode.BackgroundX = 0;
            SelectedNode.BackgroundY = 0;
            SelectedNode.BackgroundWidth = 1920;
            SelectedNode.BackgroundHeight = 1080;
            SelectedNode.BackgroundScale = 1.0;
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
            AppendLog("📐 OBS Assist: Arka plan 1920x1080 ekrana tam oturtuldu (Fitted to Screen)");
        }

        /// <summary>
        /// OBS-style Assist: Centers the selected element (Character, Dialogue, or Background) horizontally and vertically.
        /// </summary>
        [RelayCommand]
        public void CenterSelectedElement()
        {
            if (SelectedNode == null) return;
            var charComp = SelectedNode.GetComponent<CharacterComponentViewModel>();
            var bgComp = SelectedNode.GetComponent<BackgroundComponentViewModel>();

            if (charComp != null)
            {
                charComp.X = (1920 - charComp.Width) / 2.0;
                charComp.Y = 1080 - charComp.Height - 30; // ground baseline
                AppendLog($"🎯 OBS Assist: Karakter ortaya hizalandı (X: {charComp.X:0}, Y: {charComp.Y:0})");
            }
            else if (bgComp != null)
            {
                bgComp.X = (1920 - bgComp.Width) / 2.0;
                bgComp.Y = (1080 - bgComp.Height) / 2.0;
                AppendLog($"🎯 OBS Assist: Arka plan merkeze hizalandı (X: {bgComp.X:0}, Y: {bgComp.Y:0})");
            }

            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
        }

        /// <summary>
        /// OBS-style Assist: Aligns all character sprites to bottom ground baseline.
        /// </summary>
        [RelayCommand]
        public void AlignCharacterToBottom()
        {
            if (SelectedNode == null) return;
            foreach (var charComp in SelectedNode.CharacterComponents)
            {
                charComp.Y = 1080 - charComp.Height - 20;
            }
            ScheduleSave();
            if (EngineHost.IsInitialized)
                PushSceneToEngine(SelectedNode);
            AppendLog("⬇️ OBS Assist: Karakterler zemin hizasına oturtuldu (Ground Baseline)");
        }

        /// <summary>
        /// OBS-style Assist: Fits/Resets character sprite to standard size (600x900).
        /// </summary>
        [RelayCommand]
        public void ResetCharacterSize(CharacterComponentViewModel? charComp)
        {
            if (charComp == null && SelectedNode != null)
                charComp = SelectedNode.GetComponent<CharacterComponentViewModel>();
            if (charComp == null) return;

            charComp.Width = 600;
            charComp.Height = 900;
            charComp.Scale = 1.0;
            charComp.Y = 1080 - 900 - 20;
            ScheduleSave();
            if (EngineHost.IsInitialized && SelectedNode != null)
                PushSceneToEngine(SelectedNode);
            AppendLog("📐 OBS Assist: Karakter boyutu standart orana sıfırlandı (600x900)");
        }

        /// <summary>
        /// OBS-style Assist: Presets dialogue box to bottom banner or center box.
        /// </summary>
        [RelayCommand]
        public void PresetDialogueBox(string preset)
        {
            if (SelectedNode == null) return;
            var dlg = SelectedNode.GetComponent<DialogueComponentViewModel>();
            if (dlg == null) return;

            if (preset == "BottomBanner")
            {
                dlg.X = 100;
                dlg.Y = 820;
                dlg.Width = 1720;
                dlg.Height = 220;
                AppendLog("↕ OBS Assist: Diyalog kutusu alt banner olarak ayarlandı (1720x220)");
            }
            else if (preset == "Center")
            {
                dlg.X = (1920 - dlg.Width) / 2.0;
                dlg.Y = (1080 - dlg.Height) / 2.0;
                AppendLog("🎯 OBS Assist: Diyalog kutusu merkeze hizalandı");
            }
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
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();
            AppendLog($"💾 [PROJE KAYDEDİLDİ] {Nodes.Count} düğüm ve tüm bileşenler başarıyla kaydedildi ({DateTime.Now:HH:mm:ss})");
        }

        /// <summary>
        /// Opens the project root folder in the system file manager.
        /// </summary>
        [RelayCommand]
        public void OpenProjectFolder()
        {
            try
            {
                string rootDir = ProjectRoot;
                if (OperatingSystem.IsLinux())
                    System.Diagnostics.Process.Start("xdg-open", rootDir);
                else if (OperatingSystem.IsWindows())
                    System.Diagnostics.Process.Start("explorer.exe", rootDir);
                else if (OperatingSystem.IsMacOS())
                    System.Diagnostics.Process.Start("open", rootDir);
                AppendLog($"📂 Proje klasörü açıldı: {rootDir}");
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Klasör açılamadı: {ex.Message}");
            }
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
                if (!ProjectOpenCoordinator.TryResolve(selectedDir, out var project))
                {
                    AppendLog($"⚠️ Seçilen klasörde geçerli bir Rowl Engine projesi bulunamadı.");
                    AppendLog($"   Beklenen yapı: [KlasörAdı]/Assets/json/full_story_graph.json");
                    return;
                }

                string previousProjectRoot = ProjectRoot;
                string previousProjectPath = CurrentProjectPath;

                bool loaded = ProjectOpenCoordinator.Switch(
                    project!,
                    previousProjectRoot,
                    previousProjectPath,
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
                    AssetBrowserViewModel.RefreshAssets);
                if (loaded)
                {
                    AppendLog($"📂 [PROJE AÇILDI] {project!.RootPath}");
                    AppendLog($"   📊 {Nodes.Count} düğüm, {Connections.Count} bağlantı yüklendi.");

                    // Select first node if available
                    if (Nodes.Count > 0)
                        SelectedNode = Nodes[0];
                }
                else
                {
                    AppendLog($"⚠️ Hikaye grafiği yüklenemedi: {project!.GraphFilePath}");
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

                    // Create a timestamped subfolder inside the selected directory
                    string saveFolderName = $"RowlProject_{DateTime.Now:yyyy-MM-dd_HH-mm}";
                    string targetDir = Path.Combine(selectedDir, saveFolderName);
                    Directory.CreateDirectory(targetDir);

                    SaveProjectToDirectory(targetDir);
                    AppendLog($"💾 [FARKLI KAYDET] Proje başarıyla kopyalandı: {targetDir}");
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
            Directory.CreateDirectory(targetDir);

            // 1. Save current graph in memory to files
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();

            // 2. Copy Assets directory
            string targetAssets = Path.Combine(targetDir, "Assets");
            ProjectFileSystem.CopyDirectory(MainWindowViewModel.AssetsPath, targetAssets);

            // 3. Write project metadata manifest
            string projectManifest = Path.Combine(targetDir, "project.rowlproj");
            var manifestObj = new
            {
                name = "Rowl Engine Project",
                version = "1.0.0",
                engineVersion = "1.0.0",
                savedAt = DateTime.UtcNow.ToString("o"),
                nodeCount = Nodes.Count,
                startNodeId = Nodes.FirstOrDefault()?.Id ?? 101,
                virtualResolution = new { width = 1920, height = 1080 }
            };
            string manifestJson = JsonSerializer.Serialize(manifestObj, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(projectManifest, manifestJson);
        }

        /// <summary>
        /// Builds a standalone, playable game distribution package for PC (Linux/Windows/macOS).
        /// Packages binary engine, VFS assets, story graphs, and generates a run launcher.
        /// </summary>
        [RelayCommand]
        public async Task BuildGameAsync()
        {
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

                // Create a subfolder inside the selected directory to keep all build files organized
                string buildFolderName = $"RowlBuild_{DateTime.Now:yyyy-MM-dd_HH-mm}";
                string finalBuildDir = Path.Combine(buildOutDir, buildFolderName);
                Directory.CreateDirectory(finalBuildDir);

                ExecuteBuildPipeline(finalBuildDir);
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Build işlemi sırasında hata oluştu: {ex.Message}");
            }
        }

        /// <summary>
        /// Executes the complete standalone build pipeline.
        /// </summary>
        public void ExecuteBuildPipeline(string buildOutDir)
        {
            // Save latest story graphs
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();

            // Delegate export to ProjectBuildService
            ProjectBuildService.BuildStandalone(
                MainWindowViewModel.ProjectRoot,
                MainWindowViewModel.AssetsPath,
                buildOutDir,
                AppendLog);
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

                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string rootDir = Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", ".."));
                string scriptPath = Path.Combine(rootDir, "tools", "package_assets.py");

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
                        // User cancelled the dialog
                        AppendLog("ℹ️ Paket oluşturma iptal edildi.");
                        return;
                    }
                }

                Directory.CreateDirectory(outDir);
                string pkgFileName = $"game_data_{DateTime.Now:yyyy-MM-dd_HH-mm}.rowlpkg";
                string outPkg = Path.Combine(outDir, pkgFileName);

                if (File.Exists(scriptPath))
                {
                    var psi = new System.Diagnostics.ProcessStartInfo
                    {
                        FileName = "python3",
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    };
                    psi.ArgumentList.Add(scriptPath);
                    psi.ArgumentList.Add(MainWindowViewModel.AssetsPath);
                    psi.ArgumentList.Add(outPkg);
                    using var proc = System.Diagnostics.Process.Start(psi);
                    if (proc != null)
                    {
                        Task<string> outputTask = proc.StandardOutput.ReadToEndAsync();
                        Task<string> errorTask = proc.StandardError.ReadToEndAsync();
                        await proc.WaitForExitAsync();
                        string output = await outputTask;
                        string error = await errorTask;
                        if (proc.ExitCode != 0 || !File.Exists(outPkg))
                        {
                            AppendLog($"⚠️ Paket oluşturma başarısız (çıkış kodu {proc.ExitCode}):\n{error}\n{output}");
                            return;
                        }

                        AppendLog($"📦 [VFS PAKET] .rowlpkg başarıyla oluşturuldu:\n  📁 Konum: {outPkg}\n{output}");
                        AssetBrowserViewModel.RefreshAssets();
                    }
                }
                else
                {
                    AppendLog($"⚠️ Paket scripti bulunamadı: {scriptPath}");
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
