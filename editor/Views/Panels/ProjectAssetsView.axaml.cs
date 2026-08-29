using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Panels
{
    public partial class ProjectAssetsView : UserControl
    {
        private Point _dragStartPoint;
        private bool _isPointerPressed;
        private AssetNodeViewModel? _draggedNode;

        public ProjectAssetsView()
        {
            InitializeComponent();
        }

        private void OnAssetPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                if (sender is Control ctrl && ctrl.DataContext is AssetNodeViewModel node)
                {
                    // Don't drag while renaming
                    if (node.IsEditing) return;

                    _isPointerPressed = true;
                    _dragStartPoint = e.GetPosition(this);
                    _draggedNode = node;
                }
            }
        }

        private void OnAssetPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            _isPointerPressed = false;
            _draggedNode = null;
        }

        private async void OnAssetPointerMoved(object? sender, PointerEventArgs e)
        {
            if (!_isPointerPressed || _draggedNode == null) return;
            if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isPointerPressed = false;
                _draggedNode = null;
                return;
            }

            var currentPoint = e.GetPosition(this);
            var diff = _dragStartPoint - currentPoint;

            if (Math.Abs(diff.X) > 4 || Math.Abs(diff.Y) > 4)
            {
                _isPointerPressed = false;
                var node = _draggedNode;
                _draggedNode = null;

                var data = new DataObject();
                data.Set("AssetNode", node);
                data.Set("AssetFileName", node.Name);
                data.Set(DataFormats.Text, node.Name);
                if (!string.IsNullOrEmpty(node.FullPath) && File.Exists(node.FullPath))
                {
                    data.Set(DataFormats.Files, new[] { node.FullPath });
                }

                await DragDrop.DoDragDrop(e, data, DragDropEffects.Copy | DragDropEffects.Link);
            }
        }
    }
}
