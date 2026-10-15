using System;
using System.IO;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Tools.RowlEye.Goldens;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using SkiaSharp;
using Xunit;

namespace RowlEngine.Editor.Tests.RowlEye;

public class RowlEyeGoldenTests
{
    [Fact]
    public void Update_Then_Audit_Passes()
    {
        string tempRoot = Path.Combine(Path.GetTempPath(), $"RowlEyeGolden_{Guid.NewGuid():N}");
        string goldenDir = Path.Combine(tempRoot, "goldens");
        string currentDir = Path.Combine(tempRoot, "current");
        string diffDir = Path.Combine(tempRoot, "diffs");
        Directory.CreateDirectory(currentDir);

        try
        {
            var manager = new RowlEyeGoldenManager(goldenDir);

            // 1. Create a dummy snapshot
            string testPng = Path.Combine(currentDir, "Hierarchy_Dark@1x.png");
            using (var bmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                bmp.Erase(SKColors.DarkSlateGray);
                using var image = SKImage.FromBitmap(bmp);
                using var data = image.Encode(SKEncodedImageFormat.Png, 100);
                using var fs = File.Create(testPng);
                data.SaveTo(fs);
            }

            // 2. Establish golden baseline
            manager.UpdateGolden(RowlEyeTarget.Hierarchy, "Dark", 1.0, testPng);
            Assert.True(manager.HasGolden(RowlEyeTarget.Hierarchy, "Dark", 1.0));

            // 3. Run audit against identical capture
            var auditReport = RowlEyeAuditor.Audit(
                manager,
                currentDir,
                diffDir,
                [RowlEyeTarget.Hierarchy],
                ["Dark"],
                [1.0]
            );

            Assert.True(auditReport.AllPassed);
            Assert.Equal(1, auditReport.PassedTargets);
            Assert.Equal(0, auditReport.FailedTargets);
            Assert.Equal("PASSED", auditReport.Results[0].Status);
            Assert.Equal(0.0, auditReport.Results[0].MismatchPercentage);
        }
        finally
        {
            if (Directory.Exists(tempRoot))
            {
                try { Directory.Delete(tempRoot, true); } catch { }
            }
        }
    }

    [Fact]
    public void Broken_Image_Fails_Audit()
    {
        string tempRoot = Path.Combine(Path.GetTempPath(), $"RowlEyeGolden_{Guid.NewGuid():N}");
        string goldenDir = Path.Combine(tempRoot, "goldens");
        string currentDir = Path.Combine(tempRoot, "current");
        string diffDir = Path.Combine(tempRoot, "diffs");
        Directory.CreateDirectory(currentDir);

        try
        {
            var manager = new RowlEyeGoldenManager(goldenDir);

            // 1. Create baseline
            string baselinePng = Path.Combine(currentDir, "Inspector_Dark@1x.png");
            using (var bmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                bmp.Erase(SKColors.Black);
                using var image = SKImage.FromBitmap(bmp);
                using var data = image.Encode(SKEncodedImageFormat.Png, 100);
                using var fs = File.Create(baselinePng);
                data.SaveTo(fs);
            }
            manager.UpdateGolden(RowlEyeTarget.Inspector, "Dark", 1.0, baselinePng);

            // 2. Corrupt/change actual image by 10% (1000 pixels out of 10000)
            using (var bmp = new SKBitmap(100, 100, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                bmp.Erase(SKColors.Black);
                for (int y = 0; y < 10; y++)
                    for (int x = 0; x < 100; x++)
                        bmp.SetPixel(x, y, SKColors.Crimson);

                using var image = SKImage.FromBitmap(bmp);
                using var data = image.Encode(SKEncodedImageFormat.Png, 100);
                using var fs = File.Create(baselinePng);
                data.SaveTo(fs);
            }

            // 3. Run audit - should fail threshold
            var auditReport = RowlEyeAuditor.Audit(
                manager,
                currentDir,
                diffDir,
                [RowlEyeTarget.Inspector],
                ["Dark"],
                [1.0]
            );

            Assert.False(auditReport.AllPassed);
            Assert.Equal(1, auditReport.FailedTargets);
            Assert.Equal("FAILED", auditReport.Results[0].Status);
            Assert.Equal(10.0, auditReport.Results[0].MismatchPercentage, 1);
            Assert.True(File.Exists(auditReport.Results[0].DiffPath), "Diff heatmap must be generated on failure");
        }
        finally
        {
            if (Directory.Exists(tempRoot))
            {
                try { Directory.Delete(tempRoot, true); } catch { }
            }
        }
    }

    [Fact]
    public void Missing_Golden_Returns_NeedsBaseline()
    {
        string tempRoot = Path.Combine(Path.GetTempPath(), $"RowlEyeGolden_{Guid.NewGuid():N}");
        string goldenDir = Path.Combine(tempRoot, "goldens");
        string currentDir = Path.Combine(tempRoot, "current");
        string diffDir = Path.Combine(tempRoot, "diffs");
        Directory.CreateDirectory(currentDir);

        try
        {
            var manager = new RowlEyeGoldenManager(goldenDir);

            string actualPng = Path.Combine(currentDir, "NodeGraph_Dark@1x.png");
            using (var bmp = new SKBitmap(50, 50, SKColorType.Rgba8888, SKAlphaType.Premul))
            {
                bmp.Erase(SKColors.Gray);
                using var image = SKImage.FromBitmap(bmp);
                using var data = image.Encode(SKEncodedImageFormat.Png, 100);
                using var fs = File.Create(actualPng);
                data.SaveTo(fs);
            }

            var auditReport = RowlEyeAuditor.Audit(
                manager,
                currentDir,
                diffDir,
                [RowlEyeTarget.NodeGraph],
                ["Dark"],
                [1.0]
            );

            Assert.False(auditReport.AllPassed);
            Assert.Equal(1, auditReport.MissingGoldens);
            Assert.Equal("MISSING_GOLDEN", auditReport.Results[0].Status);
        }
        finally
        {
            if (Directory.Exists(tempRoot))
            {
                try { Directory.Delete(tempRoot, true); } catch { }
            }
        }
    }
}
