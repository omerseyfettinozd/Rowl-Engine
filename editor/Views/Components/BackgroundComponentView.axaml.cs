using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
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

            if (isOver && IsValidImageDrop(e.Data))
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

        private bool IsValidImageDrop(IDataObject data)
        {
            if (data.Contains("AssetNode") || data.Contains("AssetFileName")) return true;

            if (data.Contains(DataFormats.Files))
            {
                var files = data.GetFiles();
                if (files != null && files.Any())
                {
                    string ext = Path.GetExtension(files.First().Path.LocalPath).ToLowerInvariant();
                    return ext is ".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga";
                }
            }

            if (data.Contains(DataFormats.Text))
            {
                string? text = data.GetText();
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

            // 1. From internal AssetNode
            if (e.Data.Get("AssetNode") is AssetNodeViewModel node)
            {
                importedFileName = node.Name;
            }
            else if (e.Data.Get("AssetFileName") is string fileName)
            {
                importedFileName = fileName;
            }
            // 2. From OS File or Full Path
            else if (e.Data.Contains(DataFormats.Files))
            {
                var files = e.Data.GetFiles();
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
            else if (e.Data.Contains(DataFormats.Text))
            {
                string? text = e.Data.GetText();
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
