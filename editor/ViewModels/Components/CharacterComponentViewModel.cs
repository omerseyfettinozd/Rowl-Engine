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

        private bool _isUpdatingDimensions = false;

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
        private double _scaleX = 1.0;

        [ObservableProperty]
        private double _scaleY = 1.0;

        [ObservableProperty]
        private double _rotation = 0.0;

        [ObservableProperty]
        private bool _maintainAspectRatio = true;

        [ObservableProperty]
        private Bitmap? _spriteBitmap;

        // Character Default Voice Blip Settings (Milestone 25)
        [ObservableProperty]
        private string _voiceBlipSound = string.Empty;

        [ObservableProperty]
        private double _voiceBlipPitch = 1.0;

        [ObservableProperty]
        private double _voiceBlipVariance = 0.08;

        [ObservableProperty]
        private int _voiceBlipCadence = 1;

        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void PreviewVoiceBlip()
        {
            DialogueComponentViewModel.GlobalPreviewVoiceBlipAction?.Invoke(VoiceBlipSound, (float)VoiceBlipPitch, 0.85f, 1);
        }

        // ── Scale & Dimension Sync ──
        partial void OnScaleChanged(double value)
        {
            if (_isUpdatingDimensions || value <= 0) return;
            _isUpdatingDimensions = true;
            try
            {
                ScaleX = value;
                ScaleY = value;
                Width = DefaultWidth * value;
                Height = DefaultHeight * value;
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
        }

        partial void OnScaleXChanged(double value)
        {
            if (_isUpdatingDimensions || value <= 0) return;
            _isUpdatingDimensions = true;
            try
            {
                if (MaintainAspectRatio)
                {
                    ScaleY = value;
                    Scale = value;
                    Width = DefaultWidth * value;
                    Height = DefaultHeight * value;
                }
                else
                {
                    Width = DefaultWidth * value;
                }
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
        }

        partial void OnScaleYChanged(double value)
        {
            if (_isUpdatingDimensions || value <= 0) return;
            _isUpdatingDimensions = true;
            try
            {
                if (MaintainAspectRatio)
                {
                    ScaleX = value;
                    Scale = value;
                    Width = DefaultWidth * value;
                    Height = DefaultHeight * value;
                }
                else
                {
                    Height = DefaultHeight * value;
                }
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
        }

        partial void OnWidthChanged(double value)
        {
            if (_isUpdatingDimensions || value <= 0) return;
            _isUpdatingDimensions = true;
            try
            {
                double sx = value / DefaultWidth;
                ScaleX = Math.Round(sx, 3);
                if (MaintainAspectRatio)
                {
                    Scale = ScaleX;
                    ScaleY = ScaleX;
                    Height = DefaultHeight * ScaleX;
                }
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
        }

        partial void OnHeightChanged(double value)
        {
            if (_isUpdatingDimensions || value <= 0) return;
            _isUpdatingDimensions = true;
            try
            {
                double sy = value / DefaultHeight;
                ScaleY = Math.Round(sy, 3);
                if (MaintainAspectRatio)
                {
                    Scale = ScaleY;
                    ScaleX = ScaleY;
                    Width = DefaultWidth * ScaleY;
                }
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
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
            _isUpdatingDimensions = true;
            try
            {
                Width = DefaultWidth;
                Height = DefaultHeight;
                Scale = 1.0;
                ScaleX = 1.0;
                ScaleY = 1.0;
                Rotation = 0.0;
                MaintainAspectRatio = true;
                VoiceBlipSound = string.Empty;
                VoiceBlipPitch = 1.0;
                VoiceBlipVariance = 0.08;
                VoiceBlipCadence = 1;
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
        }

        /// <summary>
        /// Resets only the rotation angle back to 0 (upright).
        /// </summary>
        public void ResetRotation()
        {
            Rotation = 0.0;
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
                ["scale"] = Scale,
                ["scale_x"] = ScaleX,
                ["scale_y"] = ScaleY,
                ["rotation"] = Rotation,
                ["maintain_aspect_ratio"] = MaintainAspectRatio,
                ["voice_blip_sound"] = VoiceBlipSound,
                ["voice_blip_pitch"] = VoiceBlipPitch,
                ["voice_blip_variance"] = VoiceBlipVariance,
                ["voice_blip_cadence"] = VoiceBlipCadence
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            _isUpdatingDimensions = true;
            try
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
                if (data.TryGetValue("scale_x", out var sxv)) ScaleX = Convert.ToDouble(sxv);
                else ScaleX = Scale;
                if (data.TryGetValue("scale_y", out var syv)) ScaleY = Convert.ToDouble(syv);
                else ScaleY = Scale;
                if (data.TryGetValue("rotation", out var rv)) Rotation = Convert.ToDouble(rv);
                if (data.TryGetValue("maintain_aspect_ratio", out var marv)) MaintainAspectRatio = Convert.ToBoolean(marv);
                if (data.TryGetValue("voice_blip_sound", out var vbs) && vbs is string vbSound)
                    VoiceBlipSound = vbSound;
                else if (data.TryGetValue("typewriter_sound", out var tws) && tws is string twSound)
                    VoiceBlipSound = twSound;
                if (data.TryGetValue("voice_blip_pitch", out var vbp) && vbp != null)
                    VoiceBlipPitch = Convert.ToDouble(vbp);
                if (data.TryGetValue("voice_blip_variance", out var vbv) && vbv != null)
                    VoiceBlipVariance = Convert.ToDouble(vbv);
                if (data.TryGetValue("voice_blip_cadence", out var vbc) && vbc != null)
                    VoiceBlipCadence = Convert.ToInt32(vbc);
            }
            finally
            {
                _isUpdatingDimensions = false;
            }
            RefreshBitmap();
        }
    }
}
