using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Views.Components
{
    public partial class BackgroundComponentView : UserControl
    {
        public BackgroundComponentView()
        {
            InitializeComponent();
            AddHandler(DragDrop.DragEnterEvent, OnDragEnter);
            AddHandler(DragDrop.DragOverEvent, OnDragOver);
            AddHandler(DragDrop.DragLeaveEvent, OnDragLeave);
            AddHandler(DragDrop.DropEvent, OnDrop);
        }

        private void OnDragEnter(object? sender, DragEventArgs e)
        {
            UpdateVisualFeedback(e, true);
        }

        private void OnDragOver(object? sender, DragEventArgs e)
        {
            UpdateVisualFeedback(e, true);
        }

        private void OnDragLeave(object? sender, DragEventArgs e)
        {
            UpdateVisualFeedback(e, false);
        }

        private void UpdateVisualFeedback(DragEventArgs e, bool isOver)
        {
            var dropBorder = this.FindControl<Border>("DropZoneBorder");
            if (dropBorder == null) return;

            if (isOver && IsValidImageDrop(e.DataTransfer))
            {
                dropBorder.BorderBrush = Brush.Parse("#00F0FF");
                dropBorder.BorderThickness = new Thickness(2);
                e.DragEffects = DragDropEffects.Copy;
                e.Handled = true;
            }
            else
            {
                dropBorder.BorderBrush = this.FindResource("BorderColor") as IBrush ?? Brush.Parse("#334155");
                dropBorder.BorderThickness = new Thickness(1.5);
                if (!isOver)
                {
                    e.DragEffects = DragDropEffects.None;
                }
            }
        }

        private bool IsValidImageDrop(IDataTransfer data)
        {
            if (AssetDragData.Contains(data)) return true;

            if (data.Contains(DataFormat.File))
            {
                var files = data.TryGetFiles();
                if (files != null && files.Any())
                {
                    string ext = Path.GetExtension(files.First().Path.LocalPath).ToLowerInvariant();
                    return ext is ".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga";
                }
            }

            if (data.Contains(DataFormat.Text))
            {
                string? text = data.TryGetText();
                if (!string.IsNullOrEmpty(text))
                {
                    string ext = Path.GetExtension(text).ToLowerInvariant();
                    return ext is ".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga";
                }
            }

            return false;
        }

        private void OnDrop(object? sender, DragEventArgs e)
        {
            UpdateVisualFeedback(e, false);

            if (DataContext is not BackgroundComponentViewModel bgComp) return;
            var window = (Avalonia.Application.Current?.ApplicationLifetime as Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime)?.MainWindow;
            var mainVm = window?.DataContext as MainWindowViewModel;

            string? importedFileName = null;

            // 1. From the internal asset browser
            if (AssetDragData.GetPath(e.DataTransfer) is string fileName)
            {
                importedFileName = fileName;
            }
            // 2. From OS File or Full Path
            else if (e.DataTransfer.Contains(DataFormat.File))
            {
                var files = e.DataTransfer.TryGetFiles();
                if (files != null && files.Any())
                {
                    string fullPath = files.First().Path.LocalPath;
                    if (mainVm != null)
                        importedFileName = mainVm.ImportImageFileToProject(fullPath);
                    else
                        importedFileName = Path.GetFileName(fullPath);
                }
            }
            // 3. From text
            else if (e.DataTransfer.Contains(DataFormat.Text))
            {
                string? text = e.DataTransfer.TryGetText();
                if (!string.IsNullOrEmpty(text))
                    importedFileName = Path.GetFileName(text);
            }

            if (!string.IsNullOrEmpty(importedFileName))
            {
                bgComp.Texture = importedFileName;
                bgComp.RefreshBitmap();

                mainVm?.AssetBrowserViewModel.RefreshAssets();
                mainVm?.ScheduleSave();
                if (mainVm?.SelectedNode != null)
                    mainVm.PushSceneToEngine(mainVm.SelectedNode);

                e.Handled = true;
            }
        }
    }
}
