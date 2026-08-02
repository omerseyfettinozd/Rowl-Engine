using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Native;
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
        public static string ProjectRoot { get; } = ResolveProjectRoot();

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

        public MainWindowViewModel()
        {
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

            var node1 = new NodeViewModel(101, "Dialogue Node #101", 60, 80)
            {
                Speaker = "Evelyn",
                DialogueText = "Welcome to Rowl Engine! Try connecting multiple nodes together!",
                BackgroundTexture = "bg_beach_sunset.png",
                CharacterSprite = "spr_evelyn.png",
                DspFilter = "Normal"
            };

            var node2 = new NodeViewModel(102, "Dialogue Node #102", 420, 160)
            {
                Speaker = "System",
                DialogueText = "Player selected Option A. You can connect many cables simultaneously now!",
                BackgroundTexture = "bg_classroom.png",
                CharacterSprite = "spr_evelyn.png",
                DspFilter = "Telephone"
            };

            node1.PropertyChanged += OnNodePropertyChanged;
            node2.PropertyChanged += OnNodePropertyChanged;

            Nodes.Add(node1);
            Nodes.Add(node2);
            Connections.Add(new ConnectionViewModel(node1, node2));
            EnforceSingleOutgoingWireRule();
            SelectedNode = node1;
            UpdateStartNodeState();

            // Embedded engine: initialize directly (no separate process needed)
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
            else if (e.PropertyName == nameof(NodeViewModel.Speaker) ||
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
                ScheduleSave();
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
                StatusText = "Engine Ready — Embedded C++ Runtime Active";
                AppendLog("[Engine] RowlEngineCore initialized successfully (P/Invoke, zero IPC overhead).");

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
        private void PushSceneToEngine(NodeViewModel node)
        {
            if (!EngineHost.IsInitialized) return;

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
                    start_node_id = startId,
                    nodes = Nodes.Select(n =>
                    {
                        // Get all outgoing connections from this node
                        var outgoingConns = Connections.Where(c => c.SourceNode == n && c.TargetNode != null).ToList();
                        var nextNodes = outgoingConns.Select(c => new
                        {
                            id = c.TargetNode!.Id,
                            label = "" // No label in current ConnectionViewModel, could be extended
                        }).ToArray();

                        return new
                        {
                            id = n.Id,
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
                            dialogue_box_height = n.DialogueBoxHeight,
                            next_nodes = nextNodes
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

                    var activeNode = new
                    {
                        node_id = node.Id,
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

                var dialog = new OpenFileDialog
                {
                    Title = "Import Asset File into Rowl Engine Project",
                    AllowMultiple = true
                };
                var result = await dialog.ShowAsync(window);
                if (result != null && result.Length > 0)
                                {
                                    string dataPath = MainWindowViewModel.AssetsPath;
                                    System.IO.Directory.CreateDirectory(dataPath);

                                    foreach (var file in result)
                                    {
                                        string fileName = System.IO.Path.GetFileName(file);
                                        string ext = System.IO.Path.GetExtension(fileName).ToLowerInvariant();
                                        string subDir = ext switch
                                        {
                                            ".png" or ".jpg" or ".jpeg" or ".bmp" or ".tga" => "images",
                                            ".json" or ".lua" => "json",
                                            ".rowlpkg" => "packages",
                                            _ => ""
                                        };
                                        string targetDir = string.IsNullOrEmpty(subDir)
                                            ? MainWindowViewModel.AssetsPath
                                            : System.IO.Path.Combine(MainWindowViewModel.AssetsPath, subDir);
                                        System.IO.Directory.CreateDirectory(targetDir);

                                        string destPath = System.IO.Path.Combine(targetDir, fileName);
                                        System.IO.File.Copy(file, destPath, true);
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
        private void ScheduleSave()
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

            if (AssetNames.Count == 0)
            {
                string[] defaults = new[] { "bg_beach.png", "bg_classroom.png", "spr_evelyn.png", "bgm_theme.ogg", "test_story.json" };
                foreach (var d in defaults)
                {
                    Assets.Add(new AssetItemViewModel(d));
                    AssetNames.Add(d);
                    AssetTree.Add(new AssetNodeViewModel(d, d, d, false));
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
}