#nullable enable
using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 7 D5 — Lua paneli görünüm modeli: ilk dosya, kirli/kaydet, yeni betik, bağlama + geri alma, imleç metni.</summary>
[Collection("StaticRootSequential")]
public sealed class LuaEditorViewModelTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;
    private readonly string _assets;

    public LuaEditorViewModelTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlLuaTest_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("LuaTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
        _assets = Path.Combine(Path.GetTempPath(), "RowlLuaAssets_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_assets);
        UndoRedoService.Instance.Clear();
    }

    public void Dispose()
    {
        try { _vm.Dispose(); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); } catch (Exception) { }
        try { Directory.Delete(_assets, recursive: true); } catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
        UndoRedoService.Instance.Clear();
    }

    private void Write(string rel, string content)
    {
        string full = Path.Combine(_assets, rel.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        File.WriteAllText(full, content);
    }

    private void EnsureNoScripts()
    {
        var node = _vm.SelectedNode ?? _vm.Nodes.First();
        _vm.SelectedNode = node;
        foreach (var obj in node.Objects)
        {
            foreach (var c in obj.Components.OfType<ScriptComponentViewModel>().ToList())
                obj.RemoveComponent(c);
        }
    }

    [Fact]
    public void OpensFirstFile_WhenNodeHasNoScripts()
    {
        Write("first.lua", "-- first\n");
        Write("second.lua", "-- second\n");
        EnsureNoScripts();

        var lua = new LuaEditorViewModel(_vm, _assets);

        Assert.Equal("first.lua", lua.ActivePath);
        Assert.Equal(lua.Workspace.OpenBuffers.Select(b => b.RelativePath), lua.Tabs.Select(t => t.RelativePath));
        Assert.Single(lua.Tabs);
    }

    [Fact]
    public void UpdateActiveText_MarksDirty_Save_Clears()
    {
        Write("a.lua", "-- a\n");
        EnsureNoScripts();
        var lua = new LuaEditorViewModel(_vm, _assets);

        lua.UpdateActiveText("-- changed\n");
        Assert.True(lua.Tabs.Single().IsDirty);
        Assert.True(lua.IsActiveDirty);

        lua.Save(lua.ActivePath);
        string full = Path.Combine(_assets, "a.lua");
        Assert.Equal("-- changed\n", File.ReadAllText(full));
        Assert.False(lua.Tabs.Single().IsDirty);
        Assert.False(lua.IsActiveDirty);
    }

    [Fact]
    public void New_CreatesScript1_AndActivates()
    {
        Write("a.lua", "-- a\n");
        EnsureNoScripts();
        var lua = new LuaEditorViewModel(_vm, _assets);

        string path = lua.CreateNewFile();

        Assert.Equal("scripts/script-1.lua", path);
        Assert.Equal("scripts/script-1.lua", lua.ActivePath);
        Assert.True(File.Exists(Path.Combine(_assets, "scripts", "script-1.lua")));
        Assert.Contains(lua.Tabs, t => t.RelativePath == "scripts/script-1.lua");
    }

    [Fact]
    public void Bind_WithoutComponent_Adds_Undo_Removes()
    {
        Write("s.lua", "-- s\n");
        EnsureNoScripts();
        var lua = new LuaEditorViewModel(_vm, _assets);
        var node = _vm.SelectedNode ?? _vm.Nodes.First();
        _vm.SelectedNode = node;
        var obj = node.CreateObject("BindMe");

        lua.BindObject(obj);

        var comp = Assert.Single(obj.Components.OfType<ScriptComponentViewModel>());
        Assert.Equal(lua.ActivePath, comp.ScriptPath);
        UndoRedoService.Instance.Undo();
        Assert.Empty(obj.Components.OfType<ScriptComponentViewModel>());
    }

    [Fact]
    public void Bind_WithComponent_ChangesPath_Undo_Restores()
    {
        Write("s.lua", "-- s\n");
        EnsureNoScripts();
        var lua = new LuaEditorViewModel(_vm, _assets);
        var node = _vm.SelectedNode ?? _vm.Nodes.First();
        _vm.SelectedNode = node;
        var obj = node.CreateObject("HasScript");
        var comp = new ScriptComponentViewModel { ScriptPath = "old.lua" };
        obj.AddComponent(comp);
        UndoRedoService.Instance.Clear();

        lua.BindObject(obj);

        Assert.Equal(lua.ActivePath, comp.ScriptPath);
        UndoRedoService.Instance.Undo();
        Assert.Equal("old.lua", comp.ScriptPath);
    }

    [Fact]
    public void CaretStatus_Formats_Tr()
    {
        Write("a.lua", "a\nbc");
        EnsureNoScripts();
        EditorStrings.Instance.SetLanguage("tr");
        var lua = new LuaEditorViewModel(_vm, _assets);

        lua.UpdateCaret(4);

        Assert.Equal("Satır 2 · Sütun 3", lua.StatusCaret);
    }
}
