using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class EditorSaveDecisionTests : IDisposable
{
    private readonly string _previousRoot = MainWindowViewModel.ProjectRoot;
    private readonly string _parent = Path.Combine(Path.GetTempPath(), "RowlSaveDecision_" + Guid.NewGuid().ToString("N"));
    private readonly MainWindowViewModel _vm;
    private readonly string _root;
    private string JsonDirectory => Path.Combine(_root, "Assets", "json");
    private string GraphPath => Path.Combine(JsonDirectory, "full_story_graph.json");

    public EditorSaveDecisionTests()
    {
        var created = ProjectFactory.CreateNewProject("SaveDecision", _parent);
        Assert.True(created.Success, created.Error);
        _root = created.Info!.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
        _vm.Settings.AutoSaveEnabled = false;
        Assert.True(_vm.SaveProjectNow());
    }

    public void Dispose()
    {
        _vm.Dispose();
        MainWindowViewModel.ProjectRoot = _previousRoot;
        Directory.Delete(_parent, true);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task DiscardAndDisposePreserveBothGraphFiles(bool autosave)
    {
        var before = Directory.GetFiles(JsonDirectory, "*.json")
            .ToDictionary(path => path, File.ReadAllBytes);
        _vm.Settings.AutoSaveEnabled = autosave;
        _vm.Settings.AutoSaveIntervalSeconds = 3600;
        _vm.Nodes[0].DialogueText = "DISCARD_SENTINEL";
        _vm.ScheduleSave();
        Assert.True(CrashRecoveryService.IsDirty(JsonDirectory));

        Assert.True(await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("discard"), _vm.SaveProjectNow,
            () => _vm.IsProjectDirty, _vm.DiscardPendingProjectChanges));
        _vm.Dispose();

        Assert.False(CrashRecoveryService.IsDirty(JsonDirectory));
        foreach (var pair in before)
            Assert.Equal(pair.Value, File.ReadAllBytes(pair.Key));
    }

    [Fact]
    public async Task DiscardInvalidatesQueuedBackgroundSave()
    {
        byte[] before = File.ReadAllBytes(GraphPath);
        _vm.Nodes[0].DialogueText = "QUEUED_DISCARD_SENTINEL";
        _vm.ScheduleSave();
        Task save;
        // Hold the commit gate until discard has invalidated the captured job.
        lock (_vm.ProjectSaveGateForTests)
        {
            save = _vm.SaveProjectAsync();
            Assert.True(_vm.DiscardPendingProjectChanges());
        }
        await save.WaitAsync(TimeSpan.FromSeconds(5));
        _vm.Dispose();
        Assert.Equal(before, File.ReadAllBytes(GraphPath));
        Assert.False(CrashRecoveryService.IsDirty(JsonDirectory));
    }

    [Fact]
    public async Task CancelPreservesDirtyEditsAndDoesNotWrite()
    {
        byte[] before = File.ReadAllBytes(GraphPath);
        _vm.Nodes[0].DialogueText = "CANCEL_SENTINEL";
        _vm.ScheduleSave();
        Assert.False(await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("cancel"), _vm.SaveProjectNow,
            () => _vm.IsProjectDirty, _vm.DiscardPendingProjectChanges));
        Assert.True(_vm.IsProjectDirty);
        Assert.True(CrashRecoveryService.IsDirty(JsonDirectory));
        Assert.Equal(before, File.ReadAllBytes(GraphPath));
        Assert.True(_vm.DiscardPendingProjectChanges());
    }

    [Fact]
    public async Task FailedSavePreventsCloseAndSaveAsPublication()
    {
        _vm.Nodes[0].DialogueText = "FAILED_SAVE_SENTINEL";
        _vm.ScheduleSave();
        byte[] before = File.ReadAllBytes(GraphPath);
        // A directory in place of the canonical file produces a real portable
        // filesystem write failure, without requiring root-sensitive permissions.
        File.Delete(GraphPath);
        Directory.CreateDirectory(GraphPath);
        try
        {
            Assert.False(await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
                () => Task.FromResult<string?>("save"), _vm.SaveProjectNow,
                () => _vm.IsProjectDirty, _vm.DiscardPendingProjectChanges));
            Assert.True(_vm.IsProjectDirty);
            string target = Path.Combine(_parent, "failed-copy");
            var result = EditorProjectLifecycleCoordinator.ExecuteSaveAs(
                _root, target, _vm.Nodes.Count, 101, _vm.SaveProjectNow);
            Assert.False(result.Succeeded);
            Assert.False(Directory.Exists(target));
            string buildTarget = Path.Combine(_parent, "failed-build");
            var build = await EditorBuildCoordinator.BuildStandaloneGameAsync(
                _root, Path.Combine(_root, "Assets"), buildTarget,
                _vm.Nodes, _vm.Connections, 101, _vm.SaveProjectNow,
                _ => throw new Exception("Validation must not follow failed persistence."));
            Assert.False(build.Succeeded);
            Assert.Equal("save_project", build.Diagnostic?.Operation);
            Assert.False(Directory.Exists(buildTarget));
        }
        finally
        {
            Directory.Delete(GraphPath);
            File.WriteAllBytes(GraphPath, before);
            Assert.True(_vm.DiscardPendingProjectChanges());
        }
    }

    [Fact]
    public async Task FailedRecoveryCleanupPreventsDiscardClose()
    {
        _vm.Nodes[0].DialogueText = "DISCARD_CLEANUP_FAILURE";
        _vm.ScheduleSave();
        string marker = Path.Combine(JsonDirectory, ".recovery", "dirty.flag");
        File.Delete(marker);
        Directory.CreateDirectory(marker);
        try
        {
            Assert.False(await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
                () => Task.FromResult<string?>("discard"), _vm.SaveProjectNow,
                () => _vm.IsProjectDirty, _vm.DiscardPendingProjectChanges));
            Assert.True(_vm.IsProjectDirty);
        }
        finally
        {
            Directory.Delete(marker);
            Assert.True(_vm.DiscardPendingProjectChanges());
        }
    }

    [Fact]
    public async Task FailedPersistenceStopsBothBuildPathsBeforeValidationOrOutput()
    {
        byte[] before = File.ReadAllBytes(GraphPath);
        string output = Path.Combine(_parent, "existing-output");
        Directory.CreateDirectory(output);
        string sentinel = Path.Combine(output, "keep.txt");
        File.WriteAllText(sentinel, "existing artifact");
        var diagnostics = new List<BuildDiagnostic>();
        int issueCalls = 0;
        var sync = EditorBuildCoordinator.ExecuteBuildPipeline(
            _root, Path.Combine(_root, "Assets"), output, _vm.Nodes, _vm.Connections,
            101, () => false, _ => issueCalls++, reportDiagnostic: diagnostics.Add);
        var asyncResult = await EditorBuildCoordinator.BuildStandaloneGameAsync(
            _root, Path.Combine(_root, "Assets"), output, _vm.Nodes, _vm.Connections,
            101, () => false, _ => issueCalls++, reportDiagnostic: diagnostics.Add);
        Assert.False(sync.Succeeded);
        Assert.False(asyncResult.Succeeded);
        Assert.Equal(0, issueCalls);
        Assert.Equal(2, diagnostics.Count);
        Assert.All(diagnostics, d =>
        {
            Assert.Equal(BuildDiagnosticCode.IoFailure, d.Code);
            Assert.Equal("save_project", d.Operation);
        });
        Assert.Equal("existing artifact", File.ReadAllText(sentinel));
        Assert.Single(Directory.GetFiles(output));
        Assert.Equal(before, File.ReadAllBytes(GraphPath));
    }

    [Fact]
    public void FailedSaveAsPreservesExistingTarget()
    {
        string target = Path.Combine(_parent, "existing-copy");
        Directory.CreateDirectory(target);
        string sentinel = Path.Combine(target, "project.rowlproj");
        File.WriteAllText(sentinel, "existing project");
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(
            _root, target, _vm.Nodes.Count, 101, () => false);
        Assert.False(result.Succeeded);
        Assert.Equal("existing project", File.ReadAllText(sentinel));
        Assert.False(Directory.Exists(Path.Combine(target, "Assets")));
    }

    [Fact]
    public async Task SaveChoicePersistsEditsAndAllowsClose()
    {
        _vm.Nodes[0].DialogueText = "SAVE_SENTINEL";
        _vm.ScheduleSave();
        Assert.True(await EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("save"), _vm.SaveProjectNow,
            () => _vm.IsProjectDirty, _vm.DiscardPendingProjectChanges));
        Assert.False(_vm.IsProjectDirty);
        Assert.False(CrashRecoveryService.IsDirty(JsonDirectory));
        Assert.Contains("SAVE_SENTINEL", File.ReadAllText(GraphPath));
    }
}
