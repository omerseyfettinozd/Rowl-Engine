using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>C04 — Farklı Kaydet kardeş hazırlık klasörüne kopyalar; yarım/ezilmiş hedef kalmaz.</summary>
public sealed class EditorSaveAsAtomicTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "RowlSaveAsAtomic_" + Guid.NewGuid().ToString("N"));
    private readonly string _src;

    public EditorSaveAsAtomicTests()
    {
        _src = Path.Combine(_root, "src");
        Directory.CreateDirectory(Path.Combine(_src, "Assets", "json"));
        File.WriteAllText(Path.Combine(_src, "Assets", "json", "a.json"), "{\"v\":1}");
        File.WriteAllText(Path.Combine(_src, "Assets", "b.txt"), "bbb");
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch (Exception) { }
    }

    private string Out(string name) => Path.Combine(_root, "out", name);

    [Fact]
    public void ExistingNonEmptyTarget_IsRefused_AndUntouched()
    {
        string target = Out("t");
        Directory.CreateDirectory(target);
        File.WriteAllText(Path.Combine(target, "keep.txt"), "old");

        var r = ProjectSaveAsCoordinator.SaveProjectCopy(_src, target, 1, 1, () => { });

        Assert.False(r.Succeeded);
        Assert.Equal("old", File.ReadAllText(Path.Combine(target, "keep.txt")));
        Assert.Single(Directory.GetFileSystemEntries(target));
        Assert.Single(Directory.GetFileSystemEntries(Path.GetDirectoryName(target)!));
    }

    [Fact]
    public void FailureMidCopy_LeavesNoTargetAndNoStaging()
    {
        string target = Out("t");
        Assert.Throws<IOException>(() => ProjectSaveAsCoordinator.SaveProjectCopy(
            _src, target, 1, 1, () => { }, afterAssetsCopied: () => throw new IOException("disk dolu")));

        Assert.False(Directory.Exists(target));
        Assert.Empty(Directory.GetFileSystemEntries(Path.GetDirectoryName(target)!));
        Assert.True(File.Exists(Path.Combine(_src, "Assets", "b.txt")));
    }

    [Fact]
    public void FailureMidCopy_PreExistingEmptyTarget_Survives()
    {
        string target = Out("t");
        Directory.CreateDirectory(target);
        Assert.Throws<IOException>(() => ProjectSaveAsCoordinator.SaveProjectCopy(
            _src, target, 1, 1, () => { }, afterAssetsCopied: () => throw new IOException("x")));
        Assert.True(Directory.Exists(target));
        Assert.Single(Directory.GetFileSystemEntries(Path.GetDirectoryName(target)!));
    }

    [Fact]
    public void Success_ProducesFullCopy_NoStagingLeft()
    {
        string target = Out("t");
        var r = ProjectSaveAsCoordinator.SaveProjectCopy(_src, target, 1, 1, () => { });
        Assert.True(r.Succeeded);
        Assert.Equal("bbb", File.ReadAllText(Path.Combine(target, "Assets", "b.txt")));
        Assert.True(File.Exists(Path.Combine(target, "project.rowlproj")));
        Assert.Single(Directory.GetFileSystemEntries(Path.GetDirectoryName(target)!));
    }

    [Fact]
    public void TwoSaveAs_SameAutoName_KeepBothCopies_FirstByteIdentical()
    {
        var stamp = new DateTime(2026, 1, 2, 3, 4, 5);
        string parent = Out("p");
        string first = EditorProjectLifecycleCoordinator.EnsureUniqueSaveAsTarget(
            EditorProjectLifecycleCoordinator.GenerateSaveAsTargetDirectory(parent, stamp));
        Assert.True(ProjectSaveAsCoordinator.SaveProjectCopy(_src, first, 1, 1, () => { }).Succeeded);
        byte[] before = File.ReadAllBytes(Path.Combine(first, "project.rowlproj"));

        File.WriteAllText(Path.Combine(_src, "Assets", "b.txt"), "changed");
        string second = EditorProjectLifecycleCoordinator.EnsureUniqueSaveAsTarget(
            EditorProjectLifecycleCoordinator.GenerateSaveAsTargetDirectory(parent, stamp));
        Assert.NotEqual(first, second);
        Assert.True(ProjectSaveAsCoordinator.SaveProjectCopy(_src, second, 1, 1, () => { }).Succeeded);

        Assert.Equal("bbb", File.ReadAllText(Path.Combine(first, "Assets", "b.txt")));
        Assert.Equal(before, File.ReadAllBytes(Path.Combine(first, "project.rowlproj")));
        Assert.Equal("changed", File.ReadAllText(Path.Combine(second, "Assets", "b.txt")));
        Assert.Equal(2, Directory.GetDirectories(parent).Length);
    }

    [Fact]
    public void ExecuteSaveAs_ExistingNonEmptyTarget_ReportsFailure()
    {
        string target = Out("t");
        Directory.CreateDirectory(target);
        File.WriteAllText(Path.Combine(target, "keep.txt"), "old");
        var r = EditorProjectLifecycleCoordinator.ExecuteSaveAs(_src, target, 1, 1, () => { });
        Assert.False(r.Succeeded);
        Assert.False(string.IsNullOrEmpty(r.Message));
    }
}
