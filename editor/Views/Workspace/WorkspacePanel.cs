using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Layout;

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
/// </summary>
public class WorkspacePanel : ContentControl
{
    public const string HeaderPartName = "PART_Header";
    public const string TitlePartName = "PART_Title";

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

    /// <summary>The host-drawn header (hidden when <see cref="OwnsHeader"/>); <c>null</c> before the template is applied.</summary>
    public Border? HostHeader => _header;

    private static Control BuildTemplate(WorkspacePanel panel, INameScope scope)
    {
        var title = new TextBlock { Name = TitlePartName };
        title.Classes.Add("panelTitle");
        title.RegisterInNameScope(scope);

        var header = new Border { Name = HeaderPartName, Child = title };
        header.Classes.Add("panelHeader");
        header.RegisterInNameScope(scope);

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
        UpdateHeader();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == TitleProperty || change.Property == OwnsHeaderProperty)
            UpdateHeader();
    }

    private void UpdateHeader()
    {
        if (_header is not null)
            _header.IsVisible = !OwnsHeader;
        if (_title is not null)
            _title.Text = Title;
    }
}
