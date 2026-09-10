using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for 2D camera control in visual novel scenes.
    /// Manages focus position, zoom, rotation, smooth tweening, and screen shake.
    /// </summary>
    public partial class CameraComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "2D Camera";
        public override string Icon => "🎥";
        public override string TypeKey => "camera";

        [ObservableProperty]
        private double _x = 960.0;

        [ObservableProperty]
        private double _y = 540.0;

        [ObservableProperty]
        private double _zoom = 1.0;

        [ObservableProperty]
        private double _rotation = 0.0;

        [ObservableProperty]
        private double _panDuration = 0.0;

        [ObservableProperty]
        private double _zoomDuration = 0.0;

        [ObservableProperty]
        private string _easing = "ease_in_out";

        [ObservableProperty]
        private double _shakeIntensity = 0.0;

        [ObservableProperty]
        private double _shakeDuration = 0.0;

        [ObservableProperty]
        private double _shakeFrequency = 25.0;

        public static IReadOnlyList<string> AvailableEasings { get; } = new[]
        {
            "linear",
            "ease_in",
            "ease_out",
            "ease_in_out",
            "smooth_step"
        };

        public void ResetToCenter()
        {
            X = 960.0;
            Y = 540.0;
            Zoom = 1.0;
            Rotation = 0.0;
            PanDuration = 0.0;
            ZoomDuration = 0.0;
            ShakeIntensity = 0.0;
            ShakeDuration = 0.0;
            ShakeFrequency = 25.0;
        }

        public override Dictionary<string, object> Serialize()
        {
            var dict = new Dictionary<string, object>
            {
                ["x"] = X,
                ["y"] = Y,
                ["zoom"] = Zoom,
                ["rotation"] = Rotation,
                ["pan_duration"] = PanDuration,
                ["zoom_duration"] = ZoomDuration,
                ["easing"] = Easing
            };

            if (ShakeIntensity > 0.0 && ShakeDuration > 0.0)
            {
                dict["shake_intensity"] = ShakeIntensity;
                dict["shake_duration"] = ShakeDuration;
                dict["shake_frequency"] = ShakeFrequency;
            }

            return dict;
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("x", out var xv)) X = Convert.ToDouble(xv);
            if (data.TryGetValue("y", out var yv)) Y = Convert.ToDouble(yv);
            if (data.TryGetValue("zoom", out var zv)) Zoom = Convert.ToDouble(zv);
            if (data.TryGetValue("rotation", out var rv)) Rotation = Convert.ToDouble(rv);
            if (data.TryGetValue("pan_duration", out var pdv)) PanDuration = Convert.ToDouble(pdv);
            if (data.TryGetValue("zoom_duration", out var zdv)) ZoomDuration = Convert.ToDouble(zdv);
            if (data.TryGetValue("easing", out var ev) && ev is string es) Easing = es;
            if (data.TryGetValue("shake_intensity", out var siv)) ShakeIntensity = Convert.ToDouble(siv);
            if (data.TryGetValue("shake_duration", out var sdv)) ShakeDuration = Convert.ToDouble(sdv);
            if (data.TryGetValue("shake_frequency", out var sfv)) ShakeFrequency = Convert.ToDouble(sfv);
        }
    }
}
