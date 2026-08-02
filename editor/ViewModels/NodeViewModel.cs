using System;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using System.IO;
using System.Reflection;

namespace RowlEngine.Editor.ViewModels
{
    public partial class NodeViewModel : ObservableObject
    {
        // ── Path helpers (synced with MainWindowViewModel) ──
        private static string AssetsPath => MainWindowViewModel.AssetsPath;

        // ── Default constants ──
        private const double DefaultBackgroundWidth = 1920.0;
        private const double DefaultBackgroundHeight = 1080.0;
        private const double DefaultCharacterWidth = 360.0;
        private const double DefaultCharacterHeight = 540.0;
        private const double DefaultDialogueBoxWidth = 1760.0;
        private const double DefaultDialogueBoxHeight = 180.0;

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
        private double _backgroundWidth = DefaultBackgroundWidth;

        [ObservableProperty]
        private double _backgroundHeight = DefaultBackgroundHeight;

        [ObservableProperty]
        private double _backgroundScale = 1.0;

        [ObservableProperty]
        private double _characterX = 1440.0;

        [ObservableProperty]
        private double _characterY = 340.0;

        [ObservableProperty]
        private double _characterWidth = DefaultCharacterWidth;

        [ObservableProperty]
        private double _characterHeight = DefaultCharacterHeight;

        [ObservableProperty]
        private double _characterScale = 1.0;

        [ObservableProperty]
        private double _dialogueBoxX = 80.0;

        [ObservableProperty]
        private double _dialogueBoxY = 860.0;

        [ObservableProperty]
        private double _dialogueBoxWidth = DefaultDialogueBoxWidth;

        [ObservableProperty]
        private double _dialogueBoxHeight = DefaultDialogueBoxHeight;

        [ObservableProperty]
        private double _dialogueBoxScale = 1.0;

        // Separate reentrancy guards for each scale/width group
        private bool _updatingCharacterScale = false;
        private bool _updatingBackgroundScale = false;
        private bool _updatingDialogueBoxScale = false;

        // Character: Scale is source of truth → sets Width/Height
        partial void OnCharacterScaleChanged(double value)
        {
            if (_updatingCharacterScale || value <= 0) return;
            _updatingCharacterScale = true;
            CharacterWidth = DefaultCharacterWidth * value;
            CharacterHeight = DefaultCharacterHeight * value;
            _updatingCharacterScale = false;
        }

        // Character: Width change → back-calculate Scale only (don't re-trigger width)
        partial void OnCharacterWidthChanged(double value)
        {
            if (_updatingCharacterScale || value <= 0) return;
            _updatingCharacterScale = true;
            CharacterScale = Math.Round(value / DefaultCharacterWidth, 2);
            _updatingCharacterScale = false;
        }

        // Background: Scale is source of truth → sets Width/Height
        partial void OnBackgroundScaleChanged(double value)
        {
            if (_updatingBackgroundScale || value <= 0) return;
            _updatingBackgroundScale = true;
            BackgroundWidth = DefaultBackgroundWidth * value;
            BackgroundHeight = DefaultBackgroundHeight * value;
            _updatingBackgroundScale = false;
        }

        // Background: Width change → back-calculate Scale only
        partial void OnBackgroundWidthChanged(double value)
        {
            if (_updatingBackgroundScale || value <= 0) return;
            _updatingBackgroundScale = true;
            BackgroundScale = Math.Round(value / DefaultBackgroundWidth, 2);
            _updatingBackgroundScale = false;
        }

        // DialogueBox: Scale is source of truth → sets Width/Height
        partial void OnDialogueBoxScaleChanged(double value)
        {
            if (_updatingDialogueBoxScale || value <= 0) return;
            _updatingDialogueBoxScale = true;
            DialogueBoxWidth = DefaultDialogueBoxWidth * value;
            DialogueBoxHeight = DefaultDialogueBoxHeight * value;
            _updatingDialogueBoxScale = false;
        }

        // DialogueBox: Width change → back-calculate Scale only
        partial void OnDialogueBoxWidthChanged(double value)
        {
            if (_updatingDialogueBoxScale || value <= 0) return;
            _updatingDialogueBoxScale = true;
            DialogueBoxScale = Math.Round(value / DefaultDialogueBoxWidth, 2);
            _updatingDialogueBoxScale = false;
        }

        [ObservableProperty]
        private Bitmap? _backgroundBitmap;

        [ObservableProperty]
        private Bitmap? _characterBitmap;

        [ObservableProperty]
        private bool _isSelected;

        [ObservableProperty]
        private bool _isStartNode;

        [ObservableProperty]
        private string _borderColor = "#2A2A3D";

        partial void OnIsStartNodeChanged(bool value)
        {
            BorderColor = value ? "#10B981" : "#2A2A3D";
        }

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
            string[] searchPaths = new string[]
            {
                filename,
                Path.Combine(AssetsPath, filename),
                Path.Combine(AssetsPath, "images", filename),
                Path.Combine(AssetsPath, "images", Path.GetFileName(filename))
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