using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Metadata;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Data;
using Avalonia.Input;
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
[PseudoClasses(":revealed", ":pinned")]
public class PanelHeaderActions : StackPanel
{
    // 16 px çizili ikonlar (dolgusuz, Foreground ile boyanır).
    private const string LayoutIconData = "M4,4 h16 v16 h-16 Z M12,4 v16 M12,12 h8";
    private const string PinIconData = "M9,4 h6 M10,4 v5 l-3,4 h10 l-3,-4 v-5 M12,13 v7";
    private const string CloseIconData = "M6,6 L18,18 M18,6 L6,18";

    private WorkspacePanel? _panel;

    public PanelHeaderActions()
    {
        Orientation = Orientation.Horizontal;
        Spacing = 0;
        VerticalAlignment = VerticalAlignment.Center;
        Classes.Add("panelHeaderActions");

        LayoutButton = new Button { Name = "PART_LayoutButton" };
        LayoutButton.Classes.Add("workspaceHeaderButton");
        LayoutButton.Content = Icon(LayoutButton, LayoutIconData);
        Localize(LayoutButton, "Workspace.Layout.Tip");
        LayoutButton.Click += OnLayoutClick;

        PinButton = new ToggleButton { Name = "PART_PinButton" };
        PinButton.Classes.Add("workspaceHeaderButton");
        PinButton.Content = Icon(PinButton, PinIconData);
        Localize(PinButton, "Workspace.Pin.Tip");
        PinButton.Click += OnPinClick;

        CloseButton = new Button { Name = "PART_CloseButton" };
        CloseButton.Classes.Add("workspaceHeaderButton");
        CloseButton.Content = Icon(CloseButton, CloseIconData);
        Localize(CloseButton, "Workspace.Close.Tip");
        CloseButton.Click += OnCloseClick;

        Children.Add(LayoutButton);
        Children.Add(PinButton);
        Children.Add(CloseButton);
        IsVisible = false;
    }

    public Button LayoutButton { get; }

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
        {
            _panel.PropertyChanged += OnPanelPropertyChanged;
            _panel.PointerEntered += OnPanelPointerEntered;
            _panel.PointerExited += OnPanelPointerExited;
        }
        Sync();
        UpdateReveal();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        if (_panel is not null)
        {
            _panel.PropertyChanged -= OnPanelPropertyChanged;
            _panel.PointerEntered -= OnPanelPointerEntered;
            _panel.PointerExited -= OnPanelPointerExited;
        }
        _panel = null;
        UpdateReveal();
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

        if (e.Property == InputElement.IsKeyboardFocusWithinProperty
            || e.Property == WorkspacePanel.IsPinnedProperty)
        {
            UpdateReveal();
        }
    }

    private void OnPanelPointerEntered(object? sender, PointerEventArgs e) => UpdateReveal();

    private void OnPanelPointerExited(object? sender, PointerEventArgs e) => UpdateReveal();

    private void UpdateReveal()
    {
        // Sessiz başlık: işaretçi üstteyken ya da klavye odağı paneldeyken göster.
        bool revealed = _panel is not null && (_panel.IsPointerOver || _panel.IsKeyboardFocusWithin);
        PseudoClasses.Set(":revealed", revealed);
        PseudoClasses.Set(":pinned", _panel?.IsPinned == true);
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

    private void OnLayoutClick(object? sender, RoutedEventArgs e)
    {
        if (_panel?.PanelId is { } id)
            _panel.Host?.OpenPanelMenu(id, LayoutButton);
        e.Handled = true;
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

    private static Control Icon(TemplatedControl owner, string data)
    {
        var path = new Path
        {
            Data = Geometry.Parse(data),
            Width = 14,
            Height = 14,
            Stretch = Stretch.Uniform,
            StrokeThickness = 1.5,
            StrokeLineCap = PenLineCap.Round,
            StrokeJoin = PenLineJoin.Round,
        };
        // Icon colour follows the button's Foreground (styled in ControlStyles: muted / pinned / hover).
        path.Bind(Shape.StrokeProperty, owner.GetObservable(TemplatedControl.ForegroundProperty));
        return path;
    }

    private static void Localize(Control control, string key)
    {
        var binding = new Binding($"[{key}]") { Source = EditorStrings.Instance, Mode = BindingMode.OneWay };
        control.Bind(ToolTip.TipProperty, binding);
        control.Bind(AutomationProperties.NameProperty, binding);
    }
}
