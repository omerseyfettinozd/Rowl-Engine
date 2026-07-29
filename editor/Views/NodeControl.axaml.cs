using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using RowlEngine.Editor.ViewModels;
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
                if (current is Canvas c && c.Width >= 2000)
                {
                    return c;
                }
                current = current.GetVisualParent();
            }
            return null;
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
                else if ((outputHandle != null && outputHandle.Bounds.Contains(pointRelativeToThis)) || pointRelativeToThis.X >= Bounds.Width - 45)
                {
                    if (VisualRoot is MainWindow mwOut && mwOut.DataContext is MainWindowViewModel mainVmOut)
                    {
                        mainVmOut.DisconnectNodeOutputs(vm);
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
                    if (existingConn != null)
                    {
                        var sourceNode = existingConn.SourceNode;
                        mainVmUnplug.Connections.Remove(existingConn);

                        _isDraggingWire = true;
                        e.Pointer.Capture(this);
                        var mouseCanvasPos = e.GetPosition(canvasToUse);
                        mainVmUnplug.StartUnplugWireDrag(sourceNode, mouseCanvasPos);
                        e.Handled = true;
                        return;
                    }
                }
            }

            // --- LEFT CLICK ON OUTPUT PIN: DRAW NEW WIRE ---
            if (pointerPoint.Properties.IsLeftButtonPressed &&
                ((outputHandle != null && outputHandle.Bounds.Contains(pointRelativeToThis)) || pointRelativeToThis.X >= Bounds.Width - 45))
            {
                _isDraggingWire = true;
                e.Pointer.Capture(this);

                if (VisualRoot is MainWindow mainWindow && mainWindow.DataContext is MainWindowViewModel mainVm)
                {
                    Point mouseCanvasPos = e.GetPosition(canvasToUse);
                    mainVm.StartWireDrag(vm, mouseCanvasPos);
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
                    mainVmSelect.SelectNode(vm);
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
                double deltaX = currentPointerPos.X - _dragStartPointerPos.X;
                double deltaY = currentPointerPos.Y - _dragStartPointerPos.Y;

                vm.X = System.Math.Max(0, _dragStartNodePos.X + deltaX);
                vm.Y = System.Math.Max(0, _dragStartNodePos.Y + deltaY);
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
