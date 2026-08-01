using System;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using System.IO;

namespace RowlEngine.Editor.ViewModels
{
    public partial class NodeViewModel : ObservableObject
    {
        [ObservableProperty]
        private ulong _id;

        [ObservableProperty]
        private string _title = string.Empty;

        [ObservableProperty]
        private double _x;

        [ObservableProperty]
        private double _y;

        [ObservableProperty]
        private string _speaker = "Evelyn";

        [ObservableProperty]
        private string _dialogueText = string.Empty;

        [ObservableProperty]
        private string _backgroundTexture = "bg_beach_sunset.png";

        [ObservableProperty]
        private string _characterSprite = "spr_evelyn.png";

        [ObservableProperty]
        private string _dspFilter = "Normal";

        [ObservableProperty]
        private string _characterPosition = "Right";

        partial void OnCharacterPositionChanged(string value)
        {
            if (value == "Left") CharacterX = 120.0;
            else if (value == "Center") CharacterX = 780.0;
            else if (value == "Right") CharacterX = 1440.0;
        }

        [ObservableProperty]
        private double _backgroundX = 0.0;

        [ObservableProperty]
        private double _backgroundY = 0.0;

        [ObservableProperty]
        private double _backgroundWidth = 1920.0;

        [ObservableProperty]
        private double _backgroundHeight = 1080.0;

        [ObservableProperty]
        private double _backgroundScale = 1.0;

        [ObservableProperty]
        private double _characterX = 1440.0;

        [ObservableProperty]
        private double _characterY = 340.0;

        [ObservableProperty]
        private double _characterWidth = 360.0;

        [ObservableProperty]
        private double _characterHeight = 540.0;

        [ObservableProperty]
        private double _characterScale = 1.0;

        [ObservableProperty]
        private double _dialogueBoxX = 80.0;

        [ObservableProperty]
        private double _dialogueBoxY = 860.0;

        [ObservableProperty]
        private double _dialogueBoxWidth = 1760.0;

        [ObservableProperty]
        private double _dialogueBoxHeight = 180.0;

        [ObservableProperty]
        private double _dialogueBoxScale = 1.0;

        private bool _isUpdatingScaleInternal = false;

        partial void OnCharacterScaleChanged(double value)
        {
            if (_isUpdatingScaleInternal || value <= 0) return;
            _isUpdatingScaleInternal = true;
            CharacterWidth = 360.0 * value;
            CharacterHeight = 540.0 * value;
            _isUpdatingScaleInternal = false;
        }

        partial void OnBackgroundScaleChanged(double value)
        {
            if (_isUpdatingScaleInternal || value <= 0) return;
            _isUpdatingScaleInternal = true;
            BackgroundWidth = 1920.0 * value;
            BackgroundHeight = 1080.0 * value;
            _isUpdatingScaleInternal = false;
        }

        partial void OnDialogueBoxScaleChanged(double value)
        {
            if (_isUpdatingScaleInternal || value <= 0) return;
            _isUpdatingScaleInternal = true;
            DialogueBoxWidth = 1760.0 * value;
            DialogueBoxHeight = 180.0 * value;
            _isUpdatingScaleInternal = false;
        }

        partial void OnCharacterWidthChanged(double value)
        {
            if (_isUpdatingScaleInternal) return;
            _isUpdatingScaleInternal = true;
            CharacterScale = Math.Round(value / 360.0, 2);
            _isUpdatingScaleInternal = false;
        }

        partial void OnBackgroundWidthChanged(double value)
        {
            if (_isUpdatingScaleInternal) return;
            _isUpdatingScaleInternal = true;
            BackgroundScale = Math.Round(value / 1920.0, 2);
            _isUpdatingScaleInternal = false;
        }

        partial void OnDialogueBoxWidthChanged(double value)
        {
            if (_isUpdatingScaleInternal) return;
            _isUpdatingScaleInternal = true;
            DialogueBoxScale = Math.Round(value / 1760.0, 2);
            _isUpdatingScaleInternal = false;
        }

        [ObservableProperty]
        private Bitmap? _backgroundBitmap;

        [ObservableProperty]
        private Bitmap? _characterBitmap;

        [ObservableProperty]
        private bool _isSelected;

        partial void OnBackgroundTextureChanged(string value) => RefreshBitmaps();
        partial void OnCharacterSpriteChanged(string value) => RefreshBitmaps();

        public void RefreshBitmaps()
        {
            BackgroundBitmap = LoadBitmap(BackgroundTexture);
            CharacterBitmap = LoadBitmap(CharacterSprite);
        }

        private static Bitmap? LoadBitmap(string filename)
        {
            if (string.IsNullOrWhiteSpace(filename)) return null;
            string dataDir = "/home/chaple/Belgeler/Rowl Engine/data";
            string[] searchPaths = new string[]
            {
                filename,
                Path.Combine(dataDir, filename),
                Path.Combine(dataDir, "images", filename),
                Path.Combine(dataDir, "images", Path.GetFileName(filename))
            };

            foreach (var p in searchPaths)
            {
                if (File.Exists(p))
                {
                    try { return new Bitmap(p); }
                    catch { }
                }
            }
            return null;
        }

        public NodeViewModel(ulong id, string title, double x, double y)
        {
            Id = id;
            Title = title;
            X = x;
            Y = y;
            RefreshBitmaps();
        }
    }
}
