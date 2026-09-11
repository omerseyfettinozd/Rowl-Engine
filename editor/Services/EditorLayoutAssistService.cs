using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Provides OBS-style alignment, coordinate snapping, and asset import services for the editor scene.
    /// </summary>
    public static class EditorLayoutAssistService
    {
        public static void FitBackgroundToScreen(NodeViewModel? node)
        {
            if (node == null) return;
            node.BackgroundX = 0;
            node.BackgroundY = 0;
            node.BackgroundWidth = 1920;
            node.BackgroundHeight = 1080;
            node.BackgroundScale = 1.0;
        }

        public static bool CenterSelectedElement(NodeViewModel? node, out string elementDescription)
        {
            elementDescription = string.Empty;
            if (node == null) return false;

            var charComp = node.GetComponent<CharacterComponentViewModel>();
            var bgComp = node.GetComponent<BackgroundComponentViewModel>();

            if (charComp != null)
            {
                charComp.X = (1920 - charComp.Width) / 2.0;
                charComp.Y = 1080 - charComp.Height - 30; // ground baseline
                elementDescription = $"Karakter ortaya hizalandı (X: {charComp.X:0}, Y: {charComp.Y:0})";
                return true;
            }
            if (bgComp != null)
            {
                bgComp.X = (1920 - bgComp.Width) / 2.0;
                bgComp.Y = (1080 - bgComp.Height) / 2.0;
                elementDescription = $"Arka plan merkeze hizalandı (X: {bgComp.X:0}, Y: {bgComp.Y:0})";
                return true;
            }
            return false;
        }

        public static void AlignCharacterToBottom(NodeViewModel? node)
        {
            if (node == null) return;
            foreach (var charComp in node.CharacterComponents)
            {
                charComp.Y = 1080 - charComp.Height - 20;
            }
        }

        public static void ResetCharacterSize(NodeViewModel? node, CharacterComponentViewModel? charComp = null)
        {
            if (charComp == null && node != null)
                charComp = node.GetComponent<CharacterComponentViewModel>();
            if (charComp == null) return;

            charComp.Width = 600;
            charComp.Height = 900;
            charComp.Scale = 1.0;
            charComp.Y = 1080 - 900 - 20;
        }

        public static void PresetDialogueBox(NodeViewModel? node, string preset)
        {
            if (node == null) return;
            var dlg = node.GetComponent<DialogueComponentViewModel>();
            if (dlg == null) return;

            if (preset == "BottomBanner")
            {
                dlg.X = 100;
                dlg.Y = 820;
                dlg.Width = 1720;
                dlg.Height = 220;
            }
            else if (preset == "Center" || preset == "CenterBox")
            {
                dlg.X = (1920 - dlg.Width) / 2.0;
                dlg.Y = (1080 - dlg.Height) / 2.0;
            }
            else if (preset == "Square")
            {
                dlg.Width = 500;
                dlg.Height = 500;
            }
            else if (preset == "Standard")
            {
                dlg.X = 80;
                dlg.Y = 860;
                dlg.Width = 1760;
                dlg.Height = 180;
            }
        }

        public static void ResetCharacterDimensions(NodeViewModel? node)
        {
            if (node == null) return;
            node.CharacterWidth = 360.0;
            node.CharacterHeight = 540.0;
            node.CharacterScale = 1.0;
        }

        public static void ResetSceneRotation(NodeViewModel? node)
        {
            if (node == null) return;
            var bg = node.GetComponent<BackgroundComponentViewModel>();
            if (bg != null) bg.ResetRotation();
            foreach (var ch in node.CharacterComponents)
            {
                ch.ResetRotation();
            }
        }

        public static string ImportImageFileToProject(string fullPath, string projectAssetsPath)
        {
            string fileName = Path.GetFileName(fullPath);
            string assetsImagesFolder = Path.Combine(projectAssetsPath, "images");
            Directory.CreateDirectory(assetsImagesFolder);

            string destPath = Path.Combine(assetsImagesFolder, fileName);
            if (!string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
            {
                File.Copy(fullPath, destPath, overwrite: true);
            }
            return fileName;
        }
    }
}
