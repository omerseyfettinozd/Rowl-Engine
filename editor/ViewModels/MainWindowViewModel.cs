using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Core;
using Dock.Model.Controls;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using RowlEngine.Editor.Ipc;
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RowlEngine.Editor.ViewModels
{
    public partial class MainWindowViewModel : ViewModelBase
    {
        private readonly IpcClient _ipcClient;
        private readonly Factory _dockFactory = new();

        [ObservableProperty]
        private string _statusText = "Ready — Waiting to connect to C++ Engine IPC...";

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

        public AssetBrowserViewModel AssetBrowserViewModel { get; } = new();
        public OutputLogViewModel OutputLogViewModel { get; }
        public InspectorViewModel InspectorViewModel { get; }

        public MainWindowViewModel()
        {
            _ipcClient = new IpcClient("rowl_engine_ipc");
            OutputLogViewModel = new OutputLogViewModel(this);
            InspectorViewModel = new InspectorViewModel(this);

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
                SaveActiveStoryFile();
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
                if (seenSourceNodes.Contains(conn.SourceNode))
                {
                    toRemove.Add(conn);
                }
                else
                {
                    seenSourceNodes.Add(conn.SourceNode);
                }
            }

            foreach (var conn in toRemove)
            {
                Connections.Remove(conn);
            }
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
        }

        [RelayCommand]
        public void AddNode()
        {
            ulong nextId = (ulong)(101 + Nodes.Count);

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
            AppendLog($"✨ Added new node #{nextId} at visible screen center ({spawnX:F0}, {spawnY:F0})");
        }

        [RelayCommand]
        public async Task ConnectIpcAsync()
        {
            StatusText = "Connecting to Engine IPC...";
            bool success = await _ipcClient.ConnectAsync();
            IsConnected = success;
            if (success)
            {
                StatusText = "IPC Connected to C++ Engine Runtime! (Live Preview Active)";
                AppendLog("IPC Connected to Unix Socket /tmp/rowl_engine_ipc.sock");
            }
            else
            {
                StatusText = "IPC Connection failed. Launch C++ Engine with --ipc-mode.";
                AppendLog("IPC Connection failed (Engine not running with --ipc-mode).");
            }
        }

        public void SaveFullStoryGraphFile()
        {
            try
            {
                string dataPath = "/home/chaple/Belgeler/Rowl Engine/data";
                System.IO.Directory.CreateDirectory(dataPath);

                // Auto-detect Root / Start Node: Node with 0 incoming connections, or lowest ID node
                var startNode = Nodes.FirstOrDefault(n => !Connections.Any(c => c.TargetNode == n)) ?? Nodes.FirstOrDefault();
                ulong startId = startNode != null ? startNode.Id : 101;

                var sb = new StringBuilder();
                sb.AppendLine("{");
                sb.AppendLine($"  \"start_node_id\": {startId},");
                sb.AppendLine("  \"nodes\": [");

                for (int i = 0; i < Nodes.Count; i++)
                {
                    var n = Nodes[i];
                    var nextConn = Connections.FirstOrDefault(c => c.SourceNode == n);
                    ulong nextId = nextConn != null ? nextConn.TargetNode.Id : 0;

                    var inv = System.Globalization.CultureInfo.InvariantCulture;
                    sb.AppendLine("    {");
                    sb.AppendLine(FormattableString.Invariant($"      \"id\": {n.Id},"));
                    sb.AppendLine($"      \"speaker\": \"{n.Speaker}\",");
                    sb.AppendLine($"      \"dialogue\": \"{n.DialogueText}\",");
                    sb.AppendLine($"      \"background\": \"{n.BackgroundTexture}\",");
                    sb.AppendLine(FormattableString.Invariant($"      \"background_x\": {n.BackgroundX},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"background_y\": {n.BackgroundY},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"background_width\": {n.BackgroundWidth},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"background_height\": {n.BackgroundHeight},"));
                    sb.AppendLine($"      \"character\": \"{n.CharacterSprite}\",");
                    sb.AppendLine($"      \"character_pos\": \"{n.CharacterPosition}\",");
                    sb.AppendLine(FormattableString.Invariant($"      \"character_x\": {n.CharacterX},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"character_y\": {n.CharacterY},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"character_width\": {n.CharacterWidth},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"character_height\": {n.CharacterHeight},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"character_scale\": {n.CharacterScale},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"dialogue_box_x\": {n.DialogueBoxX},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"dialogue_box_y\": {n.DialogueBoxY},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"dialogue_box_width\": {n.DialogueBoxWidth},"));
                    sb.AppendLine(FormattableString.Invariant($"      \"dialogue_box_height\": {n.DialogueBoxHeight},"));
                    sb.AppendLine($"      \"next_id\": {nextId}");
                    sb.Append("    }");

                    if (i < Nodes.Count - 1) sb.AppendLine(",");
                    else sb.AppendLine();
                }

                sb.AppendLine("  ]");
                sb.AppendLine("}");

                System.IO.File.WriteAllText(System.IO.Path.Combine(dataPath, "full_story_graph.json"), sb.ToString());
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
                    string dataPath = "/home/chaple/Belgeler/Rowl Engine/data/json";
                    System.IO.Directory.CreateDirectory(dataPath);
                    string json = FormattableString.Invariant($"{{\n  \"node_id\": {node.Id},\n  \"speaker\": \"{node.Speaker}\",\n  \"dialogue\": \"{node.DialogueText}\",\n  \"background\": \"{node.BackgroundTexture}\",\n  \"background_x\": {node.BackgroundX},\n  \"background_y\": {node.BackgroundY},\n  \"background_width\": {node.BackgroundWidth},\n  \"background_height\": {node.BackgroundHeight},\n  \"character\": \"{node.CharacterSprite}\",\n  \"character_pos\": \"{node.CharacterPosition}\",\n  \"character_x\": {node.CharacterX},\n  \"character_y\": {node.CharacterY},\n  \"character_width\": {node.CharacterWidth},\n  \"character_height\": {node.CharacterHeight},\n  \"character_scale\": {node.CharacterScale},\n  \"dialogue_box_x\": {node.DialogueBoxX},\n  \"dialogue_box_y\": {node.DialogueBoxY},\n  \"dialogue_box_width\": {node.DialogueBoxWidth},\n  \"dialogue_box_height\": {node.DialogueBoxHeight},\n  \"dsp\": \"{node.DspFilter}\"\n}}");
                    System.IO.File.WriteAllText(System.IO.Path.Combine(dataPath, "active_story.json"), json);
                    SaveFullStoryGraphFile();
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

            if (!_ipcClient.IsConnected)
            {
                bool reconnected = await _ipcClient.ConnectAsync();
                IsConnected = reconnected;
            }

            string title = SelectedNode != null ? SelectedNode.Title : "Default Node";
            string payload = $"{{\"node_id\":{SelectedNode?.Id ?? 101}, \"title\":\"{title}\", \"speaker\":\"{SelectedNode?.Speaker}\", \"dialogue\":\"{SelectedNode?.DialogueText}\", \"background\":\"{SelectedNode?.BackgroundTexture}\", \"character\":\"{SelectedNode?.CharacterSprite}\", \"timestamp\":{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}}}";
            byte[] bytes = Encoding.UTF8.GetBytes(payload);

            bool sent = await _ipcClient.SendPacketAsync(MessageType.UpdateNodeGraph, bytes);
            if (sent)
            {
                AppendLog($"[Hot-Reload] Sent UpdateNodeGraph packet ({bytes.Length} bytes) -> Engine");
            }
            else
            {
                AppendLog("[Hot-Reload] Saved story state to disk (Engine not running in --ipc-mode).");
            }
        }

        [ObservableProperty]
        private bool _isPlayingStandalone = false;

        [ObservableProperty]
        private string _playButtonText = "▶ Play Standalone";

        [ObservableProperty]
        private string _playButtonColor = "#16A34A";

        private System.Diagnostics.Process? _standaloneProcess;

        [RelayCommand]
        public void TogglePlayStandalone()
        {
            if (IsPlayingStandalone && _standaloneProcess != null && !_standaloneProcess.HasExited)
            {
                StopStandaloneGame();
            }
            else
            {
                StartStandaloneGame();
            }
        }

        private void StartStandaloneGame()
        {
            SaveActiveStoryFile();
            AppendLog("▶ Starting Standalone Play Test (Engine runs without IPC)...");

            try
            {
                string projectRoot = "/home/chaple/Belgeler/Rowl Engine";
                string engineBinary = System.IO.Path.Combine(projectRoot, "build", "bin", "rowl_engine");

                if (!System.IO.File.Exists(engineBinary))
                {
                    AppendLog("❌ Engine binary not found. Build the C++ project first.");
                    return;
                }

                _standaloneProcess = new System.Diagnostics.Process
                {
                    StartInfo = new System.Diagnostics.ProcessStartInfo
                    {
                        FileName = engineBinary,
                        WorkingDirectory = projectRoot,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        CreateNoWindow = true
                    },
                    EnableRaisingEvents = true
                };

                _standaloneProcess.OutputDataReceived += (s, e) => {
                    if (!string.IsNullOrEmpty(e.Data))
                        AppendLog($"[Engine] {e.Data}");
                };
                _standaloneProcess.ErrorDataReceived += (s, e) => {
                    if (!string.IsNullOrEmpty(e.Data))
                        AppendLog($"[Engine Error] {e.Data}");
                };

                _standaloneProcess.Exited += (s, e) => {
                    Avalonia.Threading.Dispatcher.UIThread.Post(() => {
                        IsPlayingStandalone = false;
                        PlayButtonText = "▶ Play Standalone";
                        PlayButtonColor = "#16A34A";
                        StatusText = "Ready — Game Process Terminated";
                        AppendLog("⏹ Standalone Engine Process Exited.");
                    });
                };

                _standaloneProcess.Start();
                _standaloneProcess.BeginOutputReadLine();
                _standaloneProcess.BeginErrorReadLine();

                IsPlayingStandalone = true;
                PlayButtonText = "⏹ Stop Game";
                PlayButtonColor = "#DC2626";
                StatusText = $"Running Game Process (PID: {_standaloneProcess.Id})";
                AppendLog($"✅ Standalone engine launched (PID: {_standaloneProcess.Id}). Click 'Stop Game' or close window to end.");
            }
            catch (Exception ex)
            {
                AppendLog($"❌ Failed to launch standalone engine: {ex.Message}");
            }
        }

        private void StopStandaloneGame()
        {
            if (_standaloneProcess != null && !_standaloneProcess.HasExited)
            {
                try
                {
                    AppendLog($"⏹ Stopping Standalone Engine (PID: {_standaloneProcess.Id})...");
                    _standaloneProcess.Kill(true);
                }
                catch (Exception ex)
                {
                    AppendLog($"⚠️ Error stopping process: {ex.Message}");
                }
            }
            IsPlayingStandalone = false;
            PlayButtonText = "▶ Play Standalone";
            PlayButtonColor = "#16A34A";
            StatusText = "Ready — Waiting to connect to C++ Engine IPC...";
        }

        public void SelectNode(NodeViewModel node)
        {
            if (SelectedNode != null) SelectedNode.IsSelected = false;
            SelectedNode = node;
            SelectedNode.IsSelected = true;
            SaveActiveStoryFile();
            AppendLog($"Selected Node #{node.Id} ({node.Title})");
        }

        [RelayCommand]
        public async Task ImportAssetAsync()
        {
            try
            {
                var dialog = new OpenFileDialog
                {
                    Title = "Import Asset File into Rowl Engine Project",
                    AllowMultiple = true
                };
                var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
                if (window != null)
                {
                    var result = await dialog.ShowAsync(window);
                    if (result != null && result.Length > 0)
                    {
                        string dataPath = "/home/chaple/Belgeler/Rowl Engine/data";
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
                                ? "/home/chaple/Belgeler/Rowl Engine/data" 
                                : System.IO.Path.Combine("/home/chaple/Belgeler/Rowl Engine/data", subDir);
                            System.IO.Directory.CreateDirectory(targetDir);

                            string destPath = System.IO.Path.Combine(targetDir, fileName);
                            System.IO.File.Copy(file, destPath, true);
                            AppendLog($"📥 Imported Asset: {fileName} -> data/{(string.IsNullOrEmpty(subDir) ? "" : subDir + "/")}{fileName}");
                        }
                        AssetBrowserViewModel.RefreshAssets();
                    }
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
        [ObservableProperty]
        private AssetNodeViewModel? _selectedNode;

        public ObservableCollection<AssetNodeViewModel> AssetTree { get; } = new();
        public ObservableCollection<AssetItemViewModel> Assets { get; } = new();
        public ObservableCollection<string> AssetNames { get; } = new();

        public AssetBrowserViewModel()
        {
            RefreshAssets();
        }

        public void RefreshAssets()
        {
            AssetTree.Clear();
            Assets.Clear();
            AssetNames.Clear();

            string dataPath = "/home/chaple/Belgeler/Rowl Engine/data";
            if (System.IO.Directory.Exists(dataPath))
            {
                var rootDir = new System.IO.DirectoryInfo(dataPath);
                PopulateDirectoryNode(rootDir, dataPath, AssetTree);
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
                string rootPath = "/home/chaple/Belgeler/Rowl Engine/data";
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
                    : (System.IO.Path.GetDirectoryName(SelectedNode?.FullPath) ?? "/home/chaple/Belgeler/Rowl Engine/data");
                
                if (string.IsNullOrEmpty(targetPath) || !System.IO.Directory.Exists(targetPath))
                {
                    targetPath = "/home/chaple/Belgeler/Rowl Engine/data";
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
                        : (System.IO.Path.GetDirectoryName(SelectedNode?.FullPath) ?? "/home/chaple/Belgeler/Rowl Engine/data");
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
        private readonly MainWindowViewModel _main;

        public InspectorViewModel(MainWindowViewModel main)
        {
            _main = main;
        }

        public NodeViewModel? SelectedNode => _main.SelectedNode;
    }

    public partial class OutputLogViewModel : ViewModelBase
    {
        private readonly MainWindowViewModel _main;

        public OutputLogViewModel(MainWindowViewModel main)
        {
            _main = main;
        }

        public string LogOutput => _main.LogOutput;
    }

    public partial class NodeGraphViewModel : ViewModelBase
    {
        private readonly MainWindowViewModel _main;

        public NodeGraphViewModel(MainWindowViewModel main)
        {
            _main = main;
        }

        public ObservableCollection<NodeViewModel> Nodes => _main.Nodes;
        public ObservableCollection<ConnectionViewModel> Connections => _main.Connections;
    }

    public partial class LivePreviewViewModel : ViewModelBase
    {
        private readonly MainWindowViewModel _main;

        public LivePreviewViewModel(MainWindowViewModel main)
        {
            _main = main;
        }
    }
}