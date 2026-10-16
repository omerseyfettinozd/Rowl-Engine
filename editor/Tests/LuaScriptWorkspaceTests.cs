#nullable enable
using System;
using System.IO;
using System.Linq;
using System.Text;
using RowlEngine.Editor.Services.Scripting;
using Xunit;

namespace RowlEngine.Editor.Tests;

public sealed class LuaScriptWorkspaceTests
{
    private static string CreateTempAssetsRoot()
    {
        string dir = Path.Combine(Path.GetTempPath(), "rowl-lua-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return dir;
    }

    private static void DeleteTemp(string dir)
    {
        try
        {
            if (Directory.Exists(dir))
                Directory.Delete(dir, recursive: true);
        }
        catch
        {
        }
    }

    private static void WriteText(string assetsRoot, string rel, string content)
    {
        string full = Path.Combine(assetsRoot, rel.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        File.WriteAllText(full, content, new UTF8Encoding(false));
    }

    [Fact]
    public void Files_ListsNestedSorted_IgnoresHiddenNonLuaAndOversize()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "b.lua", "-- b\n");
            WriteText(root, "a.lua", "-- a\n");
            WriteText(root, "scripts/nested/c.lua", "-- c\n");
            WriteText(root, "scripts/ok.lua", "-- ok\n");
            WriteText(root, ".hidden/x.lua", "-- hidden\n");
            WriteText(root, "notes.txt", "not lua\n");
            WriteText(root, "big.lua", new string('x', LuaScriptWorkspace.MaxFileBytes + 16));

            var ws = new LuaScriptWorkspace(root);

            Assert.DoesNotContain(".hidden/x.lua", ws.Files);
            Assert.DoesNotContain("notes.txt", ws.Files);
            Assert.DoesNotContain("big.lua", ws.Files);
            Assert.Contains("a.lua", ws.Files);
            Assert.Contains("b.lua", ws.Files);
            Assert.Contains("scripts/nested/c.lua", ws.Files);
            Assert.Contains("scripts/ok.lua", ws.Files);
            Assert.Contains("big.lua", ws.SkippedFiles);

            var sorted = ws.Files.OrderBy(s => s, StringComparer.OrdinalIgnoreCase)
                .ThenBy(s => s, StringComparer.Ordinal).ToList();
            Assert.Equal(sorted, ws.Files.ToList());
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void Open_Edit_Save_Roundtrip()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "scripts/mira.lua", "-- hello\n");
            var ws = new LuaScriptWorkspace(root);

            var buf = ws.Open("scripts/mira.lua");
            Assert.False(buf.IsDirty);
            Assert.Equal("-- hello\n", buf.Text);

            bool raised = false;
            buf.Changed += (_, _) => raised = true;
            buf.Text = "-- hello\n-- edit\n";

            Assert.True(raised);
            Assert.True(buf.IsDirty);

            ws.Save("scripts/mira.lua");

            Assert.False(buf.IsDirty);
            Assert.Equal("-- hello\n-- edit\n", buf.SavedText);
            string onDisk = File.ReadAllText(
                Path.Combine(root, "scripts", "mira.lua"), Encoding.UTF8);
            Assert.Equal("-- hello\n-- edit\n", onDisk);
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void Open_NormalisesCrlf()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "a.lua", "a\r\nb\r\n");
            var ws = new LuaScriptWorkspace(root);
            var buf = ws.Open("a.lua");
            Assert.Equal("a\nb\n", buf.Text);
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void Open_ToleratesBom()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            string full = Path.Combine(root, "b.lua");
            File.WriteAllText(full, "-- x\n", new UTF8Encoding(true));
            var ws = new LuaScriptWorkspace(root);
            var buf = ws.Open("b.lua");
            Assert.Equal("-- x\n", buf.Text);
            Assert.False(buf.Text.StartsWith("\uFEFF", StringComparison.Ordinal));
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void CreateNew_PicksNextFreeNumber()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            var ws = new LuaScriptWorkspace(root);
            string first = ws.CreateNew("-- Yeni bir hikâye\n\n");
            Assert.Equal("scripts/script-1.lua", first);
            Assert.Equal(first, ws.ActivePath);
            Assert.True(File.Exists(Path.Combine(root, "scripts", "script-1.lua")));
            Assert.Contains(first, ws.Files);

            string second = ws.CreateNew("-- Yeni bir hikâye\n\n");
            Assert.Equal("scripts/script-2.lua", second);
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void CreateNew_SkipsExistingOnDisk()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "scripts/script-1.lua", "-- 1\n");
            WriteText(root, "scripts/script-2.lua", "-- 2\n");
            var ws = new LuaScriptWorkspace(root);
            string next = ws.CreateNew("-- Yeni bir hikâye\n\n");
            Assert.Equal("scripts/script-3.lua", next);
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Theory]
    [InlineData("a b.lua")]
    [InlineData("../x.lua")]
    [InlineData("/etc/x.lua")]
    [InlineData("x.txt")]
    [InlineData("scripts//x.lua")]
    [InlineData("C:/x.lua")]
    [InlineData("scripts/../x.lua")]
    [InlineData("ok.LUA")]
    [InlineData("")]
    [InlineData("scripts/")]
    public void PathValidation_Throws(string bad)
    {
        string root = CreateTempAssetsRoot();
        try
        {
            var ws = new LuaScriptWorkspace(root);
            Assert.Throws<ArgumentException>(() => ws.Open(bad));
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void Close_DirtyRequiresDiscard()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "a.lua", "-- a\n");
            var ws = new LuaScriptWorkspace(root);
            var buf = ws.Open("a.lua");
            ws.Activate("a.lua");
            buf.Text = "-- dirty\n";

            Assert.False(ws.Close("a.lua", discardChanges: false));
            Assert.Single(ws.OpenBuffers);

            Assert.True(ws.Close("a.lua", discardChanges: true));
            Assert.Empty(ws.OpenBuffers);
            Assert.Null(ws.ActivePath);
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void CountMatches_NonOverlapping_EmptyZero()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "a.lua", "ab ab ab");
            var ws = new LuaScriptWorkspace(root);
            ws.Open("a.lua");
            Assert.Equal(3, ws.CountMatches("a.lua", "ab"));
            Assert.Equal(0, ws.CountMatches("a.lua", ""));

            var buf = ws.Open("a.lua");
            buf.Text = "aaa";
            Assert.Equal(1, ws.CountMatches("a.lua", "aa"));
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void CaretPosition_Cases()
    {
        Assert.Equal((1, 1), LuaScriptWorkspace.CaretPosition("ab\ncd", 0));
        Assert.Equal((2, 1), LuaScriptWorkspace.CaretPosition("ab\ncd", 3));
        Assert.Equal((2, 3), LuaScriptWorkspace.CaretPosition("ab\ncd", 100));
        Assert.Equal((1, 1), LuaScriptWorkspace.CaretPosition("", 0));
        Assert.Equal((2, 1), LuaScriptWorkspace.CaretPosition("a\nb\nc", 2));
        Assert.Equal("  ", LuaScriptWorkspace.IndentOnTab);
    }

    [Fact]
    public void HasExternalChange_And_ReloadRefusesWhenDirty()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "a.lua", "v1\n");
            var ws = new LuaScriptWorkspace(root);
            var buf = ws.Open("a.lua");
            Assert.False(ws.HasExternalChange("a.lua"));

            File.WriteAllText(Path.Combine(root, "a.lua"), "v2\n", new UTF8Encoding(false));
            Assert.True(ws.HasExternalChange("a.lua"));

            // Temizken yeniden yükleme dışarıdaki içeriği alır.
            ws.Reload("a.lua");
            Assert.Equal("v2\n", buf.Text);
            Assert.False(buf.IsDirty);
            Assert.False(ws.HasExternalChange("a.lua"));

            // Kirliyken yeniden yükleme reddedilir.
            buf.Text = "dirty\n";
            File.WriteAllText(Path.Combine(root, "a.lua"), "v3\n", new UTF8Encoding(false));
            Assert.Throws<InvalidOperationException>(() => ws.Reload("a.lua"));
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void Refresh_KeepsMissingDirtyBuffer()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "a.lua", "-- a\n");
            var ws = new LuaScriptWorkspace(root);
            var buf = ws.Open("a.lua");
            buf.Text = "-- dirty\n";

            File.Delete(Path.Combine(root, "a.lua"));
            ws.Refresh();

            Assert.True(buf.IsMissingOnDisk);
            Assert.Contains("a.lua", ws.Files);
        }
        finally
        {
            DeleteTemp(root);
        }
    }

    [Fact]
    public void SaveAll_SavesDirtyCount()
    {
        string root = CreateTempAssetsRoot();
        try
        {
            WriteText(root, "a.lua", "-- a\n");
            WriteText(root, "b.lua", "-- b\n");
            var ws = new LuaScriptWorkspace(root);
            ws.Open("a.lua").Text = "-- a2\n";
            ws.Open("b.lua");
            Assert.Equal(1, ws.SaveAll());
            Assert.Equal(0, ws.SaveAll());
        }
        finally
        {
            DeleteTemp(root);
        }
    }
}
