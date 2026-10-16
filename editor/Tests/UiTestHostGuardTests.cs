using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Sıra bağımlı Dispatcher kirliliğine karşı kaynak kilidi: test kodu VM'i yalnızca
/// <see cref="UiTestHost"/> üzerinden kurar (bkz. <see cref="UiTestHost"/> belgesi).
/// </summary>
public sealed class UiTestHostGuardTests
{
    [Fact]
    public void TestCode_BuildsMainWindowViewModelOnlyThroughUiTestHost()
    {
        string repoRoot = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
        string testsDir = Path.Combine(repoRoot, "editor", "Tests");
        var offenders = new List<string>();

        foreach (string file in Directory.EnumerateFiles(testsDir, "*.cs", SearchOption.AllDirectories))
        {
            string name = Path.GetFileName(file);
            if (name is "UiTestHost.cs" or "UiTestHostGuardTests.cs")
                continue;
            string[] lines = File.ReadAllLines(file);
            for (int i = 0; i < lines.Length; i++)
            {
                if (lines[i].Contains("new MainWindowViewModel(", StringComparison.Ordinal))
                    offenders.Add($"{Path.GetRelativePath(testsDir, file)}:{i + 1}");
            }
        }

        Assert.True(offenders.Count == 0,
            "UiTestHost.CreateViewModel kullanılmalı (xUnit işçisinde VM kurmak Dispatcher.UIThread'i kaçırır): "
            + string.Join(", ", offenders));
    }
}
