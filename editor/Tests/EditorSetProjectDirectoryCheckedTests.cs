using System.IO;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Kod-inceleme bulgusu #5 — ölü proje kökü mount reddi
/// <c>load_story_graph</c> op'uyla damgalandığı için op-filtreli
/// predicate ıskalayıp mount başarılı raporluyordu (VFS bomboşken).
/// Artık sıfır-dışı her kod reddir ve detay Step öncesi yakalanır.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorSetProjectDirectoryCheckedTests
{
    [Fact]
    public void DeadRoot_MountRejectedWithDetail()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        if (!host.Initialize(64, 64, false))
            throw new Exception("D5: failed to init offscreen host");

        string savedCwd = Directory.GetCurrentDirectory();
        string emptyRoot = Path.Combine(Path.GetTempPath(), "rowl_cwd_" + Guid.NewGuid().ToString("N"));
        string deadRoot = Path.Combine(Path.GetTempPath(), "rowl_deadroot_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(emptyRoot);
        try
        {
            // Süreç-CWD fiziksel story fallback'i reddi maskelemesin.
            Directory.SetCurrentDirectory(emptyRoot);
            Assert.False(Directory.Exists(deadRoot));

            bool ok = host.SetProjectDirectoryChecked(deadRoot, out string detail);

            Assert.False(ok);
            Assert.False(string.IsNullOrWhiteSpace(detail));
        }
        finally
        {
            Directory.SetCurrentDirectory(savedCwd);
            Directory.Delete(emptyRoot, recursive: true);
        }
    }
}
