using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views
{
    public partial class MainWindow : Window
    {
        private bool _isPanningGraph = false;
        private Point _panStartPointerPos;

        public MainWindow()
        {
            InitializeComponent();
            KeyDown += MainWindow_KeyDown;

            var container = this.FindControl<Grid>("NodeGraphContainer");
            if (container != null)
            {
                container.PointerPressed += NodeGraphContainer_PointerPressed;
                container.PointerMoved += NodeGraphContainer_PointerMoved;
                container.PointerReleased += NodeGraphContainer_PointerReleased;
                container.PointerWheelChanged += NodeGraphContainer_PointerWheelChanged;
            }
        }

        private void MainWindow_KeyDown(object? sender, KeyEventArgs e)
        {
            if (e.Key == Key.F2)
            {
                if (DataContext is MainWindowViewModel vm)
                {
                    vm.AssetBrowserViewModel.StartRenameCommand.Execute(null);
                    e.Handled = true;
                }
            }
        }

        private void NodeGraphContainer_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            var pProps = e.GetCurrentPoint(sender as Control).Properties;
            if (pProps.IsMiddleButtonPressed || pProps.IsRightButtonPressed || pProps.IsLeftButtonPressed)
            {
                if (e.Source is Grid || e.Source is Canvas || e.Source is Avalonia.Controls.Shapes.Rectangle)
                {
                    _isPanningGraph = true;
                    _panStartPointerPos = e.GetPosition(sender as Control);
                    e.Pointer.Capture(sender as Control);
                    e.Handled = true;
                }
            }
        }

        private void NodeGraphContainer_PointerMoved(object? sender, PointerEventArgs e)
        {
            if (_isPanningGraph && DataContext is MainWindowViewModel vm)
            {
                Point currentPos = e.GetPosition(sender as Control);
                double deltaX = currentPos.X - _panStartPointerPos.X;
                double deltaY = currentPos.Y - _panStartPointerPos.Y;

                vm.PanX += deltaX;
                vm.PanY += deltaY;
                vm.TargetPanX = vm.PanX;
                vm.TargetPanY = vm.PanY;

                _panStartPointerPos = currentPos;
                e.Handled = true;
            }
        }

        private void NodeGraphContainer_PointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (_isPanningGraph)
            {
                _isPanningGraph = false;
                e.Pointer.Capture(null);
                e.Handled = true;
            }
        }

        private void NodeGraphContainer_PointerWheelChanged(object? sender, PointerWheelEventArgs e)
        {
            if (DataContext is MainWindowViewModel vm)
            {
                // 1. MUST hold Ctrl key to zoom
                if (!e.KeyModifiers.HasFlag(KeyModifiers.Control))
                {
                    return;
                }

                double delta = e.Delta.Y;
                if (System.Math.Abs(delta) < 0.001) return;

                Control? container = sender as Control;
                if (container == null) return;

                // 2. Get mouse position relative to NodeGraphContainer
                Point mousePos = e.GetPosition(container);

                // 3. Compute REAL canvas point under mouse from TARGET view state (prevents animation drift)
                double targetZoom = vm.TargetZoom > 0 ? vm.TargetZoom : 1.0;
                double canvasMouseX = (mousePos.X - vm.TargetPanX) / targetZoom;
                double canvasMouseY = (mousePos.Y - vm.TargetPanY) / targetZoom;

                // 4. Fine granular zoom step (2% step per wheel tick for fine control like 95%)
                double zoomStep = 0.02 * (delta > 0 ? 1.0 : -1.0);
                double newTargetZoom = System.Math.Clamp(System.Math.Round(targetZoom + zoomStep, 4), 0.15, 4.0);

                if (System.Math.Abs(newTargetZoom - targetZoom) < 0.0001) return;

                // 5. Set target zoom and mouse-anchored target PanX/PanY
                vm.TargetZoom = newTargetZoom;
                vm.TargetPanX = mousePos.X - (canvasMouseX * newTargetZoom);
                vm.TargetPanY = mousePos.Y - (canvasMouseY * newTargetZoom);

                // 6. Start smooth 60 FPS lerp animation
                vm.StartSmoothViewAnimation();

                e.Handled = true;
            }
        }
    }
}
