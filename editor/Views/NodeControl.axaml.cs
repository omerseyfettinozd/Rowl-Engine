using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using System.Linq;

namespace RowlEngine.Editor.Views
{
    public partial class NodeControl : UserControl
    {
        /// <summary>
        /// MS-5: injected canvas coordinate space. Bound from NodeGraphView
        /// (OuterCanvas); falls back to a visual-tree search when unset.
        /// </summary>
        public static readonly StyledProperty<Canvas?> HostCanvasProperty =
            AvaloniaProperty.Register<NodeControl, Canvas?>(nameof(HostCanvas));

        public Canvas? HostCanvas
        {
            get => GetValue(HostCanvasProperty);
            set => SetValue(HostCanvasProperty, value);
        }

        private bool _isDraggingNode = false;
        private bool _isDraggingWire = false;
        private Point _dragStartNodePos;
        private Point _dragStartPointerPos;
        private IPointer? _capturedPointer;

        public NodeControl()
        {
            InitializeComponent();
            Focusable = true;
            PointerPressed += OnPointerPressed;
            PointerMoved += OnPointerMoved;
            PointerReleased += OnPointerReleased;
            PointerCaptureLost += OnPointerCaptureLost;
            KeyDown += OnControlKeyDown;
            DoubleTapped += OnControlDoubleTapped;
        }

        /// <summary>
        /// Faz 4 Dilim 3 — boundary node double-click enters its subgraph.
        /// Plain nodes ignore the gesture entirely.
        /// </summary>
        private void OnControlDoubleTapped(object? sender, Avalonia.Input.TappedEventArgs e)
        {
            if (DataContext is not NodeViewModel vm || !vm.IsSubgraphBoundary)
                return;
            if (VisualRoot is MainWindow mw && mw.DataContext is MainWindowViewModel mainVm)
            {
                if (mainVm.EnterSubgraphForNode(vm.Id))
                    e.Handled = true;
            }
        }

        private Canvas? GetRootCanvas() => GetEffectiveCanvas();

        private Canvas? GetEffectiveCanvas()
        {
            if (HostCanvas != null) return HostCanvas;
            return FindCanvasFallback();
        }

        private Canvas? FindCanvasFallback()
        {
            Visual? current = this;
            while (current != null)
            {
                if (current is Canvas c && (c.Name == "OuterCanvas" || c.Width >= 1000))
                {
                    return c;
                }
                current = current.GetVisualParent();
            }
            return null;
        }

        /// <summary>
        /// MS-5: pointer capture loss (Alt+Tab, focus loss, leaving the window)
        /// must never leave a drag hanging: revert to a stable state with no
        /// undo record, since the gesture never committed.
        /// </summary>
        private void OnPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
        {
            if (!_isDraggingNode && !_isDraggingWire) return;

            if (DataContext is NodeViewModel && VisualRoot is MainWindow mw && mw.DataContext is MainWindowViewModel mainVm)
            {
                if (_isDraggingWire)
                    mainVm.CancelWireDrag();
                if (_isDraggingNode)
                    mainVm.CancelNodeDrag();
                mainVm.IsInteractivelyDragging = false;
            }
            _isDraggingNode = false;
            _isDraggingWire = false;
            _capturedPointer = null;
        }

        /// <summary>
        /// MS-5: Escape cancels the in-flight gesture and restores the
        /// pre-gesture state without producing an undo record.
        /// </summary>
        private void OnControlKeyDown(object? sender, KeyEventArgs e)
        {
            if (e.Key != Key.Escape) return;
            if (!_isDraggingNode && !_isDraggingWire) return;

            if (VisualRoot is MainWindow mw && mw.DataContext is MainWindowViewModel mainVm)
            {
                if (_isDraggingWire)
                    mainVm.CancelWireDrag();
                if (_isDraggingNode)
                    mainVm.CancelNodeDrag();
                mainVm.IsInteractivelyDragging = false;
            }
            _isDraggingNode = false;
            _isDraggingWire = false;
            _capturedPointer?.Capture(null);
            _capturedPointer = null;
            e.Handled = true;
        }

        private static string GetChoiceOptionId(object? source)
        {
            for (var visual = source as Visual; visual != null; visual = visual.GetVisualParent())
            {
                if (visual is Control { Tag: string optionId } && !string.IsNullOrEmpty(optionId))
                    return optionId;
            }
            return string.Empty;
        }

        private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (DataContext is not NodeViewModel vm) return;
            var rootCanvas = GetRootCanvas();
            Control canvasToUse = rootCanvas ?? (Parent as Control ?? this);

            var pointRelativeToThis = e.GetPosition(this);
            var outputHandle = this.FindControl<Border>("OutputPinHandle");
            var inputHandle = this.FindControl<Border>("InputPinHandle");
            var pointerPoint = e.GetCurrentPoint(this);
            var choiceOptionId = GetChoiceOptionId(e.Source);

            // --- RIGHT CLICK DISCONNECTION ---
            if (pointerPoint.Properties.IsRightButtonPressed)
            {
                if (inputHandle != null && inputHandle.Bounds.Contains(pointRelativeToThis))
                {
                    if (VisualRoot is MainWindow mwIn && mwIn.DataContext is MainWindowViewModel mainVmIn)
                    {
                        mainVmIn.DisconnectNodeInputs(vm);
                    }
                    e.Handled = true;
                    return;
                }
                else if (!string.IsNullOrEmpty(choiceOptionId) || (outputHandle != null && outputHandle.Bounds.Contains(pointRelativeToThis)))
                {
                    if (VisualRoot is MainWindow mwOut && mwOut.DataContext is MainWindowViewModel mainVmOut)
                    {
                        mainVmOut.DisconnectNodeOutputs(vm, choiceOptionId);
                    }
                    e.Handled = true;
                    return;
                }
            }

            // --- LEFT CLICK ON INPUT PIN: UNPLUG EXISTING CABLE (ComfyUI Style) ---
            if (pointerPoint.Properties.IsLeftButtonPressed && inputHandle != null && inputHandle.Bounds.Contains(pointRelativeToThis))
            {
                if (VisualRoot is MainWindow mwUnplug && mwUnplug.DataContext is MainWindowViewModel mainVmUnplug)
                {
                    var existingConn = mainVmUnplug.Connections.FirstOrDefault(c => c.TargetNode == vm);
                    if (existingConn != null && existingConn.SourceNode != null)
                    {
                        var sourceNode = existingConn.SourceNode;
                        mainVmUnplug.Connections.Remove(existingConn);
                        if (!string.IsNullOrEmpty(existingConn.OptionId))
                        {
                            var option = sourceNode.GetComponents<ChoiceComponentViewModel>()
                                .SelectMany(choice => choice.Options)
                                .FirstOrDefault(candidate => candidate.OptionId == existingConn.OptionId);
                            if (option != null) option.TargetNodeId = 0;
                        }

                        _isDraggingWire = true;
                        e.Pointer.Capture(this);
                        _capturedPointer = e.Pointer;
                        var mouseCanvasPos = e.GetPosition(canvasToUse);
                        mainVmUnplug.StartUnplugWireDrag(sourceNode, mouseCanvasPos, existingConn.OptionId, existingConn);
                        e.Handled = true;
                        return;
                    }
                }
            }

            // --- LEFT CLICK ON OUTPUT PIN: DRAW NEW WIRE ---
            if (pointerPoint.Properties.IsLeftButtonPressed &&
                (!string.IsNullOrEmpty(choiceOptionId) || (outputHandle != null && outputHandle.Bounds.Contains(pointRelativeToThis))))
            {
                _isDraggingWire = true;
                e.Pointer.Capture(this);
                        _capturedPointer = e.Pointer;

                if (VisualRoot is MainWindow mainWindow && mainWindow.DataContext is MainWindowViewModel mainVm)
                {
                    Point mouseCanvasPos = e.GetPosition(canvasToUse);
                    mainVm.StartWireDrag(vm, mouseCanvasPos, choiceOptionId);
                }
                e.Handled = true;
                return;
            }

            // Otherwise, drag node card
            if (pointerPoint.Properties.IsLeftButtonPressed)
            {
                _isDraggingNode = true;
                _dragStartNodePos = new Point(vm.X, vm.Y);
                _dragStartPointerPos = e.GetPosition(canvasToUse);
                e.Pointer.Capture(this);
                        _capturedPointer = e.Pointer;

                if (VisualRoot is MainWindow mainWindowSelect && mainWindowSelect.DataContext is MainWindowViewModel mainVmSelect)
                {
                    bool isToggle = e.KeyModifiers.HasFlag(KeyModifiers.Control) || e.KeyModifiers.HasFlag(KeyModifiers.Shift);
                    if (isToggle)
                    {
                        mainVmSelect.SelectNode(vm, addToSelection: true);
                    }
                    else if (!mainVmSelect.SelectedNodes.Contains(vm))
                    {
                        mainVmSelect.SelectNode(vm, addToSelection: false);
                    }
                    // Snapshot positions BEFORE the gesture mutates them so the
                    // release can record one atomic MoveNodesAction (MS-1).
                    mainVmSelect.BeginNodeDragSnapshot();
                    mainVmSelect.IsInteractivelyDragging = true;
                }
                e.Handled = true;
            }
        }

        private void OnPointerMoved(object? sender, PointerEventArgs e)
        {
            if (DataContext is not NodeViewModel vm) return;
            var rootCanvas = GetRootCanvas();
            Control canvasToUse = rootCanvas ?? (Parent as Control ?? this);

            if (_isDraggingWire)
            {
                var currentCanvasPos = e.GetPosition(canvasToUse);
                if (VisualRoot is MainWindow mainWindow && mainWindow.DataContext is MainWindowViewModel mainVm)
                {
                    mainVm.UpdateWireDrag(currentCanvasPos);
                }
                e.Handled = true;
            }
            else if (_isDraggingNode)
            {
                var currentPointerPos = e.GetPosition(canvasToUse);
                double stepDeltaX = currentPointerPos.X - _dragStartPointerPos.X;
                double stepDeltaY = currentPointerPos.Y - _dragStartPointerPos.Y;
                _dragStartPointerPos = currentPointerPos;

                if (VisualRoot is MainWindow mainWindowDrag && mainWindowDrag.DataContext is MainWindowViewModel mainVmDrag &&
                    mainVmDrag.SelectedNodes.Contains(vm) && mainVmDrag.SelectedNodes.Count > 1)
                {
                    mainVmDrag.BatchMoveSelectedNodes(stepDeltaX, stepDeltaY);
                }
                else
                {
                    vm.X += stepDeltaX;
                    vm.Y += stepDeltaY;
                }
                e.Handled = true;
            }
        }

        private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            var rootCanvas = GetRootCanvas();
            Control canvasToUse = rootCanvas ?? (Parent as Control ?? this);

            if (_isDraggingWire)
            {
                _isDraggingWire = false;
                _capturedPointer?.Capture(null);
                _capturedPointer = null;

                if (VisualRoot is MainWindow mainWindow && mainWindow.DataContext is MainWindowViewModel mainVm)
                {
                    var releasePos = e.GetPosition(canvasToUse);
                    mainVm.EndWireDrag(releasePos);
                }
                e.Handled = true;
            }
            else if (_isDraggingNode)
            {
                _isDraggingNode = false;
                _capturedPointer?.Capture(null);
                _capturedPointer = null;
                if (VisualRoot is MainWindow mainWindowEnd && mainWindowEnd.DataContext is MainWindowViewModel mainVmEnd)
                {
                    mainVmEnd.IsInteractivelyDragging = false;
                    mainVmEnd.EndNodeDragSnapshot();
                }
                e.Handled = true;
            }
        }
    }
}
