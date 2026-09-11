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
        private bool _isDraggingNode = false;
        private bool _isDraggingWire = false;
        private Point _dragStartNodePos;
        private Point _dragStartPointerPos;

        public NodeControl()
        {
            InitializeComponent();
            PointerPressed += OnPointerPressed;
            PointerMoved += OnPointerMoved;
            PointerReleased += OnPointerReleased;
        }

        private Canvas? GetRootCanvas()
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
                        var mouseCanvasPos = e.GetPosition(canvasToUse);
                        mainVmUnplug.StartUnplugWireDrag(sourceNode, mouseCanvasPos, existingConn.OptionId);
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
                e.Pointer.Capture(null);

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
                e.Pointer.Capture(null);
                e.Handled = true;
            }
        }
    }
}
