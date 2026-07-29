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
        private double _characterX = 1440.0;

        [ObservableProperty]
        private double _characterY = 340.0;

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
            string fullPath = Path.Combine(dataDir, filename);
            if (File.Exists(fullPath))
            {
                try { return new Bitmap(fullPath); }
                catch { return null; }
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
