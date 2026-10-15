namespace RowlEngine.Editor.Tools.RowlEye.Serve;

/// <summary>
/// Options for configuring the Rowl.Eye local web inspector server.
/// </summary>
public sealed record RowlEyeServeOptions(
    int Port = 7070,
    string? InspectorHtmlPath = null,
    string? GoldensRoot = null,
    double DefaultScale = 1.0,
    bool AutoOpenBrowser = false
);
