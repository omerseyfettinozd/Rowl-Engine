using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for background layer rendering.
    /// Manages background texture, position, dimensions, and scale.
    /// </summary>
    public partial class BackgroundComponentViewModel : NodeComponentViewModel
    {
        private const double DefaultWidth = 1920.0;
        private const double DefaultHeight = 1080.0;

        public override string DisplayName => "Background Layer";
        public override string Icon => "🖼️";
        public override string TypeKey => "background";

        [ObservableProperty]
        private string _texture = "bg_beach_sunset.png";

        [ObservableProperty]
        private double _x = 0.0;

        [ObservableProperty]
        private double _y = 0.0;

        [ObservableProperty]
        private double _width = DefaultWidth;

        [ObservableProperty]
        private double _height = DefaultHeight;

        [ObservableProperty]
        private double _scale = 1.0;

        [ObservableProperty]
        private Bitmap? _textureBitmap;

        // ── Scale → Width/Height sync ──
        partial void OnScaleChanged(double value)
        {
            if (value <= 0) return;
            Width = DefaultWidth * value;
            Height = DefaultHeight * value;
        }

        partial void OnTextureChanged(string value) => RefreshBitmap();

        /// <summary>
        /// Reloads the bitmap from the centralized asset cache.
        /// </summary>
        public void RefreshBitmap()
        {
            TextureBitmap = RowlEngine.Editor.Services.AssetBitmapCache.GetOrLoad(Texture);
        }

        /// <summary>
        /// Resets position and dimensions to fullscreen defaults.
        /// </summary>
        public void ResetDimensions()
        {
            X = 0.0;
            Y = 0.0;
            Width = DefaultWidth;
            Height = DefaultHeight;
            Scale = 1.0;
        }

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["texture"] = Texture,
                ["x"] = X,
                ["y"] = Y,
                ["width"] = Width,
                ["height"] = Height,
                ["scale"] = Scale
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("texture", out var t) && t is string tex)
                Texture = tex;
            if (data.TryGetValue("x", out var xv)) X = Convert.ToDouble(xv);
            if (data.TryGetValue("y", out var yv)) Y = Convert.ToDouble(yv);
            if (data.TryGetValue("width", out var wv)) Width = Convert.ToDouble(wv);
            if (data.TryGetValue("height", out var hv)) Height = Convert.ToDouble(hv);
            if (data.TryGetValue("scale", out var sv)) Scale = Convert.ToDouble(sv);
            RefreshBitmap();
        }
    }
}
