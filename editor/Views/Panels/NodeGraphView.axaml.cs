using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Panels
{
    public partial class NodeGraphView : UserControl
    {
        private bool _isPanningGraph = false;
        private bool _isBoxSelecting = false;
        private Point _panStartPointerPos;
        private Point _boxStartCanvasPos;
        private Point _pressStartPos;

        // ── Faz 4 Dilim 3 — group frame gestures ──
        private CanvasGroupViewModel? _groupDrag;
        private string? _groupDragKind;
        private Point _groupLastCanvasPos;
        private double _groupResizeStartW;
        private double _groupResizeStartH;
        private Point _groupResizeStartPos;

        /// <summary>
        /// Tuvalin sol üstünü kaplayan araç satırının (kırıntı + düğüm komutları) bu görünümle
        /// aynı koordinat düzlemindeki sınırları. Ana pencere satırın <c>Bounds</c>'una bağlar;
        /// mini harita buna göre yer değiştirir (bkz. <see cref="Controls.MinimapPlacement"/>).
        /// </summary>
        public static readonly StyledProperty<Rect> MinimapObstacleProperty =
            AvaloniaProperty.Register<NodeGraphView, Rect>(nameof(MinimapObstacle));

        public Rect MinimapObstacle
        {
            get => GetValue(MinimapObstacleProperty);
            set => SetValue(MinimapObstacleProperty, value);
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == MinimapObstacleProperty || change.Property == BoundsProperty)
                UpdateMinimapPlacement();
        }

        private void UpdateMinimapPlacement()
        {
            if (this.FindControl<Panel>("MinimapSlot") is not { } slot ||
                this.FindControl<Border>("MinimapCard") is not { } card ||
                this.FindControl<Controls.MinimapControl>("CanvasMinimap") is not { } minimap)
                return;
            var cardSize = new Size(
                minimap.Width + card.Padding.Left + card.Padding.Right + card.BorderThickness.Left + card.BorderThickness.Right,
                minimap.Height + card.Padding.Top + card.Padding.Bottom + card.BorderThickness.Top + card.BorderThickness.Bottom);
            var placement = Controls.MinimapPlacement.Resolve(Bounds.Size, MinimapObstacle, cardSize);
            // Margin yerine çizim kaydırması: yeniden düzen istemez, bir sonraki karede hep güncel.
            double offset = placement.Top - Controls.MinimapPlacement.EdgeMargin;
            if (slot.RenderTransform is not Avalonia.Media.TranslateTransform shift)
                slot.RenderTransform = shift = new Avalonia.Media.TranslateTransform();
            if (shift.Y != offset)
                shift.Y = offset;
            if (slot.IsVisible != placement.IsVisible)
                slot.IsVisible = placement.IsVisible;
        }

        public NodeGraphView()
        {
            InitializeComponent();

            var container = this.FindControl<Grid>("NodeGraphContainer");
            if (container != null)
            {
                container.PointerPressed += NodeGraphContainer_PointerPressed;
                container.PointerMoved += NodeGraphContainer_PointerMoved;
                container.PointerReleased += NodeGraphContainer_PointerReleased;
                container.PointerWheelChanged += NodeGraphContainer_PointerWheelChanged;
                container.LayoutUpdated += NodeGraphContainer_LayoutUpdated;
                // Group gestures run on tunnel so a header/resize press never
                // reaches the marquee/pan bubble handlers below.
                container.AddHandler(
                    InputElement.PointerPressedEvent, GroupGesturePressed,
                    Avalonia.Interactivity.RoutingStrategies.Tunnel);
                container.AddHandler(
                    InputElement.PointerMovedEvent, GroupGestureMoved,
                    Avalonia.Interactivity.RoutingStrategies.Tunnel);
                container.AddHandler(
                    InputElement.PointerReleasedEvent, GroupGestureReleased,
                    Avalonia.Interactivity.RoutingStrategies.Tunnel);
            }

            var minimap = this.FindControl<Controls.MinimapControl>("CanvasMinimap");
            if (minimap != null)
            {
                minimap.ViewportRequested += (_, canvasPoint) =>
                {
                    if (DataContext is MainWindowViewModel vm)
                        vm.NodeGraphViewModel.PanTo(canvasPoint.X, canvasPoint.Y);
                };
            }

            KeyDown += NodeGraphView_KeyDown;
            Focusable = true;
        }

        private void NodeGraphContainer_LayoutUpdated(object? sender, EventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;
            if (sender is not Control control) return;
            var graph = vm.NodeGraphViewModel;
            if (control.Bounds.Width > 0 || control.Bounds.Height > 0)
            {
                // Faz 9 E1: görünüm boyutu tek kaynaktan (Bounds) beslenir.
                graph.ViewportSize = control.Bounds.Size;
            }
        }

        private void NodeGraphView_KeyDown(object? sender, KeyEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;
            // Metin girişinde normal yazım/odak davranışı bozulmaz.
            if (MainWindow.IsTextInputSource(e.Source))
                return;
            bool shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
            bool noCtrlAlt = !e.KeyModifiers.HasFlag(KeyModifiers.Control) &&
                !e.KeyModifiers.HasFlag(KeyModifiers.Meta) &&
                !e.KeyModifiers.HasFlag(KeyModifiers.Alt);
            if (e.Key == Key.F && noCtrlAlt && !shift)
            {
                vm.NodeGraphViewModel.FitToSelectionCommand.Execute(null);
                e.Handled = true;
                return;
            }
            if (e.Key == Key.F && noCtrlAlt && shift)
            {
                vm.NodeGraphViewModel.ZoomToFitCommand.Execute(null);
                e.Handled = true;
                return;
            }
            if (e.Key == Key.Tab && noCtrlAlt)
            {
                // Bu işleyici yalnız tuval içinden kabaran tuşlarda çalışır;
                // dış panellerdeki Tab buraya uğramaz.
                vm.CycleNodeSelection(backwards: shift);
                e.Handled = true;
            }
        }

        private Point GetCanvasPoint(Point containerPos, MainWindowViewModel vm)
        {
            double zoom = vm.ZoomScale > 0 ? vm.ZoomScale : 1.0;
            return new Point((containerPos.X - vm.PanX) / zoom, (containerPos.Y - vm.PanY) / zoom);
        }

        // ── Faz 4 Dilim 3 — group frame gestures (tunnel handlers) ──

        private static (CanvasGroupViewModel? group, string? kind) FindGroupGesture(object? source)
        {
            Avalonia.Visual? current = source as Avalonia.Visual;
            while (current != null)
            {
                if (current is Control control && control.Tag is string tag &&
                    (tag == "GroupHeader" || tag == "GroupResize" || tag == "GroupDelete") &&
                    control.DataContext is CanvasGroupViewModel group)
                    return (group, tag);
                current = current.GetVisualParent();
            }
            return (null, null);
        }

        private void GroupGesturePressed(object? sender, PointerPressedEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;
            var (group, kind) = FindGroupGesture(e.Source);
            if (group is null || kind is null) return;
            if (!e.GetCurrentPoint(sender as Control).Properties.IsLeftButtonPressed) return;

            if (kind == "GroupDelete")
            {
                vm.Groups.Delete(group.GroupId);
                e.Handled = true;
                return;
            }
            if (kind == "GroupHeader" && e.ClickCount >= 2)
            {
                vm.Groups.FrameToMembers(group.GroupId);
                e.Handled = true;
                return;
            }
            var container = sender as Control;
            if (container is null) return;
            Point canvasPos = GetCanvasPoint(e.GetPosition(container), vm);
            _groupDrag = group;
            _groupDragKind = kind;
            _groupLastCanvasPos = canvasPos;
            _groupResizeStartPos = canvasPos;
            _groupResizeStartW = group.Width;
            _groupResizeStartH = group.Height;
            e.Handled = true;
        }

        private void GroupGestureMoved(object? sender, PointerEventArgs e)
        {
            if (_groupDrag is null || _groupDragKind is null) return;
            if (DataContext is not MainWindowViewModel vm)
            {
                _groupDrag = null;
                return;
            }
            if (!e.GetCurrentPoint(sender as Control).Properties.IsLeftButtonPressed) return;
            var container = sender as Control;
            if (container is null) return;
            Point canvasPos = GetCanvasPoint(e.GetPosition(container), vm);
            if (_groupDragKind == "GroupHeader")
            {
                vm.Groups.MoveGroup(
                    _groupDrag.GroupId,
                    canvasPos.X - _groupLastCanvasPos.X,
                    canvasPos.Y - _groupLastCanvasPos.Y);
                _groupLastCanvasPos = canvasPos;
            }
            else if (_groupDragKind == "GroupResize")
            {
                vm.Groups.ResizeGroup(
                    _groupDrag.GroupId,
                    _groupResizeStartW + (canvasPos.X - _groupResizeStartPos.X),
                    _groupResizeStartH + (canvasPos.Y - _groupResizeStartPos.Y));
            }
            e.Handled = true;
        }

        private void GroupGestureReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (_groupDrag is null) return;
            _groupDrag = null;
            _groupDragKind = null;
            e.Handled = true;
        }

        private void NodeGraphContainer_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;
            var control = sender as Control;
            if (control == null) return;

            var pProps = e.GetCurrentPoint(control).Properties;
            bool isBackground = e.Source is Grid || e.Source is Canvas || e.Source is Avalonia.Controls.Shapes.Rectangle;

            if (!isBackground) return;

            // Faz 9 E1: boş tuvale basınca klavye odağı tuvale gelir (F/Tab çalışır).
            Focus(NavigationMethod.Pointer);

            _pressStartPos = e.GetPosition(control);

            if (pProps.IsMiddleButtonPressed || pProps.IsRightButtonPressed)
            {
                // Middle or Right click drags canvas (Pan)
                _isPanningGraph = true;
                _panStartPointerPos = _pressStartPos;
                e.Pointer.Capture(control);
                e.Handled = true;
            }
            else if (pProps.IsLeftButtonPressed)
            {
                // Left click on empty canvas initiates box/marquee selection
                _isBoxSelecting = true;
                _boxStartCanvasPos = GetCanvasPoint(_pressStartPos, vm);
                vm.StartSelectionBox(_boxStartCanvasPos);
                e.Pointer.Capture(control);
                e.Handled = true;
            }
        }

        private void NodeGraphContainer_PointerMoved(object? sender, PointerEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;
            var control = sender as Control;
            if (control == null) return;

            Point currentPos = e.GetPosition(control);

            if (_isPanningGraph)
            {
                double deltaX = currentPos.X - _panStartPointerPos.X;
                double deltaY = currentPos.Y - _panStartPointerPos.Y;

                vm.PanX += deltaX;
                vm.PanY += deltaY;
                vm.TargetPanX = vm.PanX;
                vm.TargetPanY = vm.PanY;

                _panStartPointerPos = currentPos;
                e.Handled = true;
            }
            else if (_isBoxSelecting)
            {
                Point currentCanvasPos = GetCanvasPoint(currentPos, vm);
                bool append = e.KeyModifiers.HasFlag(KeyModifiers.Shift) || e.KeyModifiers.HasFlag(KeyModifiers.Control);
                vm.UpdateSelectionBox(_boxStartCanvasPos, currentCanvasPos, append);
                e.Handled = true;
            }
        }

        private void NodeGraphContainer_PointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (DataContext is MainWindowViewModel vm)
            {
                var control = sender as Control;
                Point currentPos = control != null ? e.GetPosition(control) : _pressStartPos;
                double dist = Math.Sqrt(Math.Pow(currentPos.X - _pressStartPos.X, 2) + Math.Pow(currentPos.Y - _pressStartPos.Y, 2));

                if (_isBoxSelecting)
                {
                    _isBoxSelecting = false;
                    vm.EndSelectionBox();

                    // If simple click on empty background with no drag (< 5px) and no modifier, clear selection
                    if (dist < 5.0 && !e.KeyModifiers.HasFlag(KeyModifiers.Shift) && !e.KeyModifiers.HasFlag(KeyModifiers.Control))
                    {
                        vm.ClearNodeSelection();
                    }
                    e.Pointer.Capture(null);
                    e.Handled = true;
                }
                else if (_isPanningGraph)
                {
                    _isPanningGraph = false;
                    e.Pointer.Capture(null);
                    e.Handled = true;
                }
            }
        }

        private void NodeGraphContainer_PointerWheelChanged(object? sender, PointerWheelEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm)
                return;
            Control? container = sender as Control;
            if (container == null) return;

            // Ctrl+tekerlek: mevcut imleç merkezli zoom.
            if (e.KeyModifiers.HasFlag(KeyModifiers.Control))
            {
                double delta = e.Delta.Y;
                if (System.Math.Abs(delta) < 0.001) return;

                // Get mouse position relative to NodeGraphContainer
                Point mousePos = e.GetPosition(container);

                // Compute REAL canvas point under mouse from TARGET view state (prevents animation drift)
                double targetZoom = vm.TargetZoom > 0 ? vm.TargetZoom : 1.0;
                double canvasMouseX = (mousePos.X - vm.TargetPanX) / targetZoom;
                double canvasMouseY = (mousePos.Y - vm.TargetPanY) / targetZoom;

                // Fine granular zoom step (5% step per wheel tick for fine control)
                double zoomStep = 0.05 * (delta > 0 ? 1.0 : -1.0);
                double newTargetZoom = System.Math.Clamp(System.Math.Round(targetZoom + zoomStep, 4), 0.15, 4.0);

                // Set target zoom and mouse-anchored target PanX/PanY
                vm.TargetZoom = newTargetZoom;
                vm.TargetPanX = mousePos.X - (canvasMouseX * newTargetZoom);
                vm.TargetPanY = mousePos.Y - (canvasMouseY * newTargetZoom);

                // Start smooth 60 FPS lerp animation
                vm.StartSmoothViewAnimation();

                e.Handled = true;
                return;
            }

            // Faz 9 E1: düz tekerlek dikey, Shift+tekerlek yatay kaydırır; adım ekran
            // pikselinde sabit (60 px), böylece her zoom düzeyinde aynı hissettirir.
            const double stepScreen = 60.0;
            double dx = e.Delta.X, dy = e.Delta.Y;
            if (e.KeyModifiers.HasFlag(KeyModifiers.Shift) && Math.Abs(dx) < 0.001)
                (dx, dy) = (dy, 0);
            if (Math.Abs(dx) < 0.001 && Math.Abs(dy) < 0.001) return;
            vm.PanX += dx * stepScreen;
            vm.PanY += dy * stepScreen;
            vm.TargetPanX = vm.PanX;
            vm.TargetPanY = vm.PanY;
            e.Handled = true;
        }
    }
}
