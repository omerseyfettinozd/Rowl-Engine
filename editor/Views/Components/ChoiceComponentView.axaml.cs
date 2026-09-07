using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Views.Components
{
    public partial class ChoiceComponentView : UserControl
    {
        public ChoiceComponentView() => InitializeComponent();

        private static readonly string[] ImageExtensions =
            [".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tga"];

        private void OnAssetDragOver(object? sender, DragEventArgs e)
        {
            if (sender is not Border border) return;
            string? assetPath = GetDraggedAssetPath(e.DataTransfer);
            string extension = Path.GetExtension(assetPath ?? string.Empty).ToLowerInvariant();
            bool acceptsFont = string.Equals(border.Tag as string, "Font", StringComparison.Ordinal);
            bool valid = acceptsFont
                ? extension is ".ttf" or ".otf"
                : ImageExtensions.Contains(extension);

            e.DragEffects = valid ? DragDropEffects.Copy : DragDropEffects.None;
            e.Handled = true;
        }

        private void OnAssetDrop(object? sender, DragEventArgs e)
        {
            if (sender is not Border { DataContext: ChoiceOptionViewModel option } border) return;

            string? assetPath = GetDraggedAssetPath(e.DataTransfer);
            if (string.IsNullOrWhiteSpace(assetPath)) return;

            var main = TopLevel.GetTopLevel(this)?.DataContext as MainWindowViewModel;
            if (Path.IsPathRooted(assetPath) && File.Exists(assetPath))
            {
                // External image drops are copied into the project. Internal
                // Assets-tree drops already carry a portable Assets-relative path.
                if (ImageExtensions.Contains(Path.GetExtension(assetPath).ToLowerInvariant()) && main != null)
                    assetPath = main.ImportImageFileToProject(assetPath);
                else
                    return;
            }

            assetPath = assetPath.Replace('\\', '/');
            switch (border.Tag as string)
            {
                case "Font":
                    if (Path.GetExtension(assetPath).ToLowerInvariant() is not (".ttf" or ".otf")) return;
                    option.FontFamily = assetPath;
                    break;
                case "Normal":
                    if (!ImageExtensions.Contains(Path.GetExtension(assetPath).ToLowerInvariant())) return;
                    option.NormalImage = assetPath;
                    option.BackgroundImage = assetPath;
                    break;
                case "Hover": option.HoverImage = assetPath; break;
                case "Pressed": option.PressedImage = assetPath; break;
                case "Disabled": option.DisabledImage = assetPath; break;
                default: return;
            }

            main?.ScheduleSave();
            e.Handled = true;
        }

        private static string? GetDraggedAssetPath(IDataTransfer data)
        {
            return AssetDragData.GetPath(data)
                ?? data.TryGetFiles()?.FirstOrDefault()?.Path.LocalPath;
        }
    }
}
