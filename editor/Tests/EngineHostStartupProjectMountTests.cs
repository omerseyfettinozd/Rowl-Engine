using System.IO;
using System.Runtime.InteropServices;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Oyun panelinde resimler çıkmıyordu: editör repo kökünden açılınca Init
/// cwd'deki örnek hikâyeyi yüklüyor, önizleme proje bağlanmadan önce sahne
/// itiyor (ses bileşeni stepId'yi 2 yapıyor), motor oturumu "oynanmış"
/// sayıp SetProjectDirectory'yi reddediyor ve ret sessizce yutuluyordu —
/// VFS boş kalıyor, hiçbir doku bulunamıyordu. Kilit: proje Init ile aynı
/// turda bağlanır; geç bağlama reddedilirse ret görünür.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EngineHostStartupProjectMountTests
{
    private const string AudioScene = """
        [ { "type": "audio", "enabled": true, "data": { "dsp_filter": "Normal" } } ]
        """;

    [Fact]
    public void PreviewPushBeforeMount_LateMountRejection_IsVisible()
    {
        using var fixture = new MountFixture();
        using var host = new EngineHost();
        Assert.True(host.Initialize(64, 64, false), host.LastError);

        // Eski sıra: önizleme bağlamadan önce sahne itiyor.
        host.UpdateSceneFromComponents(AudioScene);
        Assert.True(host.GetCurrentStepId() > 1, "ön koşul: itiş oturumu ilerletmeli");

        host.SetProjectDirectory(fixture.ProjectRoot);

        Assert.Equal(string.Empty, host.MountedProjectRoot);
        Assert.False(string.IsNullOrWhiteSpace(host.LastProjectMountError));
        Assert.False(ProbeExists(host, "images/probe.png"));
    }

    [Fact]
    public void StartupProjectRoot_IsMountedInsideInitialize_BeforeAnyPreviewPush()
    {
        using var fixture = new MountFixture();
        using var host = new EngineHost { StartupProjectRoot = fixture.ProjectRoot };
        Assert.True(host.Initialize(64, 64, false), host.LastError);

        // Önizleme itişi artık bağlamadan sonra gelir ve onu bozamaz.
        host.UpdateSceneFromComponents(AudioScene);

        Assert.Equal(fixture.ProjectRoot, host.MountedProjectRoot);
        Assert.Equal(string.Empty, host.LastProjectMountError);
        Assert.True(ProbeExists(host, "images/probe.png"));
    }

    private static bool ProbeExists(EngineHost host, string path)
    {
        string json = host.InvokeOnEngineThread(handle =>
        {
            int size = NativeBridge.RowlEngine_QueryVfsStatusJson(handle, path, IntPtr.Zero, 0);
            int cap = Math.Abs(size) + 1;
            IntPtr buffer = Marshal.AllocHGlobal(cap);
            try
            {
                int written = NativeBridge.RowlEngine_QueryVfsStatusJson(handle, path, buffer, cap);
                return written > 0 ? Marshal.PtrToStringUTF8(buffer, written) ?? string.Empty : string.Empty;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }, string.Empty);
        using var doc = System.Text.Json.JsonDocument.Parse(json);
        return doc.RootElement.GetProperty("query").GetProperty("exists").GetBoolean();
    }

    /// <summary>
    /// cwd'de örnek hikâye (repo kökünden başlatma), ayrıca resimli bir proje.
    /// </summary>
    private sealed class MountFixture : IDisposable
    {
        private readonly string _savedCwd = Directory.GetCurrentDirectory();
        private readonly string _root = Path.Combine(Path.GetTempPath(), "rowl_startup_mount_" + Guid.NewGuid().ToString("N"));

        internal string ProjectRoot { get; }

        internal MountFixture()
        {
            NativeEnvironment.EnsureDisplayFreeDrivers();
            string cwd = Path.Combine(_root, "cwd");
            Directory.CreateDirectory(Path.Combine(cwd, "Assets", "json"));
            string repoRoot = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
            File.Copy(Path.Combine(repoRoot, "Assets", "json", "full_story_graph.json"),
                      Path.Combine(cwd, "Assets", "json", "full_story_graph.json"));

            ProjectRoot = Path.Combine(_root, "project");
            Directory.CreateDirectory(Path.Combine(ProjectRoot, "Assets", "images"));
            File.WriteAllBytes(Path.Combine(ProjectRoot, "Assets", "images", "probe.png"), new byte[] { 0x89, 0x50, 0x4E, 0x47 });

            Directory.SetCurrentDirectory(cwd);
        }

        public void Dispose()
        {
            Directory.SetCurrentDirectory(_savedCwd);
            Directory.Delete(_root, recursive: true);
        }
    }
}
