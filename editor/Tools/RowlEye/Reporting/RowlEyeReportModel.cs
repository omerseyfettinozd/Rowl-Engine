using System;
using System.Collections.Generic;
using RowlEngine.Editor.Tools.RowlEye.Goldens;

namespace RowlEngine.Editor.Tools.RowlEye.Reporting;

/// <summary>
/// Model representing the data required to render the interactive HTML visual audit report.
/// </summary>
public sealed class RowlEyeReportModel
{
    public string Title { get; set; } = "Rowl.Eye Visual Inspection Report";
    public string TimestampUtc { get; set; } = DateTime.UtcNow.ToString("O");
    public string GitCommitSha { get; set; } = "unknown";
    public string AvaloniaVersion { get; set; } = "11.3.11";
    public int TotalCaptures { get; set; }
    public int PassedCount { get; set; }
    public int FailedCount { get; set; }
    public List<RowlEyeReportItem> Items { get; set; } = [];
}

public sealed class RowlEyeReportItem
{
    public string TargetName { get; set; } = string.Empty;
    public string Theme { get; set; } = "Dark";
    public double Scale { get; set; } = 1.0;
    public string Status { get; set; } = "PASSED";
    public double MismatchPercentage { get; set; }
    public long MismatchedPixels { get; set; }
    public long TotalPixels { get; set; }
    public string GoldenImagePath { get; set; } = string.Empty;
    public string ActualImagePath { get; set; } = string.Empty;
    public string DiffImagePath { get; set; } = string.Empty;
    public string ViewModelJson { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
}
