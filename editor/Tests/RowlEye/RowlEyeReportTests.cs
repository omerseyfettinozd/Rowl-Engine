using System;
using System.IO;
using RowlEngine.Editor.Tools.RowlEye.Reporting;
using Xunit;

namespace RowlEngine.Editor.Tests.RowlEye;

[Collection("StaticRootSequential")]
public class RowlEyeReportTests
{
    [Fact]
    public void Generate_Creates_Html_With_All_Results()
    {
        string tempOut = Path.Combine(Path.GetTempPath(), $"RowlEyeReport_{Guid.NewGuid():N}", "report.html");
        try
        {
            var model = new RowlEyeReportModel
            {
                TotalCaptures = 2,
                PassedCount = 1,
                FailedCount = 1,
                Items =
                [
                    new RowlEyeReportItem
                    {
                        TargetName = "Hierarchy",
                        Theme = "Dark",
                        Scale = 1.0,
                        Status = "PASSED",
                        MismatchPercentage = 0.0,
                        MismatchedPixels = 0,
                        TotalPixels = 432000,
                        ViewModelJson = "{\"NodeCount\": 2}"
                    },
                    new RowlEyeReportItem
                    {
                        TargetName = "NodeGraph",
                        Theme = "Dark",
                        Scale = 1.0,
                        Status = "FAILED",
                        MismatchPercentage = 2.45,
                        MismatchedPixels = 50800,
                        TotalPixels = 2073600,
                        ViewModelJson = "{\"ActiveNode\": 101}"
                    }
                ]
            };

            string generatedPath = RowlEyeReportGenerator.Generate(model, tempOut);

            Assert.True(File.Exists(generatedPath));
            string html = File.ReadAllText(generatedPath);

            Assert.Contains("Rowl.Eye Visual Inspection Report", html);
            Assert.Contains("Hierarchy", html);
            Assert.Contains("NodeGraph", html);
            Assert.Contains("NodeCount", html);
            Assert.Contains("ActiveNode", html);
            Assert.Contains("PASSED", html);
            Assert.Contains("FAILED", html);
        }
        finally
        {
            string? dir = Path.GetDirectoryName(tempOut);
            if (Directory.Exists(dir))
            {
                try { Directory.Delete(dir, true); } catch { }
            }
        }
    }
}
