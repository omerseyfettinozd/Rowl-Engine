using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.NodeLibrary;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

// Faz 7 D3 — kütüphane görünüm modeli davranışları (sahte kök dizisi).
[Collection("StaticRootSequential")]
public sealed class NodeLibraryViewModelTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public NodeLibraryViewModelTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlLibraryVm_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("LibraryVmTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
    }

    public void Dispose()
    {
        try { _vm.Dispose(); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); }
        catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    private static string FreshLibraryPath()
    {
        return Path.Combine(Path.GetTempPath(), "RowlLib_" + Guid.NewGuid().ToString("N") + ".json");
    }

    [Fact]
    public void SaveSelectedNode_PersistsTitleAndRemoveClears()
    {
        string libPath = FreshLibraryPath();
        try
        {
            var lib = new NodeLibraryViewModel(_vm, libPath);
            NodeViewModel node = _vm.Nodes.First();
            node.Title = "LibraryNode-ABC-123";
            _vm.SelectedNode = node;

            Assert.False(lib.IsSelectedNodeSaved);
            lib.ToggleSaveSelectedNodeCommand.Execute(null);
            Assert.True(lib.IsSelectedNodeSaved);
            Assert.True(File.Exists(libPath));
            Assert.Contains("LibraryNode-ABC-123", File.ReadAllText(libPath));

            lib.ToggleSaveSelectedNodeCommand.Execute(null);
            Assert.False(lib.IsSelectedNodeSaved);
        }
        finally
        {
            try { File.Delete(libPath); } catch (Exception) { }
        }
    }

    [Fact]
    public void UpdateRecord_ReplacesTemplateAndReloads()
    {
        string libPath = FreshLibraryPath();
        try
        {
            var lib = new NodeLibraryViewModel(_vm, libPath);
            NodeViewModel node = _vm.Nodes.First();
            node.Title = "First-Title-XYZ";
            _vm.SelectedNode = node;
            lib.ToggleSaveSelectedNodeCommand.Execute(null);
            Assert.True(lib.IsSelectedNodeSaved);

            node.Title = "Updated-Title-XYZ";
            lib.UpdateSelectedNodeRecordCommand.Execute(null);

            var fresh = new NodeLibraryViewModel(_vm, libPath);
            LibraryItem single = Assert.Single(fresh.State.Items);
            Assert.Equal("Updated-Title-XYZ", single.Template.Title);
        }
        finally
        {
            try { File.Delete(libPath); } catch (Exception) { }
        }
    }

    [Fact]
    public void CreateFolder_DuplicateKeepsState()
    {
        string libPath = FreshLibraryPath();
        try
        {
            var lib = new NodeLibraryViewModel(_vm, libPath);
            Assert.True(lib.CreateFolder("Bolum 1", null));
            NodeLibraryState afterFirst = lib.State;
            int count = afterFirst.Folders.Count;
            Assert.True(count > 0);

            Assert.False(lib.CreateFolder("Bolum 1", null));
            Assert.Same(afterFirst, lib.State);
            Assert.Equal(count, lib.State.Folders.Count);
        }
        finally
        {
            try { File.Delete(libPath); } catch (Exception) { }
        }
    }

    [Fact]
    public void AddItemToScene_GrowsNodes()
    {
        string libPath = FreshLibraryPath();
        try
        {
            var lib = new NodeLibraryViewModel(_vm, libPath);
            NodeViewModel node = _vm.Nodes.First();
            node.Title = "AddMe-Node-456";
            _vm.SelectedNode = node;
            lib.ToggleSaveSelectedNodeCommand.Execute(null);
            Assert.True(lib.IsSelectedNodeSaved);

            lib.Source = LibrarySource.User;
            lib.SelectFolder(null);
            LibraryRowViewModel row = Assert.Single(lib.Rows, r => r.Kind == LibraryRowKind.Item);

            int before = _vm.Nodes.Count;
            row.ActivateCommand.Execute(null);
            Assert.Equal(before + 1, _vm.Nodes.Count);
        }
        finally
        {
            try { File.Delete(libPath); } catch (Exception) { }
        }
    }

    [Fact]
    public void BuiltinStandardNode_AddsNode()
    {
        string libPath = FreshLibraryPath();
        try
        {
            var lib = new NodeLibraryViewModel(_vm, libPath);
            lib.Source = LibrarySource.Builtin;
            lib.OpenPackageCommand.Execute(null);
            LibraryRowViewModel row = Assert.Single(lib.Rows, r => r.Kind == LibraryRowKind.BuiltinNode);

            int before = _vm.Nodes.Count;
            row.ActivateCommand.Execute(null);
            Assert.Equal(before + 1, _vm.Nodes.Count);
        }
        finally
        {
            try { File.Delete(libPath); } catch (Exception) { }
        }
    }

    [Fact]
    public void ReadOnly_MutationsDoNotWriteFile()
    {
        string libPath = FreshLibraryPath();
        try
        {
            File.WriteAllText(libPath, "{\"schema_version\":2,\"folders\":[],\"items\":[]}");
            byte[] before = File.ReadAllBytes(libPath);

            var ro = new NodeLibraryViewModel(_vm, libPath);
            Assert.True(ro.IsReadOnly);

            ro.CreateFolder("ReadOnlyFolder", null);

            byte[] after = File.ReadAllBytes(libPath);
            Assert.Equal(before, after);
        }
        finally
        {
            try { File.Delete(libPath); } catch (Exception) { }
        }
    }
}
