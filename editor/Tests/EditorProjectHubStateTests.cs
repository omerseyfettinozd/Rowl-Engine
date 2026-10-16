using System;
using System.Globalization;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

// Hub durumları: Ready/Missing/Invalid + işlem sonucu dili. UI gerektirmez.
public sealed class EditorProjectHubStateTests : IDisposable
{
    private readonly string _tempRoot;
    private readonly string _registryJson;

    public EditorProjectHubStateTests()
    {
        _tempRoot = Path.Combine(Path.GetTempPath(), "RowlHubState_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempRoot);
        _registryJson = Path.Combine(_tempRoot, "registry.json");
    }

    public void Dispose()
    {
        try { Directory.Delete(_tempRoot, recursive: true); } catch { }
    }

    private string NewParent(string name)
    {
        string dir = Path.Combine(_tempRoot, name);
        Directory.CreateDirectory(dir);
        return dir;
    }

    private static string Format(string key, params object[] args) =>
        string.Format(CultureInfo.CurrentCulture, EditorStrings.T(key), args);

    [Fact]
    public void Probe_RealProject_IsReady()
    {
        string parent = NewParent("p1");
        var created = ProjectFactory.CreateNewProject("Ad", parent);
        Assert.True(created.Success);
        Assert.NotNull(created.Info);
        Assert.Equal(ProjectCardState.Ready, ProjectHubViewModel.ProbeProject(created.Info!.Path));
    }

    [Fact]
    public void Probe_DeletedFolder_IsMissing()
    {
        string parent = NewParent("p2");
        var created = ProjectFactory.CreateNewProject("Ad", parent);
        Assert.True(created.Success);
        string path = created.Info!.Path;
        Directory.Delete(path, recursive: true);
        Assert.Equal(ProjectCardState.Missing, ProjectHubViewModel.ProbeProject(path));
    }

    [Fact]
    public void Probe_EmptyFolder_IsInvalid()
    {
        string empty = Path.Combine(NewParent("p3"), "Bos");
        Directory.CreateDirectory(empty);
        Assert.Equal(ProjectCardState.Invalid, ProjectHubViewModel.ProbeProject(empty));
    }

    [Fact]
    public void OpenProject_MissingCard_DoesNotRaise_AndSetsError()
    {
        string parent = NewParent("p4");
        var created = ProjectFactory.CreateNewProject("Ad", parent);
        Assert.True(created.Success);
        string path = created.Info!.Path;

        var registry = new ProjectRegistryService(_registryJson);
        registry.Add(created.Info!);
        var hub = new ProjectHubViewModel(registry);
        Assert.Single(hub.Projects);

        Directory.Delete(path, recursive: true);
        var card = hub.Projects[0];

        bool raised = false;
        hub.ProjectOpened += _ => raised = true;
        hub.OpenProject(card);

        Assert.False(raised);
        Assert.Equal(HubStatusKind.Error, hub.StatusKind);
        Assert.True(hub.IsStatusError);
        Assert.Equal(ProjectCardState.Missing, card.State);
        Assert.False(card.CanOpen);
        Assert.False(card.OpenCommand.CanExecute(null));
    }

    [Fact]
    public void ImportFolder_EmptyFolder_ReturnsFalse_AndError()
    {
        var registry = new ProjectRegistryService(_registryJson);
        var hub = new ProjectHubViewModel(registry);
        string empty = Path.Combine(NewParent("p5"), "Bos");
        Directory.CreateDirectory(empty);

        bool ok = hub.ImportFolder(empty);
        Assert.False(ok);
        Assert.Equal(HubStatusKind.Error, hub.StatusKind);
        Assert.Equal(EditorStrings.T("Hub.Status.NotAProject"), hub.StatusText);
    }

    [Fact]
    public void ImportFolder_RealProject_ThenDuplicate()
    {
        string parent = NewParent("p6");
        var created = ProjectFactory.CreateNewProject("Gercek", parent);
        Assert.True(created.Success);
        string path = created.Info!.Path;

        var registry = new ProjectRegistryService(_registryJson);
        var hub = new ProjectHubViewModel(registry);

        bool first = hub.ImportFolder(path);
        Assert.True(first);
        Assert.Equal(HubStatusKind.Success, hub.StatusKind);
        Assert.Equal(Format("Hub.Status.Imported", "Gercek"), hub.StatusText);

        bool second = hub.ImportFolder(path);
        Assert.False(second);
        Assert.Equal(HubStatusKind.Neutral, hub.StatusKind);
        Assert.Equal(Format("Hub.Status.AlreadyListed", "Gercek"), hub.StatusText);
    }

    [Fact]
    public void RemoveProject_RemovesFromRegistry_AndKeepsDisk()
    {
        string parent = NewParent("p7");
        var created = ProjectFactory.CreateNewProject("Silinecek", parent);
        Assert.True(created.Success);
        string path = created.Info!.Path;

        var registry = new ProjectRegistryService(_registryJson);
        var hub = new ProjectHubViewModel(registry);
        Assert.True(hub.ImportFolder(path));
        var card = hub.Projects.First(c => string.Equals(c.Path, Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase));

        hub.RemoveProject(card);

        Assert.DoesNotContain(registry.Projects, p => p.Id == card.Info.Id);
        Assert.True(Directory.Exists(path));
        Assert.Equal(Format("Hub.Status.Removed", card.Name), hub.StatusText);
    }

    [Fact]
    public void StatusText_CountFormats()
    {
        var registry = new ProjectRegistryService(_registryJson);
        var hub = new ProjectHubViewModel(registry);
        Assert.Equal(EditorStrings.T("Hub.Status.Empty"), hub.StatusText);

        string parent = NewParent("p8");
        var created = ProjectFactory.CreateNewProject("Bir", parent);
        Assert.True(created.Success);
        registry.Add(created.Info!);
        hub.RefreshProjects();
        Assert.Equal(Format("Hub.Status.Count", 1), hub.StatusText);

        // Geçersiz klasörü elle kaydet; sayımda eksik görünmeli.
        string empty = Path.Combine(_tempRoot, "BosProje");
        Directory.CreateDirectory(empty);
        registry.Add(new ProjectInfo
        {
            Id = Guid.NewGuid().ToString("N"),
            Name = "Bos",
            Path = empty,
            CreatedAt = DateTime.UtcNow,
            LastOpenedAt = DateTime.UtcNow
        });
        hub.RefreshProjects();
        Assert.Equal(Format("Hub.Status.CountWithMissing", 2, 1), hub.StatusText);
    }

    [Fact]
    public void OpenCommand_CanExecute_False_WhenMissing()
    {
        var registry = new ProjectRegistryService(_registryJson);
        var hub = new ProjectHubViewModel(registry);
        string ghost = Path.Combine(_tempRoot, "Yok_" + Guid.NewGuid().ToString("N"));
        var info = new ProjectInfo
        {
            Id = Guid.NewGuid().ToString("N"),
            Name = "Hayalet",
            Path = ghost,
            CreatedAt = DateTime.UtcNow,
            LastOpenedAt = DateTime.UtcNow
        };
        var card = new ProjectCardViewModel(info, hub, registry);
        Assert.Equal(ProjectCardState.Missing, card.State);
        Assert.True(card.IsUnavailable);
        Assert.False(card.CanOpen);
        Assert.False(card.OpenCommand.CanExecute(null));
    }
}
