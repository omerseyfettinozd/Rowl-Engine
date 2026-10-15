using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye.Goldens;
using RowlEngine.Editor.Tools.RowlEye.Reporting;
using RowlEngine.Editor.Tools.RowlEye.Serve;
using RowlEngine.Editor.Tools.RowlEye.Targets;

namespace RowlEngine.Editor.Tools.RowlEye;

/// <summary>
/// Headless snapshot orchestrator, visual comparator, regression auditor and inspector server for Rowl.Eye.
/// </summary>
public static class RowlEyeRunner
{
    public static int Run(string[] args)
    {
        try
        {
            var config = RowlEyeConfig.Parse(args);

            // A. Diff Mode
            if (config.IsDiffMode)
            {
                Console.WriteLine($"[Rowl.Eye] Starting visual comparison: '{config.ReferencePath}' vs '{config.ActualPath}'...");
                var result = RowlEyeDiffer.Compare(
                    config.ReferencePath,
                    config.ActualPath,
                    config.DiffOutputPath,
                    config.Tolerance,
                    config.FailThresholdPercent
                );

                Console.WriteLine($"[Rowl.Eye] {result.Message}");
                if (!string.IsNullOrEmpty(result.DiffImagePath))
                {
                    Console.WriteLine($"[Rowl.Eye] Heatmap diff saved to: '{result.DiffImagePath}'");
                }

                return result.Passed ? 0 : 1;
            }

            // B. Initialize Headless Avalonia Runtime
            RowlEyeHeadlessRuntime.EnsureInitialized();

            // C. Web Inspector Serve Mode (--serve)
            if (config.IsServeMode)
            {
                var serveOpts = new RowlEyeServeOptions(
                    Port: config.Port,
                    GoldensRoot: config.GoldensDirectory,
                    DefaultScale: config.Scales.Length > 0 ? config.Scales[0] : 1.0
                );

                using var server = new RowlEyeWebServer(serveOpts);
                server.StartAsync().GetAwaiter().GetResult();

                Console.WriteLine($"[Rowl.Eye] Web Inspector running at {server.BaseUrl}");
                Console.WriteLine($"[Rowl.Eye] Press Ctrl+C or Enter to terminate server...");

                using var cts = new System.Threading.CancellationTokenSource();
                Console.CancelKeyPress += (_, e) =>
                {
                    e.Cancel = true;
                    cts.Cancel();
                };

                try
                {
                    while (!cts.IsCancellationRequested)
                    {
                        System.Threading.Thread.Sleep(500);
                    }
                }
                catch (System.Threading.ThreadInterruptedException) { }

                Console.WriteLine("[Rowl.Eye] Web Inspector shut down cleanly.");
                return 0;
            }

            // D. Golden Master Update Mode (--update-goldens)
            if (config.IsUpdateGoldens)
            {
                Console.WriteLine($"[Rowl.Eye] Updating golden masters for Target: '{config.Target}', Theme: '{config.Theme}'...");
                var goldenManager = new RowlEyeGoldenManager(config.GoldensDirectory);
                var targetsToUpdate = config.Target == RowlEyeTarget.All
                    ? [
                        RowlEyeTarget.MainWindow,
                        RowlEyeTarget.SettingsDialog,
                        RowlEyeTarget.NodeGraph,
                        RowlEyeTarget.Hierarchy,
                        RowlEyeTarget.Inspector,
                        RowlEyeTarget.AssetBrowser,
                        RowlEyeTarget.LivePreview
                      ]
                    : new[] { config.Target };

                var themesToUpdate = string.Equals(config.Theme, "All", StringComparison.OrdinalIgnoreCase)
                    ? new[] { "Dark", "Light" }
                    : new[] { config.Theme };

                int updatedCount = 0;
                string tempDir = Path.Combine(Path.GetTempPath(), $"rowl_golden_upd_{Guid.NewGuid():N}");
                Directory.CreateDirectory(tempDir);

                try
                {
                    foreach (var target in targetsToUpdate)
                    {
                        foreach (var theme in themesToUpdate)
                        {
                            Dispatcher.UIThread.Invoke(() =>
                            {
                                var (window, viewModel, _) = RowlEyeFixtureFactory.Create(target, theme);
                                window.Show();
                                Dispatcher.UIThread.RunJobs();

                                string baseName = $"{target}_{theme}";
                                string vmFile = Path.Combine(tempDir, $"{baseName}.viewmodel.json");
                                if (viewModel != null)
                                {
                                    RowlEyeSealer.SealViewModel(viewModel, vmFile);
                                }

                                foreach (double scale in config.Scales)
                                {
                                    string scaleStr = scale.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
                                    string tempFile = Path.Combine(tempDir, $"{baseName}@{scaleStr}x.png");
                                    RowlEyeRenderer.RenderWindow(window, scale, tempFile);

                                    goldenManager.UpdateGolden(target, theme, scale, tempFile, null, File.Exists(vmFile) ? vmFile : null);
                                    string destPath = goldenManager.GetGoldenPath(target, theme, scale);
                                    Console.WriteLine($"[Rowl.Eye] Golden master updated: {destPath}");
                                    updatedCount++;
                                }

                                window.Close();
                                Dispatcher.UIThread.RunJobs();
                            });
                        }
                    }
                }
                finally
                {
                    try { Directory.Delete(tempDir, true); } catch { }
                }

                Console.WriteLine($"[Rowl.Eye] Successfully updated {updatedCount} golden master reference(s).");
                return 0;
            }

            // E. Audit Mode (--audit)
            if (config.IsAuditMode)
            {
                Console.WriteLine($"[Rowl.Eye] Running visual regression audit against golden masters...");
                var goldenManager = new RowlEyeGoldenManager(config.GoldensDirectory);
                string outDir = Path.GetFullPath(config.OutputDirectory);
                string diffsDir = Path.Combine(outDir, "diffs");
                Directory.CreateDirectory(outDir);
                Directory.CreateDirectory(diffsDir);

                var targetsToAudit = config.Target == RowlEyeTarget.All
                    ? [
                        RowlEyeTarget.MainWindow,
                        RowlEyeTarget.SettingsDialog,
                        RowlEyeTarget.NodeGraph,
                        RowlEyeTarget.Hierarchy,
                        RowlEyeTarget.Inspector,
                        RowlEyeTarget.AssetBrowser,
                        RowlEyeTarget.LivePreview
                      ]
                    : new[] { config.Target };

                var themesToAudit = string.Equals(config.Theme, "All", StringComparison.OrdinalIgnoreCase)
                    ? new[] { "Dark", "Light" }
                    : new[] { config.Theme };

                // 1. Render current actual snapshots on UI Thread
                foreach (var target in targetsToAudit)
                {
                    foreach (var theme in themesToAudit)
                    {
                        Dispatcher.UIThread.Invoke(() =>
                        {
                            var (window, viewModel, _) = RowlEyeFixtureFactory.Create(target, theme);
                            window.Show();
                            Dispatcher.UIThread.RunJobs();

                            string baseName = $"{target}_{theme}";
                            foreach (double scale in config.Scales)
                            {
                                string scaleStr = scale.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
                                string actualPath = Path.Combine(outDir, $"{baseName}@{scaleStr}x.png");
                                RowlEyeRenderer.RenderWindow(window, scale, actualPath);
                            }

                            if (viewModel != null)
                            {
                                string vmFile = Path.Combine(outDir, $"{baseName}.viewmodel.json");
                                RowlEyeSealer.SealViewModel(viewModel, vmFile);
                            }

                            window.Close();
                            Dispatcher.UIThread.RunJobs();
                        });
                    }
                }

                // 2. Perform Audit comparison
                var auditReport = RowlEyeAuditor.Audit(
                    goldenManager,
                    outDir,
                    diffsDir,
                    targetsToAudit,
                    themesToAudit,
                    config.Scales,
                    config.Strict
                );

                foreach (var r in auditReport.Results)
                {
                    string statusTag = r.Status switch
                    {
                        "PASSED" => "[PASS]",
                        "FAILED" => "[FAIL]",
                        _ => "[WARN]"
                    };
                    Console.WriteLine($"[Rowl.Eye] {statusTag} {r.Target} ({r.Theme}, {r.Scale}x): {r.Message} (Diff: {r.MismatchPercentage:F3}%)");
                }

                // 3. Generate HTML report if requested
                if (config.IsReportMode)
                {
                    string reportPath = Path.Combine(outDir, "audit-report.html");
                    var reportModel = new RowlEyeReportModel
                    {
                        Title = "Rowl.Eye Visual Regression Suite",
                        TimestampUtc = auditReport.TimestampUtc,
                        GitCommitSha = ResolveGitCommitSha(),
                        AvaloniaVersion = typeof(Avalonia.Application).Assembly.GetName().Version?.ToString() ?? "11.3.11",
                        TotalCaptures = auditReport.TotalTargets,
                        PassedCount = auditReport.PassedTargets,
                        FailedCount = auditReport.FailedTargets,
                        Items = auditReport.Results.Select(r => new RowlEyeReportItem
                        {
                            TargetName = r.Target.ToString(),
                            Theme = r.Theme,
                            Scale = r.Scale,
                            Status = r.Status,
                            MismatchPercentage = r.MismatchPercentage,
                            MismatchedPixels = r.MismatchedPixels,
                            TotalPixels = r.TotalPixels,
                            GoldenImagePath = r.GoldenPath,
                            ActualImagePath = r.ActualPath,
                            DiffImagePath = r.DiffPath,
                            Message = r.Message
                        }).ToList()
                    };

                    RowlEyeReportGenerator.Generate(reportModel, reportPath);
                    Console.WriteLine($"[Rowl.Eye] Interactive HTML audit report saved to: '{reportPath}'");
                }

                Console.WriteLine(auditReport.AllPassed
                    ? $"[Rowl.Eye] AUDIT PASSED: All {auditReport.PassedTargets} visual targets matched golden baselines."
                    : $"[Rowl.Eye] AUDIT FAILED: {auditReport.FailedTargets} target(s) failed, {auditReport.MissingGoldens} missing baseline(s).");

                return auditReport.AllPassed ? 0 : 1;
            }

            // F. Standalone Report Mode (--report)
            if (config.IsReportMode)
            {
                string outDir = Path.GetFullPath(config.OutputDirectory);
                string reportPath = Path.Combine(outDir, "report.html");
                var model = new RowlEyeReportModel
                {
                    Title = "Rowl.Eye Snapshot Report",
                    TimestampUtc = DateTime.UtcNow.ToString("O"),
                    GitCommitSha = ResolveGitCommitSha()
                };
                RowlEyeReportGenerator.Generate(model, reportPath);
                Console.WriteLine($"[Rowl.Eye] Standalone report saved to: '{reportPath}'");
                return 0;
            }

            // G. Standard Snapshot Mode
            Console.WriteLine($"[Rowl.Eye] Capturing headless snapshots for target '{config.Target}'...");
            string snapshotOutDir = Path.GetFullPath(config.OutputDirectory);
            Directory.CreateDirectory(snapshotOutDir);

            var targetsToRender = config.Target == RowlEyeTarget.All
                ? [
                    RowlEyeTarget.MainWindow,
                    RowlEyeTarget.SettingsDialog,
                    RowlEyeTarget.NodeGraph,
                    RowlEyeTarget.Hierarchy,
                    RowlEyeTarget.Inspector,
                    RowlEyeTarget.AssetBrowser,
                    RowlEyeTarget.LivePreview
                  ]
                : new[] { config.Target };

            var themesToRender = string.Equals(config.Theme, "All", StringComparison.OrdinalIgnoreCase)
                ? new[] { "Dark", "Light" }
                : new[] { config.Theme };

            int totalRendered = 0;

            foreach (var target in targetsToRender)
            {
                foreach (var theme in themesToRender)
                {
                    totalRendered += RenderTarget(target, theme, config, snapshotOutDir);
                }
            }

            Console.WriteLine($"[Rowl.Eye] SUCCESS: Finished snapshot runs ({totalRendered} captures saved to '{snapshotOutDir}').");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[Rowl.Eye] ERROR: {ex.Message}");
            Console.Error.WriteLine(ex.StackTrace);
            return 1;
        }
    }

    private static int RenderTarget(RowlEyeTarget target, string theme, RowlEyeConfig config, string outDir)
    {
        return Dispatcher.UIThread.Invoke(() =>
        {
            var (window, viewModel, _) = RowlEyeFixtureFactory.Create(target, theme);
            if (config.Width > 0 && target == RowlEyeTarget.MainWindow) window.Width = config.Width;
            if (config.Height > 0 && target == RowlEyeTarget.MainWindow) window.Height = config.Height;

            window.Show();
            Dispatcher.UIThread.RunJobs();

            var generatedFiles = new List<string>();
            string baseName = $"{target}_{theme}";

            foreach (double scale in config.Scales)
            {
                string scaleStr = scale.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
                string fileName = $"{baseName}@{scaleStr}x.png";
                string filePath = Path.Combine(outDir, fileName);
                RowlEyeRenderer.RenderWindow(window, scale, filePath);
                generatedFiles.Add(fileName);
                Console.WriteLine($"[Rowl.Eye] Rendered: {fileName} (Scale {scale}x, {Math.Round(window.Width * scale)}x{Math.Round(window.Height * scale)} px)");
            }

            // Seal ViewModel state
            if (viewModel != null)
            {
                string vmFileName = $"{baseName}.viewmodel.json";
                string vmFilePath = Path.Combine(outDir, vmFileName);
                RowlEyeSealer.SealViewModel(viewModel, vmFilePath);
                generatedFiles.Add(vmFileName);
                Console.WriteLine($"[Rowl.Eye] Sealed ViewModel state: {vmFileName}");
            }

            // Seal Manifest
            var manifest = new RowlEyeManifest
            {
                TargetWindow = baseName,
                TimestampUtc = DateTime.UtcNow.ToString("O"),
                GitCommitSha = ResolveGitCommitSha(),
                AvaloniaVersion = typeof(Avalonia.Application).Assembly.GetName().Version?.ToString() ?? "11.3.11",
                LogicalWidth = (int)window.Width,
                LogicalHeight = (int)window.Height,
                Scales = config.Scales,
                GeneratedFiles = generatedFiles.ToArray()
            };
            string manifestPath = Path.Combine(outDir, $"{baseName}.manifest.json");
            RowlEyeSealer.SealManifest(manifest, manifestPath);

            // Backward compatibility aliases for legacy single-target contracts
            foreach (double scale in config.Scales)
            {
                string scaleStr = scale.ToString("0.##", System.Globalization.CultureInfo.InvariantCulture);
                string legacyPng = Path.Combine(outDir, $"{target}@{scaleStr}x.png");
                string generatedPng = Path.Combine(outDir, $"{baseName}@{scaleStr}x.png");
                if (!File.Exists(legacyPng) && File.Exists(generatedPng))
                {
                    File.Copy(generatedPng, legacyPng, overwrite: true);
                }
            }

            string legacyVm = Path.Combine(outDir, $"{target}.viewmodel.json");
            string generatedVm = Path.Combine(outDir, $"{baseName}.viewmodel.json");
            if (!File.Exists(legacyVm) && File.Exists(generatedVm))
            {
                File.Copy(generatedVm, legacyVm, overwrite: true);
            }

            string legacyManifest = Path.Combine(outDir, "manifest.json");
            if (!File.Exists(legacyManifest))
            {
                var legacyObj = new RowlEyeManifest
                {
                    TargetWindow = target.ToString(),
                    TimestampUtc = manifest.TimestampUtc,
                    GitCommitSha = manifest.GitCommitSha,
                    AvaloniaVersion = manifest.AvaloniaVersion,
                    LogicalWidth = manifest.LogicalWidth,
                    LogicalHeight = manifest.LogicalHeight,
                    Scales = manifest.Scales,
                    GeneratedFiles = generatedFiles.ToArray()
                };
                RowlEyeSealer.SealManifest(legacyObj, legacyManifest);
            }

            window.Close();
            Dispatcher.UIThread.RunJobs();
            return generatedFiles.Count;
        });
    }

    private static string ResolveGitCommitSha()
    {
        try
        {
            var psi = new ProcessStartInfo("git", "rev-parse HEAD")
            {
                RedirectStandardOutput = true,
                UseShellExecute = false
            };
            using var proc = Process.Start(psi);
            if (proc != null)
            {
                string output = proc.StandardOutput.ReadToEnd().Trim();
                proc.WaitForExit();
                if (!string.IsNullOrEmpty(output)) return output;
            }
        }
        catch
        {
            // Fallback for non-git environments
        }
        return "unknown";
    }
}
