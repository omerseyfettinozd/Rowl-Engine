using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;
using System;

namespace RowlEngine.Editor.Views
{
    public partial class LivePreviewControl : UserControl
    {
        // ── Constants for virtual canvas bounds ──
        private const double VirtualCanvasWidth = 1920.0;
        private const double VirtualCanvasHeight = 1080.0;
        private const double DragLimitPadding = 500.0; // Allow dragging slightly outside canvas

        private enum ResizeTarget { None, Background, Character, DialogueBox }
        private enum ResizeCorner { TopLeft, TopRight, BottomLeft, BottomRight }

        private bool _isDraggingBackground = false;
        private bool _isDraggingCharacter = false;
        private bool _isDraggingDialogueBox = false;

        private bool _isResizing = false;
        private ResizeTarget _resizeTarget = ResizeTarget.None;
        private ResizeCorner _resizeCorner = ResizeCorner.BottomRight;

        private Point _dragStartPointerCanvasPos;
        private double _dragStartStartX;
        private double _dragStartStartY;
        private double _dragStartStartWidth;
        private double _dragStartStartHeight;

        public LivePreviewControl()
        {
            InitializeComponent();

            var bgBox = this.FindControl<Border>("BackgroundBox");
            if (bgBox != null)
            {
                bgBox.PointerPressed += OnBackgroundPointerPressed;
                bgBox.PointerMoved += OnBackgroundPointerMoved;
                bgBox.PointerReleased += OnBackgroundPointerReleased;
            }

            var charBox = this.FindControl<Border>("CharacterBox");
            if (charBox != null)
            {
                charBox.PointerPressed += OnCharacterPointerPressed;
                charBox.PointerMoved += OnCharacterPointerMoved;
                charBox.PointerReleased += OnCharacterPointerReleased;
            }

            var dlgBox = this.FindControl<Border>("DialogueBox");
            if (dlgBox != null)
            {
                dlgBox.PointerPressed += OnDialogueBoxPointerPressed;
                dlgBox.PointerMoved += OnDialogueBoxPointerMoved;
                dlgBox.PointerReleased += OnDialogueBoxPointerReleased;
            }

            // Bind Resize Handles
            BindHandle("BgHandleBR", ResizeTarget.Background, ResizeCorner.BottomRight);

            BindHandle("CharHandleTL", ResizeTarget.Character, ResizeCorner.TopLeft);
            BindHandle("CharHandleTR", ResizeTarget.Character, ResizeCorner.TopRight);
            BindHandle("CharHandleBL", ResizeTarget.Character, ResizeCorner.BottomLeft);
            BindHandle("CharHandleBR", ResizeTarget.Character, ResizeCorner.BottomRight);

            BindHandle("DlgHandleTL", ResizeTarget.DialogueBox, ResizeCorner.TopLeft);
            BindHandle("DlgHandleTR", ResizeTarget.DialogueBox, ResizeCorner.TopRight);
            BindHandle("DlgHandleBL", ResizeTarget.DialogueBox, ResizeCorner.BottomLeft);
            BindHandle("DlgHandleBR", ResizeTarget.DialogueBox, ResizeCorner.BottomRight);
        }

        private void BindHandle(string name, ResizeTarget target, ResizeCorner corner)
        {
            var handle = this.FindControl<Border>(name);
            if (handle != null)
            {
                handle.PointerPressed += (s, e) => StartResize(e, target, corner, s as Control);
                handle.PointerMoved += OnResizePointerMoved;
                handle.PointerReleased += OnResizePointerReleased;
            }
        }

        private Canvas? GetViewportCanvas()
        {
            return this.FindControl<Canvas>("ViewportCanvas");
        }

        private void StartResize(PointerPressedEventArgs e, ResizeTarget target, ResizeCorner corner, Control? handleControl)
        {
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isResizing = true;
                _resizeTarget = target;
                _resizeCorner = corner;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);

                var node = mainVm.SelectedNode;
                if (target == ResizeTarget.Character)
                {
                    _dragStartStartX = node.CharacterX;
                    _dragStartStartY = node.CharacterY;
                    _dragStartStartWidth = node.CharacterWidth;
                    _dragStartStartHeight = node.CharacterHeight;
                }
                else if (target == ResizeTarget.DialogueBox)
                {
                    _dragStartStartX = node.DialogueBoxX;
                    _dragStartStartY = node.DialogueBoxY;
                    _dragStartStartWidth = node.DialogueBoxWidth;
                    _dragStartStartHeight = node.DialogueBoxHeight;
                }
                else if (target == ResizeTarget.Background)
                {
                    _dragStartStartX = node.BackgroundX;
                    _dragStartStartY = node.BackgroundY;
                    _dragStartStartWidth = node.BackgroundWidth;
                    _dragStartStartHeight = node.BackgroundHeight;
                }

                e.Pointer.Capture(handleControl);
                e.Handled = true;
            }
        }

        private void OnResizePointerMoved(object? sender, PointerEventArgs e)
        {
            if (!_isResizing || DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var node = mainVm.SelectedNode;
            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;

            double newX = _dragStartStartX;
            double newY = _dragStartStartY;
            double newW = _dragStartStartWidth;
            double newH = _dragStartStartHeight;

            switch (_resizeCorner)
            {
                case ResizeCorner.BottomRight:
                    newW = Math.Max(60, _dragStartStartWidth + deltaX);
                    newH = Math.Max(60, _dragStartStartHeight + deltaY);
                    break;
                case ResizeCorner.BottomLeft:
                    newW = Math.Max(60, _dragStartStartWidth - deltaX);
                    newX = _dragStartStartX + (_dragStartStartWidth - newW);
                    newH = Math.Max(60, _dragStartStartHeight + deltaY);
                    break;
                case ResizeCorner.TopRight:
                    newW = Math.Max(60, _dragStartStartWidth + deltaX);
                    newH = Math.Max(60, _dragStartStartHeight - deltaY);
                    newY = _dragStartStartY + (_dragStartStartHeight - newH);
                    break;
                case ResizeCorner.TopLeft:
                    newW = Math.Max(60, _dragStartStartWidth - deltaX);
                    newX = _dragStartStartX + (_dragStartStartWidth - newW);
                    newH = Math.Max(60, _dragStartStartHeight - deltaY);
                    newY = _dragStartStartY + (_dragStartStartHeight - newH);
                    break;
            }

            if (_resizeTarget == ResizeTarget.Character)
            {
                node.CharacterX = newX;
                node.CharacterY = newY;
                node.CharacterWidth = newW;
                node.CharacterHeight = newH;
            }
            else if (_resizeTarget == ResizeTarget.DialogueBox)
            {
                node.DialogueBoxX = newX;
                node.DialogueBoxY = newY;
                node.DialogueBoxWidth = newW;
                node.DialogueBoxHeight = newH;
            }
            else if (_resizeTarget == ResizeTarget.Background)
            {
                node.BackgroundX = newX;
                node.BackgroundY = newY;
                node.BackgroundWidth = newW;
                node.BackgroundHeight = newH;
            }

            e.Handled = true;
        }

        private void OnResizePointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (_isResizing)
            {
                _isResizing = false;
                _resizeTarget = ResizeTarget.None;
                e.Pointer.Capture(null);
                if (DataContext is MainWindowViewModel mainVm)
                {
                    mainVm.SaveActiveStoryFile();
                }
                e.Handled = true;
            }
        }

        private void OnBackgroundPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (_isResizing) return;
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isDraggingBackground = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartStartX = mainVm.SelectedNode.BackgroundX;
                _dragStartStartY = mainVm.SelectedNode.BackgroundY;
                e.Pointer.Capture(sender as Control);
                e.Handled = true;
            }
        }

        private void OnBackgroundPointerMoved(object? sender, PointerEventArgs e)
        {
            if (_isResizing || !_isDraggingBackground || DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;

            mainVm.SelectedNode.BackgroundX = _dragStartStartX + deltaX;
            mainVm.SelectedNode.BackgroundY = _dragStartStartY + deltaY;
            e.Handled = true;
        }

        private void OnBackgroundPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (_isDraggingBackground)
            {
                _isDraggingBackground = false;
                e.Pointer.Capture(null);
                if (DataContext is MainWindowViewModel mainVm)
                {
                    mainVm.SaveActiveStoryFile();
                }
                e.Handled = true;
            }
        }

        private void OnCharacterPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (_isResizing) return;
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isDraggingCharacter = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartStartX = mainVm.SelectedNode.CharacterX;
                _dragStartStartY = mainVm.SelectedNode.CharacterY;
                e.Pointer.Capture(sender as Control);
                e.Handled = true;
            }
        }

        private void OnCharacterPointerMoved(object? sender, PointerEventArgs e)
        {
            if (_isResizing || !_isDraggingCharacter || DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;

            mainVm.SelectedNode.CharacterX = Math.Clamp(_dragStartStartX + deltaX, -DragLimitPadding, VirtualCanvasWidth);
            mainVm.SelectedNode.CharacterY = Math.Clamp(_dragStartStartY + deltaY, -DragLimitPadding, VirtualCanvasHeight);
            e.Handled = true;
        }

        private void OnCharacterPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (_isDraggingCharacter)
            {
                _isDraggingCharacter = false;
                e.Pointer.Capture(null);
                if (DataContext is MainWindowViewModel mainVm)
                {
                    mainVm.SaveActiveStoryFile();
                }
                e.Handled = true;
            }
        }

        private void OnDialogueBoxPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (_isResizing) return;
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isDraggingDialogueBox = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartStartX = mainVm.SelectedNode.DialogueBoxX;
                _dragStartStartY = mainVm.SelectedNode.DialogueBoxY;
                e.Pointer.Capture(sender as Control);
                e.Handled = true;
            }
        }

        private void OnDialogueBoxPointerMoved(object? sender, PointerEventArgs e)
        {
            if (_isResizing || !_isDraggingDialogueBox || DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;

            mainVm.SelectedNode.DialogueBoxX = Math.Clamp(_dragStartStartX + deltaX, -DragLimitPadding, VirtualCanvasWidth);
            mainVm.SelectedNode.DialogueBoxY = Math.Clamp(_dragStartStartY + deltaY, -DragLimitPadding, VirtualCanvasHeight);
            e.Handled = true;
        }

        private void OnDialogueBoxPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            if (_isDraggingDialogueBox)
            {
                _isDraggingDialogueBox = false;
                e.Pointer.Capture(null);
                if (DataContext is MainWindowViewModel mainVm)
                {
                    mainVm.SaveActiveStoryFile();
                }
                e.Handled = true;
            }
        }
    }
}
