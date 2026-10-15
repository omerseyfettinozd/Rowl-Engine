using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Kök çözümleme kilidi: başka bir checkout'un içine yerleşmiş worktree
/// (.claude/worktrees/x) kendi köküne çözülmeli. "En üstteki eşleşme"
/// kuralı dış depoyu seçiyordu; worktree'de koşan lint/kayma testleri
/// sessizce ANA checkout'un dosyalarını denetliyordu.
/// </summary>
public sealed class EditorProjectRootResolutionTests
{
    private static void MakeProjectRoot(string dir)
    {
        Directory.CreateDirectory(Path.Combine(dir, "Assets"));
        Directory.CreateDirectory(Path.Combine(dir, "editor"));
        File.WriteAllText(Path.Combine(dir, "CMakeLists.txt"), "# fixture");
    }

    [Fact]
    public void NestedWorktree_ResolvesToItsOwnCheckout()
    {
        string outer = Path.Combine(Path.GetTempPath(), $"RowlRootOuter_{Guid.NewGuid():N}");
        string inner = Path.Combine(outer, ".claude", "worktrees", "w");
        string output = Path.Combine(inner, "editor", "Tests", "bin", "Debug", "net10.0");
        try
        {
            MakeProjectRoot(outer);
            Directory.CreateDirectory(Path.Combine(outer, ".git"));
            MakeProjectRoot(inner);
            File.WriteAllText(Path.Combine(inner, ".git"), "gitdir: ../../../.git/worktrees/w");
            Directory.CreateDirectory(output);

            Assert.Equal(inner, ProjectFileSystem.ResolveProjectRootFrom(output));
        }
        finally
        {
            try { Directory.Delete(outer, true); } catch { }
        }
    }

    [Fact]
    public void WithoutGitBoundary_HighestMatchStillWins()
    {
        // Kurulu/paketlenmiş düzende .git yoktur: eski "en üstteki eşleşme"
        // davranışı korunur.
        string outer = Path.Combine(Path.GetTempPath(), $"RowlRootPlain_{Guid.NewGuid():N}");
        string inner = Path.Combine(outer, "nested");
        string output = Path.Combine(inner, "editor", "bin");
        try
        {
            MakeProjectRoot(outer);
            MakeProjectRoot(inner);
            Directory.CreateDirectory(output);

            Assert.Equal(outer, ProjectFileSystem.ResolveProjectRootFrom(output));
        }
        finally
        {
            try { Directory.Delete(outer, true); } catch { }
        }
    }
}
