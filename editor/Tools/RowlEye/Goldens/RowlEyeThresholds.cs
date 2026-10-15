using System;
using System.Collections.Generic;
using RowlEngine.Editor.Tools.RowlEye.Targets;

namespace RowlEngine.Editor.Tools.RowlEye.Goldens;

/// <summary>
/// Per-target thresholds and masking rules for visual regression auditing.
/// </summary>
public sealed record RowlEyeThreshold(
    double MaxMismatchPercent,
    byte Tolerance,
    IReadOnlyList<RowlEyeIgnoreRegion>? IgnoreRegions = null)
{
    public IReadOnlyList<RowlEyeIgnoreRegion> IgnoreRegions { get; init; } = IgnoreRegions ?? [];

    public static RowlEyeThreshold Default => new(0.5, 8);
    public static RowlEyeThreshold Strict => new(0.0, 0);
    public static RowlEyeThreshold NodeGraph => new(1.0, 8);

    public static RowlEyeThreshold ForTarget(RowlEyeTarget target, bool strict = false)
    {
        if (strict) return Strict;
        return target switch
        {
            RowlEyeTarget.NodeGraph => NodeGraph,
            _ => Default
        };
    }
}
