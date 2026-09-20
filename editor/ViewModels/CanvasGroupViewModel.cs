using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// Faz 4 Dilim 3 — canvas group frame (comment box). Pure editor
    /// metadata: title, color, rect and member node ids. Carries no runtime
    /// semantics by construction — the runtime loader keeps the data but
    /// never acts on it (see GRAPH_VNEXT_CONTRACT).
    /// </summary>
    public partial class CanvasGroupViewModel : ObservableObject
    {
        /// <summary>Smallest allowed frame size (canvas px).</summary>
        public const double MinWidth = 80.0;

        /// <summary>Smallest allowed frame size (canvas px).</summary>
        public const double MinHeight = 60.0;

        /// <summary>Default padding used when framing nodes (canvas px).</summary>
        public const double DefaultFramePadding = 48.0;

        [ObservableProperty]
        private string _groupId = string.Empty;

        [ObservableProperty]
        private string _title = string.Empty;

        // Grup rengi oyun-içeriğidir (proje verisine kaydedilir, kullanıcı
        // değiştirebilir); editör kromu DEĞİL — lint istisnası bu dosyayla sınırlıdır.
        internal const string DefaultColor = "#3B82F6";

        [ObservableProperty]
        private string _color = DefaultColor;

        [ObservableProperty]
        private double _x;

        [ObservableProperty]
        private double _y;

        [ObservableProperty]
        private double _width = 320.0;

        [ObservableProperty]
        private double _height = 240.0;

        /// <summary>Member node ids (groups may overlap: one node, many groups).</summary>
        public ObservableCollection<ulong> MemberNodeIds { get; } = new();

        public CanvasGroupViewModel()
        {
        }

        public CanvasGroupViewModel(string id, string title, string color,
            double x, double y, double width, double height,
            IEnumerable<ulong>? members = null)
        {
            GroupId = id;
            Title = title;
            Color = string.IsNullOrEmpty(color) ? DefaultColor : color;
            X = x;
            Y = y;
            Width = Math.Max(MinWidth, width);
            Height = Math.Max(MinHeight, height);
            if (members is not null)
                foreach (ulong member in members)
                    MemberNodeIds.Add(member);
        }

        /// <summary>Canvas-space hit test (edges count as inside).</summary>
        public bool Contains(double canvasX, double canvasY) =>
            canvasX >= X && canvasX <= X + Width &&
            canvasY >= Y && canvasY <= Y + Height;

        /// <summary>Viewport-culling test (edge-touching counts as visible).</summary>
        public bool Intersects(double rectX, double rectY, double rectW, double rectH) =>
            X <= rectX + rectW && X + Width >= rectX &&
            Y <= rectY + rectH && Y + Height >= rectY;

        /// <summary>
        /// Computes the tightest frame around the given node bounds plus
        /// padding, clamped to the minimum frame size.
        /// </summary>
        public static (double x, double y, double w, double h) FrameAroundBounds(
            IEnumerable<(double x, double y, double w, double h)> bounds,
            double padding = DefaultFramePadding)
        {
            double minX = double.PositiveInfinity, minY = double.PositiveInfinity;
            double maxX = double.NegativeInfinity, maxY = double.NegativeInfinity;
            int count = 0;
            foreach (var (x, y, w, h) in bounds)
            {
                minX = Math.Min(minX, x);
                minY = Math.Min(minY, y);
                maxX = Math.Max(maxX, x + w);
                maxY = Math.Max(maxY, y + h);
                count++;
            }
            if (count == 0)
                return (0, 0, MinWidth, MinHeight);
            double pad = Math.Max(0, padding);
            return (
                minX - pad,
                minY - pad,
                Math.Max(MinWidth, (maxX - minX) + pad * 2),
                Math.Max(MinHeight, (maxY - minY) + pad * 2));
        }

        public CanvasGroup ToRecord() => new(
            GroupId, Title, Color, X, Y, Width, Height,
            MemberNodeIds.ToList());

        public static CanvasGroupViewModel FromRecord(CanvasGroup record) =>
            new(record.Id, record.Title, record.Color,
                record.X, record.Y, record.Width, record.Height,
                record.NodeIds);
    }
}
