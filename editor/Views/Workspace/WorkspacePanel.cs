using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Metadata;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Views.Workspace;

/// <summary>
/// Faz 5 D3 — one leaf of the workspace: a panel header (<c>Border.panelHeader</c>
/// + <c>TextBlock.panelTitle</c>) above the panel's content. Declared once per
/// panel id inside a <see cref="WorkspaceHost"/>; the host only positions and
/// shows / hides it, so the content control is never re-created or re-parented.
/// <para>
/// Header rule (one header per leaf): when the content already draws its own
/// panel header — Hierarchy and Inspector (<c>Border.panelHeader</c> with their
/// actions) and the bottom strip (its tab row is the header) — the panel sets
/// <see cref="OwnsHeader"/> and the host header is not drawn; every other panel
/// gets the host header with <see cref="Title"/>.
/// </para>
/// <para>
/// Faz 5 D4: the header is the drag handle (the host runs the gesture), is
/// keyboard-focusable (Enter / Space / Shift+F10 / Menu key open the panel
/// menu) and carries <see cref="PanelHeaderActions"/> (pin, close).
/// <see cref="IsPinned"/> (<c>:pinned</c>) and <see cref="CanClose"/> are set by
/// the host from the owner's state; the panel only forwards requests.
/// </para>
/// </summary>
[PseudoClasses(PinnedPseudoClass)]
public class WorkspacePanel : ContentControl
{
    public const string HeaderPartName = "PART_Header";
    public const string TitlePartName = "PART_Title";
    public const string HeaderActionsPartName = "PART_HeaderActions";
    public const string PinnedPseudoClass = ":pinned";

    /// <summary>Header height (K2 panel header, matches <c>Border.panelHeader</c> MinHeight).</summary>
    public const double HeaderHeight = 40;

    /// <summary>Faz 5 D4: locked against moves (set by the host from the owner's pinned set).</summary>
    public static readonly StyledProperty<bool> IsPinnedProperty =
        AvaloniaProperty.Register<WorkspacePanel, bool>(nameof(IsPinned));

    /// <summary>Faz 5 D4: the close button is enabled (false for the last panel; set by the host).</summary>
    public static readonly StyledProperty<bool> CanCloseProperty =
        AvaloniaProperty.Register<WorkspacePanel, bool>(nameof(CanClose));

    /// <summary>
    /// Faz 5 D4: the content places its own <see cref="PanelHeaderActions"/> inside
    /// the header it draws (Hierarchy, Inspector); only meaningful with <see cref="OwnsHeader"/>.
    /// </summary>
    public static readonly StyledProperty<bool> HeaderActionsInContentProperty =
        AvaloniaProperty.Register<WorkspacePanel, bool>(nameof(HeaderActionsInContent));

    /// <summary>Stable panel id (<c>PanelIds</c>); matches the leaf in the layout tree.</summary>
    public static readonly StyledProperty<string?> PanelIdProperty =
        AvaloniaProperty.Register<WorkspacePanel, string?>(nameof(PanelId));

    /// <summary>Header text (bind with <c>{l:T …}</c> so it follows live language changes).</summary>
    public static readonly StyledProperty<string?> TitleProperty =
        AvaloniaProperty.Register<WorkspacePanel, string?>(nameof(Title));

    /// <summary>The content draws its own header; the host header is hidden.</summary>
    public static readonly StyledProperty<bool> OwnsHeaderProperty =
        AvaloniaProperty.Register<WorkspacePanel, bool>(nameof(OwnsHeader));

    /// <summary>Smallest width the workspace gives this panel (px, K2 density).</summary>
    public static readonly StyledProperty<double> PanelMinWidthProperty =
        AvaloniaProperty.Register<WorkspacePanel, double>(nameof(PanelMinWidth), DefaultMinWidth);

    /// <summary>Smallest height the workspace gives this panel (px).</summary>
    public static readonly StyledProperty<double> PanelMinHeightProperty =
        AvaloniaProperty.Register<WorkspacePanel, double>(nameof(PanelMinHeight), DefaultMinHeight);

    public const double DefaultMinWidth = 160;
    public const double DefaultMinHeight = 120;

    private Border? _header;
    private TextBlock? _title;
    private PanelHeaderActions? _actions;

    static WorkspacePanel()
    {
        ClipToBoundsProperty.OverrideDefaultValue<WorkspacePanel>(true);
    }

    public WorkspacePanel()
    {
        Template = new FuncControlTemplate<WorkspacePanel>(BuildTemplate);
    }

    public string? PanelId
    {
        get => GetValue(PanelIdProperty);
        set => SetValue(PanelIdProperty, value);
    }

    public string? Title
    {
        get => GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public bool OwnsHeader
    {
        get => GetValue(OwnsHeaderProperty);
        set => SetValue(OwnsHeaderProperty, value);
    }

    public double PanelMinWidth
    {
        get => GetValue(PanelMinWidthProperty);
        set => SetValue(PanelMinWidthProperty, value);
    }

    public double PanelMinHeight
    {
        get => GetValue(PanelMinHeightProperty);
        set => SetValue(PanelMinHeightProperty, value);
    }

    public bool IsPinned
    {
        get => GetValue(IsPinnedProperty);
        set => SetValue(IsPinnedProperty, value);
    }

    public bool CanClose
    {
        get => GetValue(CanCloseProperty);
        set => SetValue(CanCloseProperty, value);
    }

    public bool HeaderActionsInContent
    {
        get => GetValue(HeaderActionsInContentProperty);
        set => SetValue(HeaderActionsInContentProperty, value);
    }

    /// <summary>The host-drawn header (hidden when <see cref="OwnsHeader"/>); <c>null</c> before the template is applied.</summary>
    public Border? HostHeader => _header;

    /// <summary>Pin / close buttons drawn by this panel's template (hidden when the content carries its own).</summary>
    public PanelHeaderActions? TemplateHeaderActions => _actions;

    /// <summary>The host this panel belongs to (set by <see cref="WorkspaceHost"/>).</summary>
    internal WorkspaceHost? Host { get; set; }

    /// <summary>Asks the owner to pin / unpin this panel (no state change here).</summary>
    public void RequestPinned(bool pinned)
    {
        if (PanelId is { } id)
            Host?.RequestPinned(id, pinned);
    }

    /// <summary>Asks the owner to close this panel (ignored when <see cref="CanClose"/> is false).</summary>
    public void RequestClose()
    {
        if (CanClose && PanelId is { } id)
            Host?.RequestClose(id);
    }

    private static Control BuildTemplate(WorkspacePanel panel, INameScope scope)
    {
        // D4: the title leaves room for the pin / close buttons drawn over the header's trailing end.
        var title = new TextBlock
        {
            Name = TitlePartName,
            TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis,
            Margin = new Thickness(0, 0, HeaderActionsReserve, 0),
        };
        title.Classes.Add("panelTitle");
        title.RegisterInNameScope(scope);

        var header = new Border { Name = HeaderPartName, Child = title, Focusable = true };
        header.Classes.Add("panelHeader");
        header.Classes.Add("workspaceDragHandle");
        header.Bind(AutomationProperties.NameProperty, panel.GetObservable(TitleProperty));
        header.Bind(AutomationProperties.HelpTextProperty,
            new Binding("[Workspace.Header.Help]") { Source = EditorStrings.Instance, Mode = BindingMode.OneWay });
        header.RegisterInNameScope(scope);

        // Trailing end of the header row; for a panel that owns its header it floats
        // over the top of the content (e.g. the bottom strip's tab row) instead.
        var actions = new PanelHeaderActions
        {
            Name = HeaderActionsPartName,
            InPanelTemplate = true,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 0, 6, 0),
        };
        actions.RegisterInNameScope(scope);

        var pinnedOutline = new Border { IsHitTestVisible = false };
        pinnedOutline.Classes.Add("workspacePinnedOutline");
        pinnedOutline.Bind(IsVisibleProperty, panel.GetObservable(IsPinnedProperty));
        Grid.SetRowSpan(pinnedOutline, 2);

        var presenter = new ContentPresenter
        {
            Name = "PART_ContentPresenter",
            [~ContentPresenter.ContentProperty] = panel[~ContentProperty],
            [~ContentPresenter.ContentTemplateProperty] = panel[~ContentTemplateProperty],
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            VerticalContentAlignment = VerticalAlignment.Stretch,
        };
        presenter.RegisterInNameScope(scope);
        Grid.SetRow(presenter, 1);

        var grid = new Grid { RowDefinitions = new RowDefinitions("Auto,*") };
        grid.Children.Add(header);
        grid.Children.Add(presenter);
        grid.Children.Add(actions);
        grid.Children.Add(pinnedOutline);

        return new Border
        {
            [~Border.BackgroundProperty] = panel[~BackgroundProperty],
            Child = grid,
        };
    }

    protected override void OnApplyTemplate(TemplateAppliedEventArgs e)
    {
        base.OnApplyTemplate(e);
        _header = e.NameScope.Find<Border>(HeaderPartName);
        _title = e.NameScope.Find<TextBlock>(TitlePartName);
        _actions = e.NameScope.Find<PanelHeaderActions>(HeaderActionsPartName);
        UpdateHeader();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == TitleProperty || change.Property == OwnsHeaderProperty || change.Property == IsPinnedProperty)
            UpdateHeader();
        if (change.Property == IsPinnedProperty)
            PseudoClasses.Set(PinnedPseudoClass, IsPinned);
    }

    /// <summary>Width kept free at the header's trailing end for <see cref="PanelHeaderActions"/>.</summary>
    private const double HeaderActionsReserve = 64;

    private void UpdateHeader()
    {
        if (_header is not null)
        {
            _header.IsVisible = !OwnsHeader;
            // Movable header: move cursor; a pinned header is not a drag handle.
            _header.Cursor = IsPinned ? Cursor.Default : new Cursor(StandardCursorType.SizeAll);
        }
        if (_title is not null)
            _title.Text = Title;
        if (_actions is not null)
        {
            // Own header (e.g. tab row): float over the content's first 40 px row.
            Grid.SetRowSpan(_actions, OwnsHeader ? 2 : 1);
            _actions.VerticalAlignment = OwnsHeader ? VerticalAlignment.Top : VerticalAlignment.Center;
            _actions.Height = OwnsHeader ? HeaderHeight : double.NaN;
        }
    }
}
