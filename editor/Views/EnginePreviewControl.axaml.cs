using System;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views
{
    /// <summary>
    /// Avalonia UserControl that displays the SDL3 C++ engine offscreen render target.
    /// Supports Unity-style Game view playback (always shows the first frame in Edit mode or on Stop).
    /// </summary>
    public partial class EnginePreviewControl : UserControl
    {
        private EngineHost? _engineHost;
        private NodeViewModel? _subscribedStartNode;

        public EnginePreviewControl()
        {
            InitializeComponent();
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);

            if (DataContext is MainWindowViewModel vm)
            {
                _engineHost = vm.EngineHost;
                vm.PropertyChanged += OnViewModelPropertyChanged;

                if (_engineHost != null)
                {
                    _engineHost.PropertyChanged += OnEngineHostPropertyChanged;
                    if (!_engineHost.IsInitialized)
                    {
                        _engineHost.Initialize(1920, 1080, true);
                    }
                    UpdatePlayModeBadge();
                }

                HookStartNode();
                RenderFirstFrame();
            }
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            if (DataContext is MainWindowViewModel vm)
                vm.PropertyChanged -= OnViewModelPropertyChanged;

            if (_engineHost != null)
                _engineHost.PropertyChanged -= OnEngineHostPropertyChanged;

            UnhookStartNode();
            _engineHost = null;
            base.OnDetachedFromVisualTree(e);
        }

        private void OnEngineHostPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EngineHost.IsPlaying))
            {
                UpdatePlayModeBadge();
                if (_engineHost != null && !_engineHost.IsPlaying)
                {
                    RenderFirstFrame();
                }
            }
        }

        private void UpdatePlayModeBadge()
        {
            if (_engineHost != null && _engineHost.IsPlaying)
            {
                UpdateStatusBadge("LIVE PLAYMODE", "#16A34A");
            }
            else
            {
                UpdateStatusBadge("FIRST FRAME (PAUSED)", "#64748B");
            }
        }

        private void HookStartNode()
        {
            UnhookStartNode();

            if (DataContext is MainWindowViewModel vm)
            {
                var startNode = vm.GetStartNode();
                if (startNode != null)
                {
                    _subscribedStartNode = startNode;
                    _subscribedStartNode.PropertyChanged += OnStartNodePropertyChanged;
                }
            }
        }

        private void UnhookStartNode()
        {
            if (_subscribedStartNode != null)
            {
                _subscribedStartNode.PropertyChanged -= OnStartNodePropertyChanged;
                _subscribedStartNode = null;
            }
        }

        private void OnStartNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            // If the first frame's properties are modified in the editor, refresh Game view
            if (_engineHost != null && !_engineHost.IsPlaying && sender is NodeViewModel node)
            {
                PushNodeScene(node);
            }
        }

        private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(MainWindowViewModel.IsPlayingStandalone))
            {
                UpdatePlayModeBadge();
                if (_engineHost != null && !_engineHost.IsPlaying)
                {
                    RenderFirstFrame();
                }
            }
            else if (e.PropertyName == nameof(MainWindowViewModel.Nodes) ||
                     e.PropertyName == nameof(MainWindowViewModel.Connections))
            {
                HookStartNode();
                if (_engineHost != null && !_engineHost.IsPlaying)
                {
                    RenderFirstFrame();
                }
            }
        }

        private void RenderFirstFrame()
        {
            if (_engineHost == null || !_engineHost.IsInitialized) return;
            if (_engineHost.IsPlaying) return;

            if (DataContext is MainWindowViewModel vm)
            {
                _engineHost.ResetToStartNode();

                var startNode = vm.GetStartNode();
                if (startNode != null)
                {
                    PushNodeScene(startNode);
                }
            }
        }

        /// <summary>
        /// Handles click / touch on the preview area to advance the visual novel story node during Play Mode.
        /// Synchronizes the selected node in the C# ViewModel with the C++ engine playback.
        /// </summary>
        private void OnPreviewPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (_engineHost == null || !_engineHost.IsInitialized) return;

            // Unity behavior: Game interaction / click to advance is active ONLY in Play mode
            if (!_engineHost.IsPlaying) return;

            // Advance story node in C++ Engine (updates m_currentNodeId and triggers instant step)
            _engineHost.AdvanceNode(0);

            // Query new node ID from C++ engine and update selected node in editor
            ulong currentNodeId = _engineHost.GetCurrentNodeId();
            if (currentNodeId != 0 && DataContext is MainWindowViewModel vm)
            {
                var matchNode = vm.Nodes.FirstOrDefault(n => n.Id == currentNodeId);
                if (matchNode != null)
                {
                    vm.SelectNodeQuiet(matchNode);
                    PushNodeScene(matchNode);
                    _engineHost.ForceRenderFrame();
                }
            }
            e.Handled = true;
        }

        private void PushNodeScene(NodeViewModel node)
        {
            if (DataContext is MainWindowViewModel vm)
            {
                vm.PushSceneToEngine(node);
            }
        }

        private void UpdateStatusBadge(string text, string hexColor)
        {
            Dispatcher.UIThread.Post(() =>
            {
                var badge = this.FindControl<Border>("StatusBadge");
                var label = this.FindControl<TextBlock>("StatusText");
                if (badge != null)
                    badge.Background = Avalonia.Media.SolidColorBrush.Parse(hexColor);
                if (label != null)
                    label.Text = $"● {text}";
            });
        }
    }
}
