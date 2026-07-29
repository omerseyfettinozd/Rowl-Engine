using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;
using System;

namespace RowlEngine.Editor.Views
{
    public partial class LivePreviewControl : UserControl
    {
        private bool _isDraggingCharacter = false;
        private bool _isDraggingDialogueBox = false;

        private Point _dragStartPointerCanvasPos;
        private double _dragStartCharX;
        private double _dragStartCharY;
        private double _dragStartBoxX;
        private double _dragStartBoxY;

        public LivePreviewControl()
        {
            InitializeComponent();

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
        }

        private Canvas? GetViewportCanvas()
        {
            return this.FindControl<Canvas>("ViewportCanvas");
        }

        private void OnCharacterPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isDraggingCharacter = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartCharX = mainVm.SelectedNode.CharacterX;
                _dragStartCharY = mainVm.SelectedNode.CharacterY;
                e.Pointer.Capture(sender as Control);
                e.Handled = true;
            }
        }

        private void OnCharacterPointerMoved(object? sender, PointerEventArgs e)
        {
            if (!_isDraggingCharacter || DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;

            mainVm.SelectedNode.CharacterX = Math.Clamp(_dragStartCharX + deltaX, -100, 1920 - 100);
            mainVm.SelectedNode.CharacterY = Math.Clamp(_dragStartCharY + deltaY, -100, 1080 - 100);
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
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isDraggingDialogueBox = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartBoxX = mainVm.SelectedNode.DialogueBoxX;
                _dragStartBoxY = mainVm.SelectedNode.DialogueBoxY;
                e.Pointer.Capture(sender as Control);
                e.Handled = true;
            }
        }

        private void OnDialogueBoxPointerMoved(object? sender, PointerEventArgs e)
        {
            if (!_isDraggingDialogueBox || DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;

            mainVm.SelectedNode.DialogueBoxX = Math.Clamp(_dragStartBoxX + deltaX, 0, 1920 - 400);
            mainVm.SelectedNode.DialogueBoxY = Math.Clamp(_dragStartBoxY + deltaY, 0, 1080 - 100);
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
