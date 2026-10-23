using System.Linq;
using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 10 C1 — Node / Game / Edit bağımsız aç-kapa (saf).</summary>
public sealed class WorkspaceCenterViewsTests
{
    // Mevcut test başındaki kalıp: varsayılan preset ağacı.
    private static EditorWorkspaceState DefaultState() =>
        EditorWorkspaceLayouts.Preset(EditorWorkspaceLayouts.DefaultPreset)!;

    // Ağaçta eksen + A/B eşleşen bölmeyi bulur.
    private static SplitNode? FindSplit(LayoutNode? node, SplitAxis axis, string first, string second)
    {
        if (node is SplitNode split)
        {
            if (split.Axis == axis && split.A is LeafNode a && split.B is LeafNode b
                && a.View == first && b.View == second)
                return split;
            return FindSplit(split.A, axis, first, second) ?? FindSplit(split.B, axis, first, second);
        }
        return null;
    }

    [Fact]
    public void EnginePreview_AddsGameRightOfNode()
    {
        EditorWorkspaceState state = DefaultState();
        Assert.False(state.IsVisible(PanelIds.Game));

        EditorWorkspaceState next = state.HandlePanelAction("EnginePreview");

        Assert.True(next.IsVisible(PanelIds.Node));
        Assert.True(next.IsVisible(PanelIds.Game));
        SplitNode? split = FindSplit(next.Layout.Tree, SplitAxis.Horizontal, PanelIds.Node, PanelIds.Game);
        Assert.NotNull(split);
        Assert.Equal(0.5, split!.Ratio);
    }

    [Fact]
    public void Preview_AddsEditUnderGame_AllActive()
    {
        EditorWorkspaceState state = DefaultState().HandlePanelAction("EnginePreview");

        EditorWorkspaceState next = state.HandlePanelAction("Preview");

        Assert.True(next.IsVisible(PanelIds.Node));
        Assert.True(next.IsVisible(PanelIds.Game));
        Assert.True(next.IsVisible(PanelIds.Edit));
        SplitNode? split = FindSplit(next.Layout.Tree, SplitAxis.Vertical, PanelIds.Game, PanelIds.Edit);
        Assert.NotNull(split);
        Assert.Equal(0.5, split!.Ratio);
        Assert.True(next.IsNodeGraphActive);
        Assert.True(next.IsEnginePreviewActive);
        Assert.True(next.IsPreviewActive);
    }

    [Fact]
    public void EnginePreview_RemovesGame_KeepsNodeAndEdit()
    {
        EditorWorkspaceState state = DefaultState()
            .HandlePanelAction("EnginePreview")
            .HandlePanelAction("Preview");

        EditorWorkspaceState next = state.HandlePanelAction("EnginePreview");

        Assert.False(next.IsVisible(PanelIds.Game));
        Assert.True(next.IsVisible(PanelIds.Node));
        Assert.True(next.IsVisible(PanelIds.Edit));
    }

    [Fact]
    public void NodeGraph_OnSingleNode_NoChange()
    {
        EditorWorkspaceState state = DefaultState();
        Assert.True(state.IsVisible(PanelIds.Node));
        Assert.False(state.IsVisible(PanelIds.Game));
        Assert.False(state.IsVisible(PanelIds.Edit));

        EditorWorkspaceState next = state.HandlePanelAction("NodeGraph");

        Assert.Same(state, next);
        Assert.True(next.IsVisible(PanelIds.Node));
    }

    [Fact]
    public void SidePanels_StayThroughSteps()
    {
        EditorWorkspaceState s0 = DefaultState();
        EditorWorkspaceState s1 = s0.HandlePanelAction("EnginePreview");
        EditorWorkspaceState s2 = s1.HandlePanelAction("Preview");
        EditorWorkspaceState s3 = s2.HandlePanelAction("EnginePreview");

        foreach (EditorWorkspaceState s in new[] { s0, s1, s2, s3 })
        {
            Assert.True(s.IsVisible(PanelIds.Hierarchy));
            Assert.True(s.IsVisible(PanelIds.Inspector));
            Assert.True(s.IsVisible(PanelIds.Bottom));
        }
    }

    [Fact]
    public void EnsureCenterVisible_WhenOpen_SameInstance()
    {
        EditorWorkspaceState state = DefaultState().HandlePanelAction("EnginePreview");

        Assert.Same(state, state.EnsureCenterVisible(PanelIds.Game));
    }

    [Fact]
    public void EnsureCenterVisible_WhenClosed_OpensWithoutClosingOthers()
    {
        EditorWorkspaceState state = DefaultState().HandlePanelAction("EnginePreview");
        Assert.False(state.IsVisible(PanelIds.Edit));

        EditorWorkspaceState next = state.EnsureCenterVisible(PanelIds.Edit);

        Assert.True(next.IsVisible(PanelIds.Node));
        Assert.True(next.IsVisible(PanelIds.Game));
        Assert.True(next.IsVisible(PanelIds.Edit));
    }
}
