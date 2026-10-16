using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Metadata;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Services.Workspace;

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
    public const string HeaderIconPartName = "PART_HeaderIcon";
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

    // Prototip başlık ikonları (24 birim kutuda çizili, 14 px gösterilir).
    private static readonly Dictionary<string, string> HeaderIconDataByPanelId = new(StringComparer.OrdinalIgnoreCase)
    {
        [PanelIds.Node] = "M4,4 h6 v6 h-6 Z M14,14 h6 v6 h-6 Z M10,7 h3 a2,2 0 0 1 2,2 v5",
        [PanelIds.Game] = "M3,5 h18 v12 h-18 Z M8,21 h8 M10,8.5 L15,11 L10,13.5 Z",
        [PanelIds.Edit] = "M4,4 h4 M4,4 v4 M20,4 h-4 M20,4 v4 M4,20 v-4 M4,20 h4 M11,11 L19,14 L15.5,15.5 L14,19 Z",
        [PanelIds.Hierarchy] = "M5,4 h5 v4 h-5 Z M8,8 v8 h5 M8,12 h5 M13,10 h6 v4 h-6 Z M13,14 h6 v4 h-6 Z",
        [PanelIds.Inspector] = "M4,7 h10 M18,7 h2 M4,17 h2 M10,17 h10 M14,5 v4 M6,15 v4",
        [PanelIds.Library] = "M4,4 h6 v6 h-6 Z M14,4 h6 v6 h-6 Z M4,14 h6 v6 h-6 Z M17,13.5 l1.1,2.3 l2.4,.3 l-1.8,1.7 l.5,2.4 l-2.2,-1.2 l-2.2,1.2 l.5,-2.4 l-1.8,-1.7 l2.4,-.3 Z",
        [PanelIds.Store] = "M3,8 a1,1 0 0 1 1,-1 h5 l2,2 h6 a1,1 0 0 1 1,1 v8 a1,1 0 0 1 -1,1 H4 a1,1 0 0 1 -1,-1 Z",
        [PanelIds.Lua] = "M8,7 L3,12 L8,17 M16,7 L21,12 L16,17 M14,4 L10,20",
        [PanelIds.Bottom] = "M3,7 a2,2 0 0 1 2,-2 h4 l2,2 h8 a2,2 0 0 1 2,2 v8 a2,2 0 0 1 -2,2 h-14 a2,2 0 0 1 -2,-2 Z",
    };

    /// <summary>Panel id → başlık ikon geometrisi (bilinmeyene null, ikon gizlenir).</summary>
    public static string? GetHeaderIconData(string? panelId)
    {
        if (panelId is not null && HeaderIconDataByPanelId.TryGetValue(panelId, out var data))
            return data;
        return null;
    }

    private Border? _header;
    private TextBlock? _title;
    private Path? _headerIcon;
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
        // Başlık ikonu: 14 px, ince çizgi, MutedText.
        var icon = new Path
        {
            Name = HeaderIconPartName,
            Width = HeaderIconSize,
            Height = HeaderIconSize,
            Stretch = Stretch.Uniform,
            StrokeThickness = 1.5,
            StrokeLineCap = PenLineCap.Round,
            StrokeJoin = PenLineJoin.Round,
            VerticalAlignment = VerticalAlignment.Center,
            HorizontalAlignment = HorizontalAlignment.Left,
            // Başlık dolgusuyla hizalı; başlık çocuğu metin kalır (testler Child'ı TextBlock bekler).
            Margin = new Thickness(HeaderPaddingLeft, 0, 0, 0),
            IsHitTestVisible = false,
            IsVisible = false,
        };
        icon.Bind(Shape.StrokeProperty, icon.GetResourceObservable("MutedText"));
        icon.RegisterInNameScope(scope);

        // D4: the title leaves room for the pin / close buttons drawn over the header's trailing end.
        var title = new TextBlock
        {
            Name = TitlePartName,
            TextTrimming = TextTrimming.CharacterEllipsis,
            Margin = new Thickness(HeaderIconSize + HeaderIconGap, 0, HeaderActionsReserve, 0),
            VerticalAlignment = VerticalAlignment.Center,
        };
        title.Classes.Add("panelTitle");
        title.RegisterInNameScope(scope);

        var header = new Border
        {
            Name = HeaderPartName,
            Child = title,
            Focusable = true,
            Height = HeaderHeight,
            MinHeight = HeaderHeight,
            Padding = new Thickness(HeaderPaddingLeft, 0, 6, 0),
            BorderThickness = new Thickness(0, 0, 0, 1),
            VerticalAlignment = VerticalAlignment.Top,
        };
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

        var pinnedOutline = new Border
        {
            IsHitTestVisible = false,
            CornerRadius = new CornerRadius(12),
            BorderThickness = new Thickness(1),
        };
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
        grid.Children.Add(icon);
        grid.Children.Add(presenter);
        grid.Children.Add(actions);
        grid.Children.Add(pinnedOutline);

        // Ayrı yuvarlak kart: kenar + kırpma burada, içerik zemini panelden gelir.
        var card = new Border
        {
            [~Border.BackgroundProperty] = panel[~BackgroundProperty],
            Child = grid,
            CornerRadius = new CornerRadius(12),
            BorderThickness = new Thickness(1),
            ClipToBounds = true,
            Margin = new Thickness(0),
        };
        card.Bind(Border.BorderBrushProperty, card.GetResourceObservable("BorderColor"));
        return card;
    }

    protected override void OnApplyTemplate(TemplateAppliedEventArgs e)
    {
        base.OnApplyTemplate(e);
        _header = e.NameScope.Find<Border>(HeaderPartName);
        _title = e.NameScope.Find<TextBlock>(TitlePartName);
        _headerIcon = e.NameScope.Find<Path>(HeaderIconPartName);
        _actions = e.NameScope.Find<PanelHeaderActions>(HeaderActionsPartName);
        UpdateHeader();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == TitleProperty || change.Property == OwnsHeaderProperty || change.Property == IsPinnedProperty || change.Property == PanelIdProperty)
            UpdateHeader();
        if (change.Property == IsPinnedProperty)
            PseudoClasses.Set(PinnedPseudoClass, IsPinned);
    }

    /// <summary>Width kept free at the header's trailing end for <see cref="PanelHeaderActions"/>.</summary>
    private const double HeaderActionsReserve = 90;

    private const double HeaderPaddingLeft = 13;
    private const double HeaderIconSize = 14;
    private const double HeaderIconGap = 8;

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
        if (_headerIcon is not null)
        {
            var data = GetHeaderIconData(PanelId);
            _headerIcon.Data = data is not null ? Geometry.Parse(data) : null;
            _headerIcon.IsVisible = data is not null && !OwnsHeader;
        }
        if (_actions is not null)
        {
            // Own header (e.g. tab row): float over the content's first 40 px row.
            Grid.SetRowSpan(_actions, OwnsHeader ? 2 : 1);
            _actions.VerticalAlignment = OwnsHeader ? VerticalAlignment.Top : VerticalAlignment.Center;
            _actions.Height = OwnsHeader ? HeaderHeight : double.NaN;
        }
    }
}
