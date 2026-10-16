using RowlEngine.Editor.Services.Workspace;
using Xunit;

namespace RowlEngine.Editor.Tests;

// Odak sırası: ileri/geri/sarmal/boş/bilinmeyen.
public sealed class WorkspaceFocusOrderTests
{
    private static readonly string[] Panels = new[] { "a", "b", "c" };

    [Fact]
    public void Next_Forward() =>
        Assert.Equal("b", WorkspaceFocusOrder.Next(Panels, "a", false));

    [Fact]
    public void Next_Backward() =>
        Assert.Equal("a", WorkspaceFocusOrder.Next(Panels, "b", true));

    [Fact]
    public void Wraps_Around()
    {
        Assert.Equal("a", WorkspaceFocusOrder.Next(Panels, "c", false));
        Assert.Equal("c", WorkspaceFocusOrder.Next(Panels, "a", true));
    }

    [Fact]
    public void Empty_ReturnsNull() =>
        Assert.Null(WorkspaceFocusOrder.Next(System.Array.Empty<string>(), "a", false));

    [Fact]
    public void Unknown_Current_GoesToEdge()
    {
        Assert.Equal("a", WorkspaceFocusOrder.Next(Panels, "zzz", false));
        Assert.Equal("c", WorkspaceFocusOrder.Next(Panels, "zzz", true));
        Assert.Equal("a", WorkspaceFocusOrder.Next(Panels, null, false));
        Assert.Equal("c", WorkspaceFocusOrder.Next(Panels, null, true));
    }
}
