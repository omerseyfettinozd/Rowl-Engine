using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 7 — Düğüm kütüphanesi merkez alanın sağında, Lua editörü merkez alanın altında açılır
/// (prototip library-categories.jpg / desktop.jpg); menü eylemi aç/kapa yapar, başlıktaki ×
/// kapatır, yeniden açılış son payı hatırlar.
/// </summary>
public sealed class EditorWorkspaceToolPanelsTests
{
    private static SplitNode ParentOf(LayoutNode? node, string view)
    {
        if (node is not SplitNode split)
            throw new Exception($"{view} not found");
        if (split.A is LeafNode { View: var a } && a == view || split.B is LeafNode { View: var b } && b == view)
            return split;
        try { return ParentOf(split.A, view); }
        catch (Exception) { return ParentOf(split.B, view); }
    }

    [Fact]
    public void Library_OpensRightOfTheCenterArea_AndToggles()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default.HandlePanelAction("Library");

        Assert.True(state.IsVisible(PanelIds.Library));
        SplitNode parent = ParentOf(state.Layout.Tree, PanelIds.Library);
        Assert.Equal(SplitAxis.Horizontal, parent.Axis);
        Assert.Equal(new LeafNode(PanelIds.Library), parent.B);
        Assert.Contains(PanelIds.Node, new WorkspaceLayout(parent.A, 0).Views);
        Assert.Equal(EditorWorkspaceLayouts.LibrarySplitRatio, parent.Ratio, 3);

        Assert.False(state.HandlePanelAction("Library").IsVisible(PanelIds.Library));
    }

    [Fact]
    public void Lua_OpensUnderTheCenterArea_AboveTheBottomStrip()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default.HandlePanelAction("Lua");

        SplitNode parent = ParentOf(state.Layout.Tree, PanelIds.Lua);
        Assert.Equal(SplitAxis.Vertical, parent.Axis);
        Assert.Equal(new LeafNode(PanelIds.Lua), parent.B);
        Assert.Equal(new LeafNode(PanelIds.Node), parent.A);
        // Alt şerit merkez + Lua bloğunun altında kalır.
        Assert.True(state.IsBottomVisible);
        SplitNode bottomParent = ParentOf(state.Layout.Tree, PanelIds.Bottom);
        Assert.Contains(PanelIds.Lua, new WorkspaceLayout(bottomParent.A, 0).Views);
    }

    [Fact]
    public void ClosePanel_RemovesToolPanel_AndReopenRemembersShare()
    {
        EditorWorkspaceState open = EditorWorkspaceState.Default.HandlePanelAction("Lua");
        string splitId = ParentOf(open.Layout.Tree, PanelIds.Lua).Id;
        EditorWorkspaceState resized = open.WithSplitRatio(splitId, 0.7);

        EditorWorkspaceState closed = resized.ClosePanel(PanelIds.Lua);
        Assert.False(closed.IsVisible(PanelIds.Lua));

        EditorWorkspaceState reopened = closed.HandlePanelAction("Lua");
        Assert.Equal(0.7, ParentOf(reopened.Layout.Tree, PanelIds.Lua).Ratio, 3);
    }
}
