using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Player;

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

        // ── Faz 5 Dilim 3: katmanlı karakter kütüphanesi ──
        //
        // Dört sabit slot (body/face/outfit/accessory; sıra
        // CharacterLayersService.SlotOrder'dadır). Legacy tek-sprite
        // dosyalar layers'sız yüklenir ve sprite body'ye düşer; dolu
        // layers.body.sprite'i ezer (native migration aynası).

        [ObservableProperty]
        private string _layerBodyAsset = string.Empty;

        [ObservableProperty]
        private string _layerFaceAsset = string.Empty;

        [ObservableProperty]
        private string _layerOutfitAsset = string.Empty;

        [ObservableProperty]
        private string _layerAccessoryAsset = string.Empty;

        [ObservableProperty]
        private string? _selectedExpressionName;

        [ObservableProperty]
        private bool _isLayersBadgeVisible;

        [ObservableProperty]
        private string _layersBadgeText = string.Empty;

        private readonly Dictionary<string, Dictionary<string, string>> _expressions =
            new(StringComparer.Ordinal);

        /// <summary>Expression adları (sıralı, bağlanabilir).</summary>
        public ObservableCollection<string> ExpressionNames { get; } = new();

        /// <summary>Expression kütüphanesi: isim → slot→asset (salt-okunur görünüm).</summary>
        public IReadOnlyDictionary<string, IReadOnlyDictionary<string, string>> ExpressionPresets =>
            _expressions.ToDictionary(
                pair => pair.Key,
                pair => (IReadOnlyDictionary<string, string>)pair.Value,
                StringComparer.Ordinal);

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

        partial void OnLayerBodyAssetChanged(string value) => RefreshBitmap();
        partial void OnLayerFaceAssetChanged(string value) => RefreshBitmap();
        partial void OnLayerOutfitAssetChanged(string value) => RefreshBitmap();
        partial void OnLayerAccessoryAssetChanged(string value) => RefreshBitmap();

        partial void OnSelectedExpressionNameChanged(string? value)
        {
            if (!string.IsNullOrEmpty(value))
                ApplyExpression(value, null, out _);
        }

        /// <summary>
        /// Reloads the sprite bitmap from the centralized asset cache.
        /// Layer body setliyse o, yoksa legacy sprite gösterilir.
        /// </summary>
        public void RefreshBitmap()
        {
            string effective = !string.IsNullOrEmpty(LayerBodyAsset) ? LayerBodyAsset : Sprite;
            SpriteBitmap = RowlEngine.Editor.Services.AssetBitmapCache.GetOrLoad(effective);
        }

        /// <summary>
        /// Efektif katman haritası (dört slot, boş dahil; sıra sabit).
        /// </summary>
        public IReadOnlyDictionary<string, string> GetLayerAssets() =>
            new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["body"] = LayerBodyAsset ?? string.Empty,
                ["face"] = LayerFaceAsset ?? string.Empty,
                ["outfit"] = LayerOutfitAsset ?? string.Empty,
                ["accessory"] = LayerAccessoryAsset ?? string.Empty,
            };

        /// <summary>
        /// Tek slot yazar. Bilinmeyen slot ya da sözdizimi-bozuk/aşırı asset
        /// fail-closed reddedilir (durum değişmez, false döner).
        /// </summary>
        public bool SetLayerAsset(string? slot, string? asset)
        {
            if (!CharacterLayersService.TryGetSlotIndex(slot, out int index))
                return false;
            string candidate = asset ?? string.Empty;
            if (!CharacterLayersService.IsAssetPathSyntaxValid(candidate))
                return false;
            switch (index)
            {
                case 0: LayerBodyAsset = candidate; break;
                case 1: LayerFaceAsset = candidate; break;
                case 2: LayerOutfitAsset = candidate; break;
                default: LayerAccessoryAsset = candidate; break;
            }
            return true;
        }

        /// <summary>
        /// Expression preset ekler/değiştirir. Bozuk girdi (bilinmeyen slot,
        /// non-string, aşırı asset, geçersiz ad) fail-closed reddedilir.
        /// </summary>
        public bool UpsertExpression(string? name, string? expressionJson)
        {
            if (!CharacterLayersService.IsPresetNameValid(name) ||
                !CharacterLayersService.TryParsePreset(expressionJson, out Dictionary<string, string> slots) ||
                slots.Count == 0)
                return false;
            _expressions[name!] = slots;
            RebuildExpressionNames();
            return true;
        }

        /// <summary>Expression siler (yoksa false).</summary>
        public bool RemoveExpression(string? name)
        {
            if (string.IsNullOrEmpty(name) || !_expressions.Remove(name))
                return false;
            RebuildExpressionNames();
            if (string.Equals(SelectedExpressionName, name, StringComparison.Ordinal))
                SelectedExpressionName = null;
            return true;
        }

        /// <summary>
        /// Expression'ı önizlemeye atomik uygular: preset bozuk/çözülemezse
        /// HİÇBİR slot değişmez, tanı rozeti görünür, false döner. Temiz
        /// girdide slotlar güncellenir, rozet temizlenir ve (engine verildiyse)
        /// preset kaydı + uygulaması adaptör üzerinden forward edilir
        /// (ölü handle sessiz no-op). Null resolver yalnız-sözdizimi
        /// demektir (native aynası).
        /// </summary>
        public bool ApplyExpression(
            string? name, IPlayerEngine? engine, out string error,
            Func<string, bool>? isResolvable = null)
        {
            error = string.Empty;
            if (string.IsNullOrEmpty(name) || !_expressions.TryGetValue(name, out Dictionary<string, string>? preset))
            {
                error = $"Unknown expression '{name}'; layers unchanged.";
                ShowLayersBadge(error);
                return false;
            }
            if (!CharacterLayersService.TryBuildExpressionResult(
                    GetLayerAssets(), preset, out Dictionary<string, string> next, out error, isResolvable))
            {
                ShowLayersBadge(error);
                return false;
            }
            LayerBodyAsset = next["body"];
            LayerFaceAsset = next["face"];
            LayerOutfitAsset = next["outfit"];
            LayerAccessoryAsset = next["accessory"];
            ClearLayersBadge();
            if (engine is not null)
            {
                try
                {
                    engine.RegisterCharacterPreset(name, JsonSerializer.Serialize(preset));
                    engine.ApplyCharacterExpression(name);
                    string diagnosis = engine.GetLastCharacterError();
                    if (!string.IsNullOrEmpty(diagnosis))
                        ShowLayersBadge(diagnosis);
                }
                catch (Exception)
                {
                    // Ölü handle / kapalı native: yerel önizleme korunur.
                }
            }
            return true;
        }

        /// <summary>Rozet aynası (servis Describe; bozuk girdi gizlenir).</summary>
        public void UpdateLayersBadge(string? drawListJson)
        {
            CharacterLayersDescription description;
            try
            {
                description = CharacterLayersService.Describe(drawListJson);
            }
            catch (Exception)
            {
                description = CharacterLayersDescription.Hidden;
            }
            IsLayersBadgeVisible = description.Visible;
            LayersBadgeText = description.Text;
        }

        private void ShowLayersBadge(string diagnosis)
        {
            IsLayersBadgeVisible = true;
            LayersBadgeText = diagnosis ?? string.Empty;
        }

        private void ClearLayersBadge()
        {
            IsLayersBadgeVisible = false;
            LayersBadgeText = string.Empty;
        }

        private void RebuildExpressionNames()
        {
            ExpressionNames.Clear();
            foreach (string name in _expressions.Keys.OrderBy(key => key, StringComparer.Ordinal))
                ExpressionNames.Add(name);
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
            // layers HER ZAMAN dört slotu yazar (boş dahil): boş-body
            // ayrımı round-trip'te korunur, eksik anahtar legacy sayılır.
            var layers = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach ((string slot, string asset) in GetLayerAssets())
                layers[slot] = asset;
            var expressions = _expressions
                .OrderBy(pair => pair.Key, StringComparer.Ordinal)
                .Select(pair => (object)new Dictionary<string, object>
                {
                    ["name"] = pair.Key,
                    ["slots"] = new Dictionary<string, string>(pair.Value, StringComparer.Ordinal),
                })
                .ToList();
            return new Dictionary<string, object>
            {
                ["sprite"] = Sprite,
                ["layers"] = layers,
                ["expressions"] = expressions,
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
                // Faz 5 Dilim 3 migration: layers varsa body dahil ordan
                // gelir (dolu layers.body sprite'ı ezer; eksik body
                // sprite'a düşer); yoksa legacy sprite body'ye düşer.
                // Hidrasyon ham JSON string taşır, direkt sözlük de olur.
                if (data.TryGetValue("layers", out var layersValue) &&
                    CharacterLayersService.TryParseLayers(layersValue, out Dictionary<string, string> parsedLayers))
                {
                    LayerBodyAsset = parsedLayers.TryGetValue("body", out string? body) ? body : Sprite;
                    LayerFaceAsset = parsedLayers.TryGetValue("face", out string? face) ? face : string.Empty;
                    LayerOutfitAsset = parsedLayers.TryGetValue("outfit", out string? outfit) ? outfit : string.Empty;
                    LayerAccessoryAsset = parsedLayers.TryGetValue("accessory", out string? accessory) ? accessory : string.Empty;
                }
                else
                {
                    LayerBodyAsset = Sprite;
                    LayerFaceAsset = string.Empty;
                    LayerOutfitAsset = string.Empty;
                    LayerAccessoryAsset = string.Empty;
                }
                _expressions.Clear();
                if (data.TryGetValue("expressions", out var expressionsValue))
                {
                    foreach ((string name, IReadOnlyDictionary<string, string> preset) in
                             CharacterLayersService.ParseExpressionLibrary(expressionsValue))
                        _expressions[name] = new Dictionary<string, string>(preset, StringComparer.Ordinal);
                }
                RebuildExpressionNames();
                SelectedExpressionName = null;
                ClearLayersBadge();
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
