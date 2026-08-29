using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for character sprite rendering.
    /// Supports position presets (Left/Center/Right) and manual transform.
    /// Multiple instances can be attached to a single node for multi-character scenes.
    /// </summary>
    public partial class CharacterComponentViewModel : NodeComponentViewModel
    {
        private const double DefaultWidth = 360.0;
        private const double DefaultHeight = 540.0;

        public override string DisplayName => "Character Sprite";
        public override string Icon => "👤";
        public override string TypeKey => "character";

        [ObservableProperty]
        private string _sprite = "spr_evelyn.png";

        [ObservableProperty]
        private string _position = "Right";

        [ObservableProperty]
        private double _x = 1440.0;

        [ObservableProperty]
        private double _y = 340.0;

        [ObservableProperty]
        private double _width = DefaultWidth;

        [ObservableProperty]
        private double _height = DefaultHeight;

        [ObservableProperty]
        private double _scale = 1.0;

        [ObservableProperty]
        private Bitmap? _spriteBitmap;

        // ── Scale → Width/Height sync ──
        partial void OnScaleChanged(double value)
        {
            if (value <= 0) return;
            Width = DefaultWidth * value;
            Height = DefaultHeight * value;
        }

        partial void OnSpriteChanged(string value) => RefreshBitmap();

        /// <summary>
        /// Reloads the sprite bitmap from the centralized asset cache.
        /// </summary>
        public void RefreshBitmap()
        {
            SpriteBitmap = RowlEngine.Editor.Services.AssetBitmapCache.GetOrLoad(Sprite);
        }

        /// <summary>
        /// Resets dimensions to default values.
        /// </summary>
        public void ResetDimensions()
        {
            Width = DefaultWidth;
            Height = DefaultHeight;
            Scale = 1.0;
        }

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["sprite"] = Sprite,
                ["position"] = Position,
                ["x"] = X,
                ["y"] = Y,
                ["width"] = Width,
                ["height"] = Height,
                ["scale"] = Scale
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("sprite", out var s) && s is string sprite)
                Sprite = sprite;
            if (data.TryGetValue("position", out var p) && p is string pos)
                Position = pos;
            if (data.TryGetValue("x", out var xv)) X = Convert.ToDouble(xv);
            if (data.TryGetValue("y", out var yv)) Y = Convert.ToDouble(yv);
            if (data.TryGetValue("width", out var wv)) Width = Convert.ToDouble(wv);
            if (data.TryGetValue("height", out var hv)) Height = Convert.ToDouble(hv);
            if (data.TryGetValue("scale", out var sv)) Scale = Convert.ToDouble(sv);
            RefreshBitmap();
        }
    }
}
