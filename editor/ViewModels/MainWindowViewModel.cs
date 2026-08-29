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
    public partial class MainWindowViewModel : ViewModelBase
    {
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

        [ObservableProperty]
        private Point _wireStartPoint = new Point(0, 0);

        [ObservableProperty]
        private Point _wireEndPoint = new Point(0, 0);

        [ObservableProperty]
        private bool _isDraggingWire = false;

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

        public MainWindowViewModel() : this(string.Empty)
        {
        }

        public MainWindowViewModel(string projectPath)
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
            if (e.PropertyName == nameof(NodeViewModel.X) || e.PropertyName == nameof(NodeViewModel.Y))
            {
                foreach (var conn in Connections)
                {
                    conn.UpdatePoints();
                }
            }
            else if (e.PropertyName == nameof(NodeViewModel.Components) ||
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
                     e.PropertyName == nameof(NodeViewModel.DialogueBoxScale))
            {
                if (IsInteractivelyDragging)
                {
                    // Fast path: During active mouse dragging/resizing in Edit Frame,
                    // skip synchronous C++ JSON rendering to guarantee smooth 60+ FPS.
                    return;
                }

                ScheduleSave();

                // Also push scene update to engine when component data changes
                if (sender is NodeViewModel node && node == SelectedNode && EngineHost.IsInitialized)
                    PushSceneToEngine(node);
            }
        }

        public void EnforceSingleOutgoingWireRule()
        {
            var seenSourceNodes = new System.Collections.Generic.HashSet<NodeViewModel>();
            var toRemove = new System.Collections.Generic.List<ConnectionViewModel>();

            // Iterate reverse: keep newest cable for any source node, delete older outgoing cables from the same output pin
            for (int i = Connections.Count - 1; i >= 0; i--)
            {
                var conn = Connections[i];
                if (conn.SourceNode != null && seenSourceNodes.Contains(conn.SourceNode))
                {
                    toRemove.Add(conn);
                }
                else if (conn.SourceNode != null)
                {
                    seenSourceNodes.Add(conn.SourceNode);
                }
            }

            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
            }
            UpdateStartNodeState();
        }

        public void StartWireDrag(NodeViewModel sourceNode, Point pinPos)
        {
            // Strict Single-Output Rule: Unplug any existing cable originating from sourceNode's output pin
            var existingOutgoing = Connections.Where(c => c.SourceNode == sourceNode).ToList();
            foreach (var conn in existingOutgoing)
            {
                Connections.Remove(conn);
            }

            _wireDragSourceNode = sourceNode;
            WireStartPoint = new Point(sourceNode.X + 250, sourceNode.Y + 60);
            WireEndPoint = pinPos;
            IsDraggingWire = true;
            AppendLog($"Started drawing wire from Green Output Pin of Node #{sourceNode.Id}...");
        }

        public void StartUnplugWireDrag(NodeViewModel sourceNode, Point mousePos)
        {
            _wireDragSourceNode = sourceNode;
            WireStartPoint = new Point(sourceNode.X + 250, sourceNode.Y + 60);
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
                // Remove any existing cable originating from _wireDragSourceNode's output pin
                var existingOutgoing = Connections.Where(c => c.SourceNode == _wireDragSourceNode).ToList();
                foreach (var conn in existingOutgoing)
                {
                    Connections.Remove(conn);
                }

                Connections.Add(new ConnectionViewModel(_wireDragSourceNode, targetNode));
                AppendLog($"✅ Connected Wire: Node #{_wireDragSourceNode.Id} ---> Node #{targetNode.Id} (Total cables: {Connections.Count})");
            }
            else
            {
                AppendLog("✂️ Connection dropped in empty space (cable unplugged / removed).");
            }

            EnforceSingleOutgoingWireRule();
            _wireDragSourceNode = null;
        }

        public void DisconnectNodeInputs(NodeViewModel node)
        {
            var toRemove = Connections.Where(c => c.TargetNode == node).ToList();
            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
            }
            if (toRemove.Count > 0)
            {
                AppendLog($"✂️ Disconnected {toRemove.Count} incoming cable(s) from Node #{node.Id}");
            }
        }

        public void DisconnectNodeOutputs(NodeViewModel node)
        {
            var toRemove = Connections.Where(c => c.SourceNode == node).ToList();
            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
            }
            if (toRemove.Count > 0)
            {
                AppendLog($"✂️ Disconnected {toRemove.Count} outgoing cable(s) from Node #{node.Id}");
            }
        }

        [RelayCommand]
        public void DisconnectSelectedNodeCables()
        {
            if (SelectedNode == null) return;
            DisconnectAllNodeCables(SelectedNode);
        }

        public void DisconnectAllNodeCables(NodeViewModel node)
        {
            var toRemove = Connections.Where(c => c.SourceNode == node || c.TargetNode == node).ToList();
            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
            }
            if (toRemove.Count > 0)
            {
                AppendLog($"✂️ Disconnected all {toRemove.Count} cable(s) attached to Node #{node.Id}");
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
            DisconnectAllNodeCables(node);
            Nodes.Remove(node);
            AppendLog($"🗑️ Deleted Node #{node.Id} ({node.Title})");
            SelectedNode = Nodes.FirstOrDefault();
            UpdateStartNodeState();
        }

        [RelayCommand]
        public void AddNode()
        {
            ulong nextId = Nodes.Count > 0 ? Nodes.Max(n => n.Id) + 1 : 101;

            double zoom = ZoomScale > 0 ? ZoomScale : 1.0;
            double spawnX = (-PanX + 300) / zoom + ((Nodes.Count % 5) * 40);
            double spawnY = (-PanY + 180) / zoom + ((Nodes.Count % 5) * 30);

            var newNode = new NodeViewModel(nextId, $"Dialogue Node #{nextId}", spawnX, spawnY)
            {
                Speaker = "Narrator",
                DialogueText = $"New dialogue block #{nextId}. Drag green pin to connect!",
                BackgroundTexture = "bg_beach_sunset.png",
                CharacterSprite = "spr_evelyn.png"
            };

            newNode.PropertyChanged += OnNodePropertyChanged;
            Nodes.Add(newNode);
            SelectedNode = newNode;
            UpdateStartNodeState();
            AppendLog($"✨ Added new node #{nextId} at visible screen center ({spawnX:F0}, {spawnY:F0})");
        }

        [RelayCommand]
        public async Task ConnectEngineAsync()
        {
            StatusText = "Initializing embedded C++ Engine...";
            AppendLog("[Engine] Starting embedded RowlEngineCore library...");

            // Initialize engine (standalone window mode; embedded NativeControlHost mode
            // is set up separately by the view via InitializeEmbedded).
            bool success = await Task.Run(() => EngineHost.Initialize(1920, 1080, true));
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

            // Serialize ALL components (including multiple characters) as JSON
            // and push via the component-aware API
            try
            {
                var componentsList = new List<object>();
                foreach (var comp in node.Components)
                {
                    if (!comp.IsEnabled) continue;
                    componentsList.Add(new
                    {
                        type = comp.TypeKey,
                        id = comp.ComponentId,
                        enabled = comp.IsEnabled,
                        data = comp.Serialize()
                    });
                }

                var options = new JsonSerializerOptions
                {
                    Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
                };
                string json = JsonSerializer.Serialize(componentsList, options);
                EngineHost.UpdateSceneFromComponents(json);
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
        /// Supports both v2 (component-based) and legacy v1 (flat fields) formats.
        /// Returns true if file was loaded successfully, false if file doesn't exist or parsing failed.
        /// </summary>
        public bool LoadFullStoryGraphFile()
        {
            string filePath = System.IO.Path.Combine(AssetsPath, "full_story_graph.json");
            if (!System.IO.File.Exists(filePath))
            {
                filePath = System.IO.Path.Combine(AssetsJsonPath, "full_story_graph.json");
                if (!System.IO.File.Exists(filePath))
                    return false;
            }

            try
            {
                string json = System.IO.File.ReadAllText(filePath);
                using var doc = JsonDocument.Parse(json);
                var root = doc.RootElement;

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
                    ulong nodeId = nodeJson.TryGetProperty("id", out var idProp) ? idProp.GetUInt64() : 0;
                    string title = nodeJson.TryGetProperty("title", out var titleProp) ? titleProp.GetString() ?? $"Node #{nodeId}" : $"Node #{nodeId}";

                    double posX = 60 + (nodeMap.Count % 5) * 280;
                    double posY = 80 + (nodeMap.Count / 5) * 220;
                    if (nodeJson.TryGetProperty("editor_x", out var exProp)) posX = exProp.GetDouble();
                    if (nodeJson.TryGetProperty("editor_y", out var eyProp)) posY = eyProp.GetDouble();

                    // Use "bare" constructor (no default components) since we'll add them manually
                    var node = new NodeViewModel(nodeId, title, posX, posY, bare: true);

                    // ── V2 Format: Deserialize components ──
                    if (formatVersion >= 2 && nodeJson.TryGetProperty("components", out var compsArray) && compsArray.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var compJson in compsArray.EnumerateArray())
                        {
                            string typeKey = compJson.TryGetProperty("type", out var typeProp) ? typeProp.GetString() ?? "" : "";
                            if (string.IsNullOrEmpty(typeKey)) continue;

                            try
                            {
                                var component = ComponentRegistry.Create(typeKey);

                                // Set component ID if present
                                if (compJson.TryGetProperty("id", out var cidProp))
                                    component.ComponentId = cidProp.GetString() ?? component.ComponentId;

                                // Set enabled state
                                if (compJson.TryGetProperty("enabled", out var enabledProp))
                                    component.IsEnabled = enabledProp.GetBoolean();

                                // Deserialize component-specific data
                                if (compJson.TryGetProperty("data", out var dataProp) && dataProp.ValueKind == JsonValueKind.Object)
                                {
                                    var dataDict = new Dictionary<string, object?>();
                                    foreach (var kvp in dataProp.EnumerateObject())
                                    {
                                        dataDict[kvp.Name] = kvp.Value.ValueKind switch
                                        {
                                            JsonValueKind.String => kvp.Value.GetString(),
                                            JsonValueKind.Number => kvp.Value.GetDouble(),
                                            JsonValueKind.True => true,
                                            JsonValueKind.False => false,
                                            _ => kvp.Value.GetRawText()
                                        };
                                    }
                                    component.Deserialize(dataDict);
                                }

                                node.AddComponent(component);
                            }
                            catch (KeyNotFoundException)
                            {
                                AppendLog($"⚠️ Unknown component type '{typeKey}' in Node #{nodeId}, skipping.");
                            }
                        }
                    }
                    // ── V1 Format: Create components from flat fields ──
                    else
                    {
                        // Dialogue (Speaker + Box Layout)
                        var dlgComp = node.AddComponent<DialogueComponentViewModel>();
                        if (nodeJson.TryGetProperty("speaker", out var spk))
                            dlgComp.Speaker = spk.GetString() ?? "Evelyn";
                        if (nodeJson.TryGetProperty("dialogue", out var dlg))
                            dlgComp.DialogueText = dlg.GetString() ?? "";
                        if (nodeJson.TryGetProperty("dialogue_box_x", out var dbx)) dlgComp.X = dbx.GetDouble();
                        if (nodeJson.TryGetProperty("dialogue_box_y", out var dby)) dlgComp.Y = dby.GetDouble();
                        if (nodeJson.TryGetProperty("dialogue_box_width", out var dbw)) dlgComp.Width = dbw.GetDouble();
                        if (nodeJson.TryGetProperty("dialogue_box_height", out var dbh)) dlgComp.Height = dbh.GetDouble();

                        // Background
                        var bg = node.AddComponent<BackgroundComponentViewModel>();
                        if (nodeJson.TryGetProperty("background", out var bgTex))
                            bg.Texture = bgTex.GetString() ?? "bg_beach_sunset.png";
                        if (nodeJson.TryGetProperty("background_x", out var bgx)) bg.X = bgx.GetDouble();
                        if (nodeJson.TryGetProperty("background_y", out var bgy)) bg.Y = bgy.GetDouble();
                        if (nodeJson.TryGetProperty("background_width", out var bgw)) bg.Width = bgw.GetDouble();
                        if (nodeJson.TryGetProperty("background_height", out var bgh)) bg.Height = bgh.GetDouble();

                        // Character
                        var ch = node.AddComponent<CharacterComponentViewModel>();
                        if (nodeJson.TryGetProperty("character", out var chSpr))
                            ch.Sprite = chSpr.GetString() ?? "spr_evelyn.png";
                        if (nodeJson.TryGetProperty("character_pos", out var chPos))
                            ch.Position = chPos.GetString() ?? "Right";
                        if (nodeJson.TryGetProperty("character_x", out var chx)) ch.X = chx.GetDouble();
                        if (nodeJson.TryGetProperty("character_y", out var chy)) ch.Y = chy.GetDouble();
                        if (nodeJson.TryGetProperty("character_width", out var chw)) ch.Width = chw.GetDouble();
                        if (nodeJson.TryGetProperty("character_height", out var chh)) ch.Height = chh.GetDouble();
                        if (nodeJson.TryGetProperty("character_scale", out var chs)) ch.Scale = chs.GetDouble();

                        // Audio
                        var audio = node.AddComponent<AudioComponentViewModel>();
                        if (nodeJson.TryGetProperty("dsp", out var dsp))
                            audio.DspFilter = dsp.GetString() ?? "Normal";
                    }

                    // If no components were added at all (unexpected), add defaults
                    if (node.Components.Count == 0)
                    {
                        node.AddComponent<DialogueComponentViewModel>();
                        node.AddComponent<BackgroundComponentViewModel>();
                        node.AddComponent<CharacterComponentViewModel>();
                        node.AddComponent<AudioComponentViewModel>();
                    }

                    node.RefreshBitmaps();
                    node.PropertyChanged += OnNodePropertyChanged;
                    Nodes.Add(node);
                    nodeMap[nodeId] = node;
                }

                // Rebuild connections from next_nodes
                foreach (var nodeJson in nodesArray.EnumerateArray())
                {
                    ulong sourceId = nodeJson.TryGetProperty("id", out var srcIdProp) ? srcIdProp.GetUInt64() : 0;
                    if (!nodeMap.ContainsKey(sourceId)) continue;

                    if (nodeJson.TryGetProperty("next_nodes", out var nextArr) && nextArr.ValueKind == JsonValueKind.Array)
                    {
                        foreach (var nextJson in nextArr.EnumerateArray())
                        {
                            ulong targetId = nextJson.TryGetProperty("id", out var tidProp) ? tidProp.GetUInt64() : 0;
                            if (targetId != 0 && nodeMap.ContainsKey(targetId))
                            {
                                Connections.Add(new ConnectionViewModel(nodeMap[sourceId], nodeMap[targetId]));
                            }
                        }
                    }
                }
                EnforceSingleOutgoingWireRule();

                AppendLog($"📂 Loaded story graph from {filePath} ({Nodes.Count} nodes, {Connections.Count} connections, format v{formatVersion})");
                return true;
            }
            catch (Exception ex)
            {
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

                // Auto-detect Root / Start Node
                var startNode = GetStartNode();
                ulong startId = startNode != null ? startNode.Id : 101;

                // Use System.Text.Json for proper escaping
                var options = new JsonSerializerOptions
                {
                    WriteIndented = true,
                    Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
                };

                var graph = new
                {
                    format_version = 2,
                    start_node_id = startId,
                    nodes = Nodes.Select(n =>
                    {
                        // Get all outgoing connections from this node
                        var outgoingConns = Connections.Where(c => c.SourceNode == n && c.TargetNode != null).ToList();
                        var nextNodes = outgoingConns.Select(c => new
                        {
                            id = c.TargetNode!.Id,
                            label = ""
                        }).ToArray();

                        // Serialize components
                        var components = n.Components.Select(comp => new
                        {
                            type = comp.TypeKey,
                            id = comp.ComponentId,
                            enabled = comp.IsEnabled,
                            data = comp.Serialize()
                        }).ToArray();

                        return new
                        {
                            id = n.Id,
                            title = n.Title,
                            editor_x = n.X,
                            editor_y = n.Y,
                            components = components,
                            next_nodes = nextNodes,
                            // Legacy flat fields for backward compatibility with engine
                            speaker = n.Speaker,
                            dialogue = n.DialogueText,
                            background = n.BackgroundTexture,
                            background_x = n.BackgroundX,
                            background_y = n.BackgroundY,
                            background_width = n.BackgroundWidth,
                            background_height = n.BackgroundHeight,
                            character = n.CharacterSprite,
                            character_pos = n.CharacterPosition,
                            character_x = n.CharacterX,
                            character_y = n.CharacterY,
                            character_width = n.CharacterWidth,
                            character_height = n.CharacterHeight,
                            character_scale = n.CharacterScale,
                            dialogue_box_x = n.DialogueBoxX,
                            dialogue_box_y = n.DialogueBoxY,
                            dialogue_box_width = n.DialogueBoxWidth,
                            dialogue_box_height = n.DialogueBoxHeight
                        };
                    }).ToArray()
                };

                string content = JsonSerializer.Serialize(graph, options);
                System.IO.File.WriteAllText(System.IO.Path.Combine(AssetsPath, "full_story_graph.json"), content);
                System.IO.File.WriteAllText(System.IO.Path.Combine(AssetsJsonPath, "full_story_graph.json"), content);
            }
            catch (Exception ex)
            {
                AppendLog($"⚠️ Failed to save full_story_graph.json: {ex.Message}");
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

                    // Serialize components for active story file
                    var components = node.Components.Select(comp => new
                    {
                        type = comp.TypeKey,
                        id = comp.ComponentId,
                        enabled = comp.IsEnabled,
                        data = comp.Serialize()
                    }).ToArray();

                    var activeNode = new
                    {
                        format_version = 2,
                        node_id = node.Id,
                        components = components,
                        // Legacy flat fields for backward compatibility with engine
                        speaker = node.Speaker,
                        dialogue = node.DialogueText,
                        background = node.BackgroundTexture,
                        background_x = node.BackgroundX,
                        background_y = node.BackgroundY,
                        background_width = node.BackgroundWidth,
                        background_height = node.BackgroundHeight,
                        character = node.CharacterSprite,
                        character_pos = node.CharacterPosition,
                        character_x = node.CharacterX,
                        character_y = node.CharacterY,
                        character_width = node.CharacterWidth,
                        character_height = node.CharacterHeight,
                        character_scale = node.CharacterScale,
                        dialogue_box_x = node.DialogueBoxX,
                        dialogue_box_y = node.DialogueBoxY,
                        dialogue_box_width = node.DialogueBoxWidth,
                        dialogue_box_height = node.DialogueBoxHeight,
                        dsp = node.DspFilter
                    };

                    var options = new JsonSerializerOptions
                    {
                        WriteIndented = true,
                        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
                    };

                    string json = JsonSerializer.Serialize(activeNode, options);
                    System.IO.File.WriteAllText(System.IO.Path.Combine(AssetsJsonPath, "active_story.json"), json);
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

        private void StartStandaloneGame()
        {
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();
            AppendLog("▶ Starting Offscreen Play Mode...");

            if (!EngineHost.IsInitialized)
            {
                AppendLog("❌ Engine not initialized. Click 'Connect Engine' first.");
                return;
            }

            // Reload the story graph into the running engine
            string graphPath = System.IO.Path.Combine(AssetsJsonPath, "full_story_graph.json");
            if (System.IO.File.Exists(graphPath))
            {
                EngineHost.LoadStoryGraph(graphPath);
                AppendLog($"[Play] Story graph loaded from: {graphPath}");
            }

            // Reset engine state to initial start node (first frame)
            EngineHost.ResetToStartNode();

            var startNode = GetStartNode();
            if (startNode != null)
            {
                PushSceneToEngine(startNode);
                SelectNode(startNode);
            }

            EngineHost.SetPlayState(true);

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

        private void AppendLog(string message)
        {
            LogOutput += $"[{DateTime.Now:HH:mm:ss}] {message}\n";
        }

        // Debounced save to avoid disk thrashing during drag operations
        private DispatcherTimer? _saveDebounceTimer;
        public void ScheduleSave()
        {
            _saveDebounceTimer?.Stop();
            _saveDebounceTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
            _saveDebounceTimer.Tick += (s, e) =>
            {
                _saveDebounceTimer!.Stop();
                SaveActiveStoryFile();
                SaveFullStoryGraphFile();
            };
            _saveDebounceTimer.Start();
        }

        [RelayCommand]
        public void ShowPanel(string panelName)
        {
            switch (panelName)
            {
                case "Assets":
                    IsAssetsPanelVisible = !IsAssetsPanelVisible;
                    break;
                case "Inspector":
                    IsInspectorPanelVisible = !IsInspectorPanelVisible;
                    break;
                case "Log":
                    IsLogPanelVisible = !IsLogPanelVisible;
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
    }

    public partial class AssetNodeViewModel : ViewModelBase
    {
        public string Name { get; private set; }
        public string RelativePath { get; private set; }
        public string FullPath { get; private set; }
        public bool IsDirectory { get; }
        public string Icon { get; }
        public string IconColor { get; }
        public ObservableCollection<AssetNodeViewModel> Children { get; } = new();

        [ObservableProperty]
        private bool _isEditing;

        [ObservableProperty]
        private string _editingName = string.Empty;

        private readonly Action? _onRenamed;

        public AssetNodeViewModel(string name, string relativePath, string fullPath, bool isDirectory, Action? onRenamed = null)
        {
            Name = name;
            RelativePath = relativePath;
            FullPath = fullPath;
            IsDirectory = isDirectory;
            _onRenamed = onRenamed;

            if (isDirectory)
            {
                Icon = "📁";
                IconColor = "#FBBF24";
            }
            else
            {
                string ext = System.IO.Path.GetExtension(name).ToLowerInvariant();
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || ext == ".gif")
                {
                    Icon = "🖼️";
                    IconColor = "#38BDF8";
                }
                else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")
                {
                    Icon = "🎵";
                    IconColor = "#A855F7";
                }
                else if (ext == ".json" || ext == ".txt" || ext == ".lua")
                {
                    Icon = "📜";
                    IconColor = "#F59E0B";
                }
                else if (ext == ".rowlpkg")
                {
                    Icon = "📦";
                    IconColor = "#10B981";
                }
                else
                {
                    Icon = "📄";
                    IconColor = "#94A3B8";
                }
            }
        }

        public void StartRename()
        {
            EditingName = Name;
            IsEditing = true;
        }

        [RelayCommand]
        public void CommitRename()
        {
            if (!IsEditing) return;
            IsEditing = false;

            if (string.IsNullOrWhiteSpace(EditingName) || EditingName.Trim() == Name)
            {
                return;
            }

            try
            {
                string? parentDir = System.IO.Path.GetDirectoryName(FullPath);
                if (string.IsNullOrEmpty(parentDir)) return;

                string newFullPath = System.IO.Path.Combine(parentDir, EditingName.Trim());

                if (IsDirectory)
                {
                    if (System.IO.Directory.Exists(FullPath) && !System.IO.Directory.Exists(newFullPath))
                    {
                        System.IO.Directory.Move(FullPath, newFullPath);
                    }
                }
                else
                {
                    if (System.IO.File.Exists(FullPath) && !System.IO.File.Exists(newFullPath))
                    {
                        System.IO.File.Move(FullPath, newFullPath);
                    }
                }

                _onRenamed?.Invoke();
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Failed to rename asset: {ex.Message}");
            }
        }

        [RelayCommand]
        public void CancelRename()
        {
            IsEditing = false;
        }

        public override string ToString() => RelativePath;

        public override bool Equals(object? obj)
        {
            if (obj is AssetNodeViewModel other) return RelativePath == other.RelativePath;
            if (obj is string str) return RelativePath == str;
            return false;
        }

        public override int GetHashCode() => RelativePath.GetHashCode();
    }

    public partial class AssetItemViewModel : ViewModelBase
    {
        public string Name { get; }
        public string Icon { get; }
        public string IconColor { get; }

        public AssetItemViewModel(string path)
        {
            Name = path;
            string ext = System.IO.Path.GetExtension(path).ToLowerInvariant();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || ext == ".gif")
            {
                Icon = "🖼️";
                IconColor = "#38BDF8";
            }
            else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")
            {
                Icon = "🎵";
                IconColor = "#A855F7";
            }
            else if (ext == ".json" || ext == ".txt" || ext == ".lua")
            {
                Icon = "📜";
                IconColor = "#F59E0B";
            }
            else
            {
                Icon = "📦";
                IconColor = "#10B981";
            }
        }

        public override string ToString() => Name;

        public override bool Equals(object? obj)
        {
            if (obj is AssetItemViewModel other) return Name == other.Name;
            if (obj is string str) return Name == str;
            return false;
        }

        public override int GetHashCode() => Name.GetHashCode();
    }

    public partial class AssetBrowserViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        [ObservableProperty]
        private AssetNodeViewModel? _selectedNode;

        public ObservableCollection<AssetNodeViewModel> AssetTree { get; } = new();
        public ObservableCollection<AssetItemViewModel> Assets { get; } = new();
        public ObservableCollection<string> AssetNames { get; } = new();

        public AssetBrowserViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            RefreshAssets();
        }

        public void RefreshAssets()
        {
            AssetTree.Clear();
            Assets.Clear();
            AssetNames.Clear();

            // Define VFS mount point: only Assets/ is the canonical asset root.
            var mountPoints = new List<(string displayName, string path)>
            {
                ("Assets", MainWindowViewModel.AssetsPath),
                ("Mods", Path.Combine(MainWindowViewModel.ProjectRoot, "mods"))
            };

            foreach (var mountPoint in mountPoints)
            {
                string displayName = mountPoint.displayName;
                string mountPath = mountPoint.path;

                if (System.IO.Directory.Exists(mountPath))
                {
                    var rootDir = new System.IO.DirectoryInfo(mountPath);
                    var rootNode = new AssetNodeViewModel(displayName, displayName, mountPath, true, RefreshAssets);

                    PopulateDirectoryNode(rootDir, mountPath, rootNode.Children);
                    AssetTree.Add(rootNode);
                }
            }
        }

        private void PopulateDirectoryNode(System.IO.DirectoryInfo dirInfo, string rootPath, ObservableCollection<AssetNodeViewModel> targetCollection)
        {
            foreach (var subDir in dirInfo.GetDirectories().OrderBy(d => d.Name))
            {
                if (subDir.Name.StartsWith(".")) continue;

                string relPath = System.IO.Path.GetRelativePath(rootPath, subDir.FullName);
                var dirNode = new AssetNodeViewModel(subDir.Name, relPath, subDir.FullName, true, RefreshAssets);

                PopulateDirectoryNode(subDir, rootPath, dirNode.Children);

                targetCollection.Add(dirNode);
            }

            foreach (var file in dirInfo.GetFiles().OrderBy(f => f.Name))
            {
                if (file.Name.StartsWith(".") || file.Name.Equals(".gitkeep", StringComparison.OrdinalIgnoreCase)) continue;

                string relPath = System.IO.Path.GetRelativePath(rootPath, file.FullName);
                var fileNode = new AssetNodeViewModel(file.Name, relPath, file.FullName, false, RefreshAssets);

                targetCollection.Add(fileNode);
                Assets.Add(new AssetItemViewModel(relPath));
                AssetNames.Add(relPath);
            }
        }

        [RelayCommand]
        public void CreateFolder()
        {
            try
            {
                string rootPath = MainWindowViewModel.AssetsPath;
                string targetDir = rootPath;

                if (SelectedNode != null)
                {
                    if (SelectedNode.IsDirectory)
                    {
                        targetDir = SelectedNode.FullPath;
                    }
                    else
                    {
                        string? parent = System.IO.Path.GetDirectoryName(SelectedNode.FullPath);
                        if (!string.IsNullOrEmpty(parent)) targetDir = parent;
                    }
                }

                string newFolderName = "YeniKlasor";
                string fullNewFolderPath = System.IO.Path.Combine(targetDir, newFolderName);
                int counter = 1;
                while (System.IO.Directory.Exists(fullNewFolderPath))
                {
                    newFolderName = $"YeniKlasor_{counter++}";
                    fullNewFolderPath = System.IO.Path.Combine(targetDir, newFolderName);
                }

                System.IO.Directory.CreateDirectory(fullNewFolderPath);
                System.IO.File.WriteAllText(System.IO.Path.Combine(fullNewFolderPath, ".gitkeep"), "");

                RefreshAssets();
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Failed to create folder: {ex.Message}");
            }
        }

        [RelayCommand]
        public void DeleteAsset()
        {
            if (SelectedNode == null) return;
            try
            {
                if (SelectedNode.IsDirectory)
                {
                    if (System.IO.Directory.Exists(SelectedNode.FullPath))
                    {
                        System.IO.Directory.Delete(SelectedNode.FullPath, true);
                    }
                }
                else
                {
                    if (System.IO.File.Exists(SelectedNode.FullPath))
                    {
                        System.IO.File.Delete(SelectedNode.FullPath);
                    }
                }
                SelectedNode = null;
                RefreshAssets();
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Failed to delete asset: {ex.Message}");
            }
        }

        [RelayCommand]
        public void OpenInExplorer()
        {
            try
            {
                string targetPath = SelectedNode?.IsDirectory == true
                    ? SelectedNode.FullPath
                    : (System.IO.Path.GetDirectoryName(SelectedNode?.FullPath) ?? MainWindowViewModel.AssetsPath);

                if (string.IsNullOrEmpty(targetPath) || !System.IO.Directory.Exists(targetPath))
                {
                    targetPath = MainWindowViewModel.AssetsPath;
                }

                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
                {
                    FileName = targetPath,
                    UseShellExecute = true
                });
            }
            catch
            {
                try
                {
                    string targetPath = SelectedNode?.IsDirectory == true
                        ? SelectedNode.FullPath
                        : (System.IO.Path.GetDirectoryName(SelectedNode?.FullPath) ?? MainWindowViewModel.AssetsPath);
                    System.Diagnostics.Process.Start("xdg-open", targetPath);
                }
                catch { }
            }
        }

        [RelayCommand]
        public void StartRename()
        {
            if (SelectedNode != null)
            {
                SelectedNode.StartRename();
            }
        }

        [RelayCommand]
        public void RefreshAssetsCommand()
        {
            RefreshAssets();
        }
    }

    public partial class InspectorViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public InspectorViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            main.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
                    OnPropertyChanged(nameof(SelectedNode));
            };
        }

        public NodeViewModel? SelectedNode => MainViewModel.SelectedNode;
    }

    public partial class OutputLogViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public OutputLogViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            main.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.LogOutput))
                    OnPropertyChanged(nameof(LogOutput));
            };
        }

        public string LogOutput => MainViewModel.LogOutput;
    }

    public partial class NodeGraphViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public NodeGraphViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
        }

        public ObservableCollection<NodeViewModel> Nodes => MainViewModel.Nodes;
        public ObservableCollection<ConnectionViewModel> Connections => MainViewModel.Connections;
    }

    public partial class LivePreviewViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public LivePreviewViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // ██  COMPONENT MANAGEMENT (partial class extension)  ██
    // ══════════════════════════════════════════════════════════════════════

    public partial class MainWindowViewModel
    {
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
        /// Adds a new component of the specified type to the selected node.
        /// </summary>
        [RelayCommand]
        public void AddComponentByType(string typeKey)
        {
            if (SelectedNode == null || string.IsNullOrEmpty(typeKey)) return;

            try
            {
                var component = ComponentRegistry.Create(typeKey);
                SelectedNode.AddComponent(component);

                // Refresh bitmap on visual components so the image loads immediately
                if (component is BackgroundComponentViewModel bg) bg.RefreshBitmap();
                else if (component is CharacterComponentViewModel ch) ch.RefreshBitmap();

                IsAddComponentMenuOpen = false;
                AppendLog($"➕ Added {component.DisplayName} component to Node #{SelectedNode.Id}");
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
        /// Removes a specific component from the selected node.
        /// </summary>
        [RelayCommand]
        public void RemoveComponent(NodeComponentViewModel? component)
        {
            if (SelectedNode == null || component == null) return;
            string name = component.DisplayName;
            SelectedNode.RemoveComponent(component);
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

                // Determine the project root:
                // Case 1: Selected folder itself has Assets/ subfolder (project root)
                // Case 2: Selected folder IS the Assets folder
                // Case 3: Selected folder has full_story_graph.json directly (json subfolder selected)
                string? projectRoot = null;
                string? graphFile = null;

                string assetsSubDir = Path.Combine(selectedDir, "Assets");
                if (Directory.Exists(assetsSubDir))
                {
                    // Selected the project root folder
                    projectRoot = selectedDir;
                    graphFile = Path.Combine(assetsSubDir, "json", "full_story_graph.json");
                    if (!File.Exists(graphFile))
                        graphFile = Path.Combine(assetsSubDir, "full_story_graph.json");
                }
                else if (Path.GetFileName(selectedDir).Equals("Assets", StringComparison.OrdinalIgnoreCase))
                {
                    // Selected the Assets folder directly
                    projectRoot = Path.GetDirectoryName(selectedDir);
                    graphFile = Path.Combine(selectedDir, "json", "full_story_graph.json");
                    if (!File.Exists(graphFile))
                        graphFile = Path.Combine(selectedDir, "full_story_graph.json");
                }
                else
                {
                    // Try looking for full_story_graph.json in the selected folder
                    string directGraph = Path.Combine(selectedDir, "full_story_graph.json");
                    if (File.Exists(directGraph))
                    {
                        // Might be inside Assets/json/
                        projectRoot = Path.GetFullPath(Path.Combine(selectedDir, "..", ".."));
                        graphFile = directGraph;
                    }
                }

                if (projectRoot == null || graphFile == null || !File.Exists(graphFile))
                {
                    AppendLog($"⚠️ Seçilen klasörde geçerli bir Rowl Engine projesi bulunamadı.");
                    AppendLog($"   Beklenen yapı: [KlasörAdı]/Assets/json/full_story_graph.json");
                    return;
                }

                // 1. Clear cached bitmaps so no old assets remain
                AssetBitmapCache.Clear();

                // 2. Update project root and active project path
                ProjectRoot = projectRoot;
                CurrentProjectPath = projectRoot;

                // 3. Remount native engine VFS to isolated project directory
                EngineHost.SetProjectDirectory(projectRoot);

                // 4. Load the story graph
                bool loaded = LoadFullStoryGraphFile();
                if (loaded)
                {
                    // 5. Refresh asset browser strictly from new project's Assets/
                    AssetBrowserViewModel.RefreshAssets();
                    AppendLog($"📂 [PROJE AÇILDI] {projectRoot}");
                    AppendLog($"   📊 {Nodes.Count} düğüm, {Connections.Count} bağlantı yüklendi.");

                    // Select first node if available
                    if (Nodes.Count > 0)
                        SelectedNode = Nodes[0];
                }
                else
                {
                    AppendLog($"⚠️ Hikaye grafiği yüklenemedi: {graphFile}");
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
            CopyDirectoryRecursive(MainWindowViewModel.AssetsPath, targetAssets);

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
            AppendLog("\n=======================================================");
            AppendLog("🚀 ROWL ENGINE STANDALONE BUILD PIPELINE BAŞLATILDI");
            AppendLog($"📦 Hedef Çıktı Dizini: {buildOutDir}");
            AppendLog("=======================================================");

            Directory.CreateDirectory(buildOutDir);

            // Step 1: Save latest story graphs
            AppendLog("[BUILD 1/5] 📝 Hikaye grafiği ve bileşen verileri derleniyor...");
            SaveActiveStoryFile();
            SaveFullStoryGraphFile();

            // Step 2: Copy Assets folder
            AppendLog("[BUILD 2/5] 🖼️ Varlıklar (Assets) ve görseller paketleniyor...");
            string outAssets = Path.Combine(buildOutDir, "Assets");
            CopyDirectoryRecursive(MainWindowViewModel.AssetsPath, outAssets);

            // Step 3: Copy native binaries (rowl_engine & libRowlEngineCore.so)
            AppendLog("[BUILD 3/5] ⚙️ Yerel oyun motoru ikilileri (Rowl Engine Core) kopyalanıyor...");
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string rootDir = Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", ".."));
            string nativeBinSource = Path.Combine(rootDir, "build", "bin", "rowl_engine");
            string nativeLibSource = Path.Combine(rootDir, "build", "lib", "libRowlEngineCore.so");

            string destEngineExe = Path.Combine(buildOutDir, "RowlGame");
            string destEngineLib = Path.Combine(buildOutDir, "libRowlEngineCore.so");

            if (File.Exists(nativeBinSource))
            {
                File.Copy(nativeBinSource, destEngineExe, true);
                try
                {
                    if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
                    {
                        var mode = File.GetUnixFileMode(destEngineExe);
                        File.SetUnixFileMode(destEngineExe, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
                    }
                }
                catch { }
            }

            if (File.Exists(nativeLibSource))
            {
                File.Copy(nativeLibSource, destEngineLib, true);
            }

            // Step 4: Create launcher script (run_game.sh)
            AppendLog("[BUILD 4/5] 📜 Otomatik Başlatıcı (Launcher Script) oluşturuluyor...");
            string launcherScriptPath = Path.Combine(buildOutDir, "run_game.sh");
            string launcherContent = "#!/bin/bash\n" +
                                     "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n" +
                                     "export LD_LIBRARY_PATH=\"$SCRIPT_DIR:$LD_LIBRARY_PATH\"\n" +
                                     "cd \"$SCRIPT_DIR\"\n" +
                                     "exec \"$SCRIPT_DIR/RowlGame\" \"$@\"\n";
            File.WriteAllText(launcherScriptPath, launcherContent);

            try
            {
                if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
                {
                    var mode = File.GetUnixFileMode(launcherScriptPath);
                    File.SetUnixFileMode(launcherScriptPath, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
                }
            }
            catch { }

            // Step 5: Create README instructions
            AppendLog("[BUILD 5/5] 📄 Dağıtım ve çalıştırma kılavuzu (README) ekleniyor...");
            string readmePath = Path.Combine(buildOutDir, "README.txt");
            string readmeContent = "=======================================================\n" +
                                   "🎮 ROWL ENGINE - STANDALONE GAME RELEASE\n" +
                                   "=======================================================\n\n" +
                                   "Oyunu Başlatmak İçin:\n" +
                                   "Linux / macOS: ./run_game.sh veya ./RowlGame\n" +
                                   "Windows: RowlGame.exe\n\n" +
                                   "Tüm grafikler ve hikaye akışı Assets/ klasöründen bağımsız olarak yüklenir.\n" +
                                   "Oluşturulma Tarihi: " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "\n";
            File.WriteAllText(readmePath, readmeContent);

            AppendLog("\n=======================================================");
            AppendLog($"🎉 [BUILD BAŞARILI] Oyun bağımsız dağıtım paketi oluşturuldu!");
            AppendLog($"📁 Konum: {buildOutDir}");
            AppendLog($"▶️ Çalıştırmak için: {launcherScriptPath}");
            AppendLog("=======================================================\n");
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
                        Arguments = $"\"{scriptPath}\" \"{MainWindowViewModel.AssetsPath}\" \"{outPkg}\"",
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    };
                    using var proc = System.Diagnostics.Process.Start(psi);
                    if (proc != null)
                    {
                        proc.WaitForExit();
                        string output = proc.StandardOutput.ReadToEnd();
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

        /// <summary>
        /// Recursively copies a directory to a target path.
        /// </summary>
        private static void CopyDirectoryRecursive(string sourceDir, string targetDir)
        {
            if (!Directory.Exists(sourceDir)) return;
            Directory.CreateDirectory(targetDir);

            foreach (string file in Directory.GetFiles(sourceDir))
            {
                string dest = Path.Combine(targetDir, Path.GetFileName(file));
                File.Copy(file, dest, true);
            }

            foreach (string subDir in Directory.GetDirectories(sourceDir))
            {
                string dest = Path.Combine(targetDir, Path.GetFileName(subDir));
                CopyDirectoryRecursive(subDir, dest);
            }
        }
    }
}