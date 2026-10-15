using System;
using System.IO;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using Xunit;

namespace RowlEngine.Editor.Tests.RowlEye;

public class RowlEyeTargetTests
{
    [Theory]
    [InlineData(RowlEyeTarget.SettingsDialog, "Dark")]
    [InlineData(RowlEyeTarget.NodeGraph, "Dark")]
    [InlineData(RowlEyeTarget.Hierarchy, "Dark")]
    [InlineData(RowlEyeTarget.Inspector, "Dark")]
    [InlineData(RowlEyeTarget.AssetBrowser, "Dark")]
    [InlineData(RowlEyeTarget.LivePreview, "Dark")]
    public void IsolatedTargets_Produce_ValidSnapshots(RowlEyeTarget target, string theme)
    {
        string tempOut = Path.Combine(Path.GetTempPath(), $"RowlEyeTarget_{target}_{Guid.NewGuid():N}");
        try
        {
            string[] args = ["--rowl-eye", "--target", target.ToString(), "--theme", theme, "--scales", "1", "--out", tempOut];
            int exitCode = RowlEyeRunner.Run(args);

            Assert.Equal(0, exitCode);

            string expectedPng = Path.Combine(tempOut, $"{target}_{theme}@1x.png");
            string expectedManifest = Path.Combine(tempOut, $"{target}_{theme}.manifest.json");

            Assert.True(File.Exists(expectedPng), $"Snapshot PNG should exist for {target}: {expectedPng}");
            Assert.True(File.Exists(expectedManifest), $"Manifest JSON should exist for {target}: {expectedManifest}");
            Assert.True(new FileInfo(expectedPng).Length > 1000, $"Snapshot PNG should be non-trivial for {target}");
        }
        finally
        {
            if (Directory.Exists(tempOut))
            {
                try { Directory.Delete(tempOut, true); } catch { }
            }
        }
    }
}
