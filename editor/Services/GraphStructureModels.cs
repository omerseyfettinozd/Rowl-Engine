using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Graph vNext (format v5) structure contract.
/// <list type="bullet">
/// <item><see cref="CanvasGroup"/> is editor-canvas-only metadata (title, color,
/// rect, members). It carries no runtime semantics.</item>
/// <item><see cref="SubgraphDefinition"/> is a modular sub-flow contract: flow
/// may enter the member set only through the entry node and leave it only
/// through an exit node.</item>
/// <item><see cref="ChapterDefinition"/> is a runtime section marker: save/load
/// boundaries, backlog clustering and profile progress resolve chapters
/// through the chapter id assigned to each node.</item>
/// </list>
/// Format v4 documents predate this contract: they carry no groups, subgraphs
/// or chapters and no per-node chapter ids. Such documents load as a single
/// implicit chapter with every node unassigned.
/// </summary>
public sealed record CanvasGroup(
    string Id,
    string Title,
    string Color,
    double X,
    double Y,
    double Width,
    double Height,
    IReadOnlyList<ulong> NodeIds);

public sealed record SubgraphDefinition(
    string Id,
    string Title,
    ulong EntryNodeId,
    IReadOnlyList<ulong> ExitNodeIds,
    IReadOnlyList<ulong> NodeIds);

public sealed record ChapterDefinition(
    string Id,
    string Title,
    int Order,
    string Summary,
    ulong? StartNodeId);

/// <summary>Parsed v5 structure section of one story graph document.</summary>
public sealed class GraphStructureDocument
{
    public List<CanvasGroup> Groups { get; } = new();
    public List<SubgraphDefinition> Subgraphs { get; } = new();
    public List<ChapterDefinition> Chapters { get; } = new();

    public bool IsEmpty => Groups.Count == 0 && Subgraphs.Count == 0 && Chapters.Count == 0;
}

/// <summary>Limits shared by the hydrator and the validator (mirrors the native caps).</summary>
internal static class GraphStructureLimits
{
    public const int MaxGroups = 4096;
    public const int MaxSubgraphs = 1024;
    public const int MaxChapters = 1024;
    public const int MaxMembersPerEntry = 10000;
    public const int MaxPortsPerSubgraph = 1024;
    public const int MaxIdChars = 128;
    public const int MaxTitleChars = 512;
    public const int MaxSummaryChars = 4096;

    /// <summary>Format version written when a document carries vNext structure.</summary>
    public const int CurrentVersion = 5;

    /// <summary>Last format version without structure sections.</summary>
    public const int LegacyVersion = 4;
}
