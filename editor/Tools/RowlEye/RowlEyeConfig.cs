using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using RowlEngine.Editor.Tools.RowlEye.Targets;

namespace RowlEngine.Editor.Tools.RowlEye;

/// <summary>
/// Configuration for the Rowl.Eye headless snapshot runner, differ, auditor, and server.
/// </summary>
public sealed class RowlEyeConfig
{
    public bool IsDiffMode { get; init; }
    public string ReferencePath { get; init; } = string.Empty;
    public string ActualPath { get; init; } = string.Empty;
    public string DiffOutputPath { get; init; } = string.Empty;
    public byte Tolerance { get; init; } = 8;
    public double FailThresholdPercent { get; init; } = 0.5;

    public bool IsAuditMode { get; init; }
    public bool IsUpdateGoldens { get; init; }
    public bool Strict { get; init; }
    public bool IsReportMode { get; init; }
    public bool IsServeMode { get; init; }
    public int Port { get; init; } = 7070;

    public RowlEyeTarget Target { get; init; } = RowlEyeTarget.SettingsDialog;
    public string Theme { get; init; } = "Dark";
    public string GoldensDirectory { get; init; } = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "__goldens__");

    public string WindowName { get; init; } = "SettingsDialog";
    public double[] Scales { get; init; } = [1.0, 1.25, 2.0];
    public string OutputDirectory { get; init; } = Path.Combine(Path.GetTempPath(), "rowl-eye");
    public int Width { get; init; } = 720;
    public int Height { get; init; } = 560;

    public static RowlEyeConfig Parse(string[] args)
    {
        bool isDiff = false;
        string refPath = string.Empty;
        string actPath = string.Empty;
        string diffOut = string.Empty;
        byte tolerance = 8;
        double threshold = 0.5;

        bool isAudit = false;
        bool isUpdateGoldens = false;
        bool strict = false;
        bool isReport = false;
        bool isServe = false;
        int port = 7070;

        RowlEyeTarget target = RowlEyeTarget.SettingsDialog;
        bool targetExplicit = false;
        string theme = "Dark";
        string goldensDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "__goldens__");

        string windowName = "SettingsDialog";
        List<double> scales = [1.0, 1.25, 2.0];
        string outDir = Path.Combine(Path.GetTempPath(), "rowl-eye");
        int width = 720;
        int height = 560;
        bool explicitSize = false;

        for (int i = 0; i < args.Length; i++)
        {
            if (string.Equals(args[i], "--diff", StringComparison.OrdinalIgnoreCase))
            {
                isDiff = true;
            }
            else if (string.Equals(args[i], "--ref", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                refPath = args[++i];
            }
            else if (string.Equals(args[i], "--actual", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                actPath = args[++i];
            }
            else if (string.Equals(args[i], "--tolerance", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                if (byte.TryParse(args[++i], out byte t)) tolerance = t;
            }
            else if (string.Equals(args[i], "--threshold", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                if (double.TryParse(args[++i], CultureInfo.InvariantCulture, out double th)) threshold = th;
            }
            else if (string.Equals(args[i], "--audit", StringComparison.OrdinalIgnoreCase))
            {
                isAudit = true;
            }
            else if (string.Equals(args[i], "--update-goldens", StringComparison.OrdinalIgnoreCase))
            {
                isUpdateGoldens = true;
            }
            else if (string.Equals(args[i], "--strict", StringComparison.OrdinalIgnoreCase))
            {
                strict = true;
            }
            else if (string.Equals(args[i], "--report", StringComparison.OrdinalIgnoreCase))
            {
                isReport = true;
            }
            else if (string.Equals(args[i], "--serve", StringComparison.OrdinalIgnoreCase))
            {
                isServe = true;
            }
            else if (string.Equals(args[i], "--port", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                if (int.TryParse(args[++i], out int p)) port = p;
            }
            else if (string.Equals(args[i], "--goldens", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                goldensDir = args[++i];
            }
            else if (string.Equals(args[i], "--target", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                string rawTarget = args[++i];
                if (Enum.TryParse<RowlEyeTarget>(rawTarget, true, out var parsedTarget))
                {
                    target = parsedTarget;
                    windowName = parsedTarget.ToString();
                    targetExplicit = true;
                }
            }
            else if (string.Equals(args[i], "--theme", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                theme = args[++i];
            }
            else if (string.Equals(args[i], "--window", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                windowName = args[++i];
                if (!targetExplicit && Enum.TryParse<RowlEyeTarget>(windowName, true, out var parsedTarget))
                {
                    target = parsedTarget;
                }
            }
            else if (string.Equals(args[i], "--scales", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                var parts = args[++i].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
                scales.Clear();
                foreach (var part in parts)
                {
                    if (double.TryParse(part, CultureInfo.InvariantCulture, out double val))
                        scales.Add(val);
                }
                if (scales.Count == 0) scales.AddRange([1.0, 1.25, 2.0]);
            }
            else if (string.Equals(args[i], "--scale", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                string sc = args[++i];
                if (double.TryParse(sc, CultureInfo.InvariantCulture, out double sVal))
                {
                    scales = [sVal];
                }
            }
            else if (string.Equals(args[i], "--out", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                outDir = args[++i];
                diffOut = outDir;
            }
            else if (string.Equals(args[i], "--output", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                outDir = args[++i];
                diffOut = outDir;
            }
            else if (string.Equals(args[i], "--width", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                if (int.TryParse(args[++i], out int w)) { width = w; explicitSize = true; }
            }
            else if (string.Equals(args[i], "--height", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                if (int.TryParse(args[++i], out int h)) { height = h; explicitSize = true; }
            }
        }

        if (!explicitSize && (windowName.Equals("MainWindow", StringComparison.OrdinalIgnoreCase) || target == RowlEyeTarget.MainWindow))
        {
            width = 1920;
            height = 1080;
        }

        return new RowlEyeConfig
        {
            IsDiffMode = isDiff,
            ReferencePath = refPath,
            ActualPath = actPath,
            DiffOutputPath = diffOut,
            Tolerance = tolerance,
            FailThresholdPercent = threshold,
            IsAuditMode = isAudit,
            IsUpdateGoldens = isUpdateGoldens,
            Strict = strict,
            IsReportMode = isReport,
            IsServeMode = isServe,
            Port = port,
            Target = target,
            Theme = theme,
            GoldensDirectory = goldensDir,
            WindowName = windowName,
            Scales = scales.ToArray(),
            OutputDirectory = outDir,
            Width = width,
            Height = height
        };
    }
}
