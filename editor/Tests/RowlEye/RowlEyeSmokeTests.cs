using System;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Tools.RowlEye.Goldens;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using RowlEngine.Editor.Views.Dialogs;
using SkiaSharp;
using Xunit;

namespace RowlEngine.Editor.Tests.RowlEye;

public class RowlEyeSmokeTests
{
    [Fact]
    public void RowlEyeConfig_ParsesArguments_Correctly()
    {
        string[] args = ["--rowl-eye", "--window", "SettingsDialog", "--scales", "1,1.5,2", "--width", "800", "--height", "600", "--out", "/tmp/test-rowl-eye"];
        var config = RowlEyeConfig.Parse(args);

        Assert.Equal("SettingsDialog", config.WindowName);
        Assert.Equal(3, config.Scales.Length);
        Assert.Equal(1.0, config.Scales[0]);
        Assert.Equal(1.5, config.Scales[1]);
        Assert.Equal(2.0, config.Scales[2]);
        Assert.Equal(800, config.Width);
        Assert.Equal(600, config.Height);
        Assert.Equal("/tmp/test-rowl-eye", config.OutputDirectory);
    }

    [Fact]
    public void RowlEyeRunner_Produces_SealedArtifacts()
    {
        string tempOut = Path.Combine(Path.GetTempPath(), $"RowlEyeTest_{Guid.NewGuid():N}");
        try
        {
            string[] args = ["--rowl-eye", "--window", "SettingsDialog", "--scales", "1,2", "--out", tempOut];
            int exitCode = RowlEyeRunner.Run(args);

            Assert.Equal(0, exitCode);

            string png1x = Path.Combine(tempOut, "SettingsDialog@1x.png");
            string png2x = Path.Combine(tempOut, "SettingsDialog@2x.png");
            string vmJson = Path.Combine(tempOut, "SettingsDialog.viewmodel.json");
            string manifest = Path.Combine(tempOut, "manifest.json");

            // 1. Files exist and are non-empty
            Assert.True(File.Exists(png1x), "1x PNG must exist");
            Assert.True(File.Exists(png2x), "2x PNG must exist");
            Assert.True(File.Exists(vmJson), "ViewModel JSON must exist");
            Assert.True(File.Exists(manifest), "Manifest JSON must exist");

            Assert.True(new FileInfo(png1x).Length > 5000, "1x PNG should be > 5KB");
            Assert.True(new FileInfo(png2x).Length > 10000, "2x PNG should be > 10KB");

            // 2. ViewModel JSON content validation
            string vmText = File.ReadAllText(vmJson);
            using var doc = JsonDocument.Parse(vmText);
            var root = doc.RootElement;
            Assert.True(root.TryGetProperty("SelectedTheme", out _), "ViewModel must contain SelectedTheme");
            Assert.True(root.TryGetProperty("AutoSaveIntervalSeconds", out _), "ViewModel must contain AutoSaveIntervalSeconds");

            // 3. Manifest validation
            string manifestText = File.ReadAllText(manifest);
            using var mDoc = JsonDocument.Parse(manifestText);
            var mRoot = mDoc.RootElement;
            Assert.Equal("SettingsDialog", mRoot.GetProperty("TargetWindow").GetString());
            Assert.Equal(720, mRoot.GetProperty("LogicalWidth").GetInt32());
            Assert.Equal(560, mRoot.GetProperty("LogicalHeight").GetInt32());
        }
        finally
        {
            if (Directory.Exists(tempOut))
            {
                try { Directory.Delete(tempOut, true); } catch { }
            }
        }
    }

    [Fact]
    public void RowlEyeDiffer_IdenticalImages_ReturnsZeroMismatch()
    {
        string tempDir = Path.Combine(Path.GetTempPath(), $"RowlEyeDiff_{Guid.NewGuid():N}");
        Directory.CreateDirectory(tempDir);
        try
        {
            string imgPath = Path.Combine(tempDir, "base.png");
            string diffPath = Path.Combine(tempDir, "diff.png");

            using (var bmp = new SKBitmap(50, 50, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                bmp.Erase(SKColors.Blue);
                using var image = SKImage.FromBitmap(bmp);
                using var data = image.Encode(SKEncodedImageFormat.Png, 100);
                using var fs = File.Create(imgPath);
                data.SaveTo(fs);
            }

            var result = RowlEyeDiffer.Compare(imgPath, imgPath, diffPath, tolerance: 8, failThresholdPercent: 0.1);

            Assert.True(result.Passed);
            Assert.Equal(0.0, result.MismatchPercentage);
            Assert.Equal(0, result.MismatchedPixels);
            Assert.True(File.Exists(diffPath), "Diff heatmap image should be generated");
        }
        finally
        {
            if (Directory.Exists(tempDir))
            {
                try { Directory.Delete(tempDir, true); } catch { }
            }
        }
    }

    [Fact]
    public void RowlEyeDiffer_SyntheticDifference_DetectsMismatchAndSavesHeatmap()
    {
        string tempDir = Path.Combine(Path.GetTempPath(), $"RowlEyeDiff_{Guid.NewGuid():N}");
        Directory.CreateDirectory(tempDir);
        try
        {
            string refPath = Path.Combine(tempDir, "ref.png");
            string actPath = Path.Combine(tempDir, "act.png");
            string diffPath = Path.Combine(tempDir, "diff.png");

            // 100x100 canvas = 10,000 pixels
            using (var refBmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            using (var actBmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                refBmp.Erase(SKColors.DarkGray);
                actBmp.Erase(SKColors.DarkGray);

                // Modify 10x10 square = 100 pixels
                for (int y = 10; y < 20; y++)
                {
                    for (int x = 10; x < 20; x++)
                    {
                        actBmp.SetPixel(x, y, SKColors.Yellow);
                    }
                }

                using (var image1 = SKImage.FromBitmap(refBmp))
                using (var data1 = image1.Encode(SKEncodedImageFormat.Png, 100))
                using (var fs1 = File.Create(refPath))
                {
                    data1.SaveTo(fs1);
                }

                using (var image2 = SKImage.FromBitmap(actBmp))
                using (var data2 = image2.Encode(SKEncodedImageFormat.Png, 100))
                using (var fs2 = File.Create(actPath))
                {
                    data2.SaveTo(fs2);
                }
            }

            // Threshold 0.5% will fail because 100 / 10,000 is exactly 1.0%
            var result = RowlEyeDiffer.Compare(refPath, actPath, diffPath, tolerance: 8, failThresholdPercent: 0.5);

            Assert.False(result.Passed, "1.0% mismatch must fail a 0.5% threshold");
            Assert.Equal(1.0, result.MismatchPercentage, 2);
            Assert.Equal(100, result.MismatchedPixels);
            Assert.Equal(10000, result.TotalPixels);
            Assert.True(File.Exists(diffPath), "Diff heatmap must exist");
            Assert.True(new FileInfo(diffPath).Length > 0, "Diff heatmap must not be empty");
        }
        finally
        {
            if (Directory.Exists(tempDir))
            {
                try { Directory.Delete(tempDir, true); } catch { }
            }
        }
    }

    [Fact]
    public void RowlEyeDiffer_Masking_IgnoreRegion_Bypasses_Mismatch()
    {
        string tempDir = Path.Combine(Path.GetTempPath(), $"RowlEyeMask_{Guid.NewGuid():N}");
        Directory.CreateDirectory(tempDir);
        try
        {
            string refPath = Path.Combine(tempDir, "ref.png");
            string actPath = Path.Combine(tempDir, "act.png");
            string diffPath = Path.Combine(tempDir, "diff.png");

            using (var refBmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            using (var actBmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                refBmp.Erase(SKColors.DarkGray);
                actBmp.Erase(SKColors.DarkGray);

                // Modify 10x10 square at (10, 10) = 100 mismatched pixels
                for (int y = 10; y < 20; y++)
                {
                    for (int x = 10; x < 20; x++)
                    {
                        actBmp.SetPixel(x, y, SKColors.Yellow);
                    }
                }

                using (var image1 = SKImage.FromBitmap(refBmp))
                using (var data1 = image1.Encode(SKEncodedImageFormat.Png, 100))
                using (var fs1 = File.Create(refPath)) { data1.SaveTo(fs1); }

                using (var image2 = SKImage.FromBitmap(actBmp))
                using (var data2 = image2.Encode(SKEncodedImageFormat.Png, 100))
                using (var fs2 = File.Create(actPath)) { data2.SaveTo(fs2); }
            }

            // Define ignore region covering the exact modified square
            var masks = new[] { new RowlEyeIgnoreRegion(10, 10, 10, 10, "Ignore intentional test region") };

            var result = RowlEyeDiffer.Compare(
                refPath,
                actPath,
                diffPath,
                tolerance: 8,
                failThresholdPercent: 0.0,
                ignoreRegions: masks
            );

            Assert.True(result.Passed, "Ignored region must not cause failure");
            Assert.Equal(0, result.MismatchedPixels);
            Assert.Equal(0.0, result.MismatchPercentage, 2);
            Assert.True(File.Exists(diffPath), "Diff file with mask rendering should exist");
        }
        finally
        {
            if (Directory.Exists(tempDir))
            {
                try { Directory.Delete(tempDir, true); } catch { }
            }
        }
    }

    [Fact]
    public void RowlEyeFixtureFactory_StateVariants_Produces_Expected_States()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();

        Avalonia.Threading.Dispatcher.UIThread.Invoke(() =>
        {
            // Empty variant
            var (emptyWin, emptyVm, _) = RowlEyeFixtureFactory.Create(RowlEyeTarget.NodeGraph, "Dark", "Empty");
            var mainVmEmpty = Assert.IsType<RowlEngine.Editor.ViewModels.MainWindowViewModel>(emptyVm);
            Assert.Empty(mainVmEmpty.Nodes);
            emptyWin.Close();

            // Default variant
            var (defaultWin, defaultVm, _) = RowlEyeFixtureFactory.Create(RowlEyeTarget.NodeGraph, "Dark", "Default");
            var mainVmDefault = Assert.IsType<RowlEngine.Editor.ViewModels.MainWindowViewModel>(defaultVm);
            Assert.NotEmpty(mainVmDefault.Nodes);
            defaultWin.Close();
        });
    }
}
