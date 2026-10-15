using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Tools.RowlEye.Targets;

namespace RowlEngine.Editor.Tools.RowlEye.Goldens;

/// <summary>
/// Result of an audit comparison between a target snapshot and its golden baseline.
/// </summary>
public sealed record RowlEyeAuditResult
{
    public RowlEyeTarget Target { get; init; }
    public string Theme { get; init; } = "Dark";
    public double Scale { get; init; } = 1.0;
    public bool Passed { get; init; }
    public double MismatchPercentage { get; init; }
    public long MismatchedPixels { get; init; }
    public long TotalPixels { get; init; }
    public string Status { get; init; } = "UNKNOWN";
    public string GoldenPath { get; init; } = string.Empty;
    public string ActualPath { get; init; } = string.Empty;
    public string DiffPath { get; init; } = string.Empty;
    public string Message { get; init; } = string.Empty;
}

/// <summary>
/// Audit report summarizing all target comparisons.
/// </summary>
public sealed class RowlEyeAuditReport
{
    public string TimestampUtc { get; set; } = DateTime.UtcNow.ToString("O");
    public int TotalTargets { get; set; }
    public int PassedTargets { get; set; }
    public int FailedTargets { get; set; }
    public int MissingGoldens { get; set; }
    public bool AllPassed => FailedTargets == 0 && MissingGoldens == 0;
    public List<RowlEyeAuditResult> Results { get; set; } = [];
}

/// <summary>
/// Automated visual regression auditor checking snapshots against golden baselines.
/// </summary>
public static class RowlEyeAuditor
{
    private static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };

    public static RowlEyeAuditReport Audit(
        RowlEyeGoldenManager goldenManager,
        string currentSnapshotsDir,
        string diffOutputDir,
        IEnumerable<RowlEyeTarget> targets,
        IEnumerable<string> themes,
        IEnumerable<double> scales,
        bool strict = false)
    {
        ArgumentNullException.ThrowIfNull(goldenManager);
        ArgumentException.ThrowIfNullOrWhiteSpace(currentSnapshotsDir);
        ArgumentException.ThrowIfNullOrWhiteSpace(diffOutputDir);

        Directory.CreateDirectory(diffOutputDir);
        var report = new RowlEyeAuditReport();

        foreach (var target in targets)
        {
            var threshold = RowlEyeThreshold.ForTarget(target, strict);

            foreach (var theme in themes)
            {
                foreach (var scale in scales)
                {
                    report.TotalTargets++;
                    string scaleStr = scale.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
                    string fileName = $"{target}_{theme}@{scaleStr}x.png";
                    string actualPath = Path.Combine(currentSnapshotsDir, fileName);
                    string goldenPath = goldenManager.GetGoldenPath(target, theme, scale);
                    string diffPath = Path.Combine(diffOutputDir, $"{target}_{theme}@{scaleStr}x_diff.png");

                    if (!File.Exists(actualPath))
                    {
                        report.FailedTargets++;
                        report.Results.Add(new RowlEyeAuditResult
                        {
                            Target = target,
                            Theme = theme,
                            Scale = scale,
                            Passed = false,
                            Status = "MISSING_ACTUAL",
                            GoldenPath = goldenPath,
                            ActualPath = actualPath,
                            Message = $"Actual snapshot not found: '{actualPath}'"
                        });
                        continue;
                    }

                    if (!goldenManager.HasGolden(target, theme, scale))
                    {
                        report.MissingGoldens++;
                        report.Results.Add(new RowlEyeAuditResult
                        {
                            Target = target,
                            Theme = theme,
                            Scale = scale,
                            Passed = false,
                            Status = "MISSING_GOLDEN",
                            GoldenPath = goldenPath,
                            ActualPath = actualPath,
                            Message = $"Golden baseline missing. Run with --update-goldens to establish baseline."
                        });
                        continue;
                    }

                    var diffResult = RowlEyeDiffer.Compare(
                        goldenPath,
                        actualPath,
                        diffPath,
                        threshold.Tolerance,
                        threshold.MaxMismatchPercent,
                        threshold.IgnoreRegions
                    );

                    if (diffResult.Passed)
                    {
                        report.PassedTargets++;
                    }
                    else
                    {
                        report.FailedTargets++;
                    }

                    report.Results.Add(new RowlEyeAuditResult
                    {
                        Target = target,
                        Theme = theme,
                        Scale = scale,
                        Passed = diffResult.Passed,
                        MismatchPercentage = diffResult.MismatchPercentage,
                        MismatchedPixels = diffResult.MismatchedPixels,
                        TotalPixels = diffResult.TotalPixels,
                        Status = diffResult.Passed ? "PASSED" : "FAILED",
                        GoldenPath = goldenPath,
                        ActualPath = actualPath,
                        DiffPath = diffPath,
                        Message = diffResult.Message
                    });
                }
            }
        }

        string auditJsonPath = Path.Combine(diffOutputDir, "audit.json");
        File.WriteAllText(auditJsonPath, JsonSerializer.Serialize(report, JsonOpts));
        return report;
    }
}
