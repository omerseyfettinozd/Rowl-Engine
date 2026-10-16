using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Data;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Views.Workspace;

/// <summary>
/// Faz 5 D4 — the pin and close (×) buttons of a workspace panel header.
/// <see cref="WorkspacePanel"/> draws one at the trailing end of its header; a
/// panel that owns its header (Hierarchy, Inspector) places one inside that
/// header instead (<c>&lt;ws:PanelHeaderActions DockPanel.Dock="Right"/&gt;</c>,
/// with <see cref="WorkspacePanel.HeaderActionsInContent"/>). It finds its panel
/// on attach and hides itself outside a workspace. The buttons only request the
/// change (one-way flow: the owner updates the tree, the pin state flows back).
/// </summary>
public class PanelHeaderActions : StackPanel
{
    // 12×12 icons (drawn, not glyphs).
    private const string PinIconData = "M4,1 H8 V2 L7,3 V6 L9,8 V9 H6.5 V12 H5.5 V9 H3 V8 L5,6 V3 L4,2 Z";
    private const string CloseIconData = "M3,3 L9,9 M9,3 L3,9";

    private WorkspacePanel? _panel;

    public PanelHeaderActions()
    {
        Orientation = Orientation.Horizontal;
        Spacing = 0;
        VerticalAlignment = VerticalAlignment.Center;

        PinButton = new ToggleButton { Name = "PART_PinButton" };
        PinButton.Classes.Add("workspaceHeaderButton");
        PinButton.Content = Icon(PinButton, PinIconData, filled: true);
        Localize(PinButton, "Workspace.Pin.Tip");
        PinButton.Click += OnPinClick;

        CloseButton = new Button { Name = "PART_CloseButton" };
        CloseButton.Classes.Add("workspaceHeaderButton");
        CloseButton.Content = Icon(CloseButton, CloseIconData, filled: false);
        Localize(CloseButton, "Workspace.Close.Tip");
        CloseButton.Click += OnCloseClick;

        Children.Add(PinButton);
        Children.Add(CloseButton);
        IsVisible = false;
    }

    public ToggleButton PinButton { get; }

    public Button CloseButton { get; }

    /// <summary>The copy drawn by <see cref="WorkspacePanel"/>'s own template (not one placed in the content).</summary>
    internal bool InPanelTemplate { get; init; }

    /// <summary>The panel these buttons act on (nearest <see cref="WorkspacePanel"/> ancestor).</summary>
    public WorkspacePanel? Panel => _panel;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _panel = this.FindAncestorOfType<WorkspacePanel>();
        if (_panel is not null)
            _panel.PropertyChanged += OnPanelPropertyChanged;
        Sync();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        if (_panel is not null)
            _panel.PropertyChanged -= OnPanelPropertyChanged;
        _panel = null;
    }

    private void OnPanelPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == WorkspacePanel.IsPinnedProperty
            || e.Property == WorkspacePanel.CanCloseProperty
            || e.Property == WorkspacePanel.OwnsHeaderProperty
            || e.Property == WorkspacePanel.HeaderActionsInContentProperty)
        {
            Sync();
        }
    }

    private void Sync()
    {
        // A copy inside the content shows only when the panel delegates its actions there.
        IsVisible = _panel is not null && (InPanelTemplate
            ? !(_panel.OwnsHeader && _panel.HeaderActionsInContent)
            : _panel.HeaderActionsInContent);
        PinButton.IsChecked = _panel?.IsPinned == true;
        CloseButton.IsEnabled = _panel?.CanClose == true;
    }

    private void OnPinClick(object? sender, RoutedEventArgs e)
    {
        bool wanted = !(_panel?.IsPinned == true);
        _panel?.RequestPinned(wanted);
        Sync(); // the owner may reject: the button always shows the panel's state
        e.Handled = true;
    }

    private void OnCloseClick(object? sender, RoutedEventArgs e)
    {
        _panel?.RequestClose();
        e.Handled = true;
    }

    private static Control Icon(TemplatedControl owner, string data, bool filled)
    {
        var path = new Path
        {
            Data = Geometry.Parse(data),
            Width = 12,
            Height = 12,
            Stretch = Stretch.Uniform,
            StrokeThickness = filled ? 0 : 1.5,
            StrokeLineCap = PenLineCap.Round,
        };
        // Icon colour follows the button's Foreground (styled in ControlStyles: muted / pinned / hover).
        path.Bind(filled ? Shape.FillProperty : Shape.StrokeProperty, owner.GetObservable(TemplatedControl.ForegroundProperty));
        return path;
    }

    private static void Localize(Control control, string key)
    {
        var binding = new Binding($"[{key}]") { Source = EditorStrings.Instance, Mode = BindingMode.OneWay };
        control.Bind(ToolTip.TipProperty, binding);
        control.Bind(AutomationProperties.NameProperty, binding);
    }
}
