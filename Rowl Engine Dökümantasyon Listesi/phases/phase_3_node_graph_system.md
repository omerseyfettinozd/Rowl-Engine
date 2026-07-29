# 🚀 PHASE 3 EXECUTION PLAN: COMFYUI NODE GRAPH, INSPECTOR & MSDF TYPOGRAPHY

> **Phase Objective:** Build the interactive ComfyUI-Style Node Graph Editor in C# Avalonia, deploy the Right-Side Contextual Inspector Panel, and implement MSDF (Multi-channel Signed Distance Field) text rendering in the C++ Engine Runtime.

---

## 🏗️ 1. DIRECTORY STRUCTURE (VISUAL CANVAS & RENDERING)

```text
Node-Oyun-Motoru/
├── editor/
│   └── Src/
│       ├── Views/
│       │   ├── NodeGraphCanvasView.axaml      # Main interactive canvas UI
│       │   ├── NodeControl.axaml              # Individual draggable node control
│       │   └── InspectorPanelView.axaml       # Contextual right-side panel
│       └── Controls/
│           └── BezierWireRenderer.cs          # Custom canvas bezier connection drawer
└── engine/
    ├── include/rowl/render/
    │   ├── msdf_renderer.hpp                  # MSDF font parser & GPU texture binder
    │   └── layout_engine.hpp                  # Responsive percentage anchor calculator
    └── src/render/
        ├── msdf_renderer.cpp
        └── layout_engine.cpp
```

---

## 💻 2. COMFYUI NODE CANVAS IMPLEMENTATION BLUEPRINT (C# AVALONIA)

The canvas requires custom layout logic to draw connecting curves (Bezier wires) and track node dragging coordinates.

### A. Bezier Wire Drawing Logic (`editor/Src/Controls/BezierWireRenderer.cs`)
```csharp
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace RowlEngine.Editor.Controls
{
    public class BezierWireRenderer : Control
    {
        public static readonly StyledProperty<Point> StartPointProperty =
            AvaloniaProperty.Register<BezierWireRenderer, Point>(nameof(StartPoint));

        public static readonly StyledProperty<Point> EndPointProperty =
            AvaloniaProperty.Register<BezierWireRenderer, Point>(nameof(EndPoint));

        public Point StartPoint
        {
            get => GetValue(StartPointProperty);
            set => SetValue(StartPointProperty, value);
        }

        public Point EndPoint
        {
            get => GetValue(EndPointProperty);
            set => SetValue(EndPointProperty, value);
        }

        public override void Render(DrawingContext context)
        {
            // Calculate bezier control points for a smooth S-curve
            double dx = EndPoint.X - StartPoint.X;
            Point control1 = new Point(StartPoint.X + dx / 2, StartPoint.Y);
            Point control2 = new Point(EndPoint.X - dx / 2, EndPoint.Y);

            var geometry = new StreamGeometry();
            using (var ctx = geometry.Open())
            {
                ctx.BeginFigure(StartPoint, false);
                ctx.CubicBezierTo(control1, control2, EndPoint);
            }

            var pen = new Pen(Brushes.LightGreen, 3);
            context.DrawGeometry(null, pen, geometry);
        }
    }
}
```

---

## 🎨 3. MSDF TEXT RENDERING IMPLEMENTATION BLUEPRINT (C++ SHADER)

The C++ runtime uses a Multi-channel Signed Distance Field texture and a custom fragment shader to draw jilet-sharp characters.

### Fragment Shader Blueprint (`engine/src/shaders/msdf_text.frag`)
```glsl
#version 330 core
in vec2 v_tex_coords;
in vec4 v_color;
out vec4 frag_color;

uniform sampler2D u_msdf_atlas;
uniform float u_pixel_range; // Extracted range from font metadata

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // 1. Sample MSDF texture channels
    vec3 msdf_sample = texture(u_msdf_atlas, v_tex_coords).rgb;
    
    // 2. Decode signed distance from median channel
    float sd = median(msdf_sample.r, msdf_sample.g, msdf_sample.b);
    
    // 3. Convert distance to screen space pixels
    float screen_px_distance = u_pixel_range * (sd - 0.5);
    float opacity = clamp(screen_px_distance + 0.5, 0.0, 1.0);
    
    // 4. Output crisp text with color
    frag_color = vec4(v_color.rgb, v_color.a * opacity);
}
```

---

## 📐 4. LAYOUT MATHEMATICS (ANCHORS & PERCENTAGES)

To scale the VN interface perfectly regardless of target physical screen resolution (aspect ratio guardian logic):

$$x_{\text{render}} = w_{\text{viewport}} \cdot x_{\text{pct}} + x_{\text{offset\_offset}}$$

$$y_{\text{render}} = h_{\text{viewport}} \cdot y_{\text{pct}} + y_{\text{offset\_offset}}$$

- **9-Slice Scaling Formula:** Maintains border pixel dimensions ($W_{\text{border}}$) constant while stretching only the center content regions to eliminate UI texture warping.

---

## ✅ PHASE 3 ACCEPTANCE CRITERIA
- [ ] Nodes are draggable on the Avalonia canvas, and Bezier connection wires follow port pins cleanly in real-time.
- [ ] Selecting a node instantly updates the property bindings on the Right-Side Inspector Panel.
- [ ] C++ Engine loads an MSDF `.png` texture atlas and associated layout metadata successfully.
- [ ] Test dialogue text renders cleanly on the screen, scaling from 200px size down to 10px size without any blur or pixelation.
- [ ] Letterboxing/pillarboxing automatically snaps into place when the engine window is stretched to different aspect ratios.
