using Avalonia.Threading;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Motor nesnesi yalnız sahibi olan çalışan iş parçacığından çağrılabilir; native taraf başka
/// iş parçacığından gelen çağrıyı WRONG_THREAD ile reddeder. Denetçi'nin kaynak (provenance)
/// sorgusu ve Oynat'taki bölüm beslemesi UI iş parçacığından doğrudan çağırıyordu: her düğüm
/// seçiminde günlüğe hata düşüyor, bölümler motora hiç beslenmiyordu.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorEngineThreadAffinityTests
{
    [Fact]
    public void NativeGuard_RejectsForeignThread_ControlCase()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        Assert.True(host.Initialize(64, 64, false));
        // Testin iş parçacığı motorun sahibi değil: doğrudan çağrı reddedilmeli (testin anlamlı olduğunun kanıtı).
        var rc = NativeBridge.RowlEngine_GetAssetProvenanceJson(host.Handle, "images/none.png", IntPtr.Zero, 0, out _);
        Assert.Equal(NativeBridge.ResultCode.WrongThread, rc);
    }

    [Fact]
    public void InspectorProvenance_RunsOnEngineThread_NotRejected()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlAffinity_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        string root = ProjectFactory.CreateNewProject("Affinity", parent).Info!.Path;
        var vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(root, connectEngine: false));
        try
        {
            Assert.True(vm.EngineHost.Initialize(64, 64, false));
            var (call, handle) = vm.InspectorViewModel.ActiveProvenanceEndpoint();
            Assert.NotNull(call);
            var rc = call!(handle, "images/none.png", IntPtr.Zero, 0, out _);
            Assert.NotEqual(NativeBridge.ResultCode.WrongThread, rc);
        }
        finally
        {
            Dispatcher.UIThread.Invoke(() => vm.Dispose());
            try { Directory.Delete(parent, true); } catch (Exception) { }
            if (string.Equals(MainWindowViewModel.ProjectRoot, root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = previousRoot;
        }
    }

    [Fact]
    public void PlayChapterFeed_RunsOnEngineThread_NoWrongThreadRejection()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        string dir = Path.Combine(Path.GetTempPath(), "RowlFeed_" + Guid.NewGuid().ToString("N"));
        string assetsJson = Path.Combine(dir, "json");
        Directory.CreateDirectory(Path.Combine(assetsJson, "chapters"));
        try
        {
            File.WriteAllText(Path.Combine(assetsJson, "chapters", "chapter_index.json"),
                "{\"format_version\":5,\"chapters\":[{\"id\":\"ch1\"}]}");
            File.WriteAllText(Path.Combine(assetsJson, "chapters", "ch1.json"),
                "{\"chapter_id\":\"ch1\",\"nodes\":[]}");
            using var host = new EngineHost();
            Assert.True(host.Initialize(64, 64, false));
            var log = new List<string>();
            int accepted = PrefetchChapterFeedService.FeedFromAssetsDir(host, assetsJson, log.Add);
            Assert.DoesNotContain(log, line => line.Contains(nameof(NativeBridge.ResultCode.WrongThread), StringComparison.Ordinal));
            Assert.Equal(2, accepted); // index + bir bölüm dosyası, ikisi de motorca kabul
            // Motor bölümü gerçekten aldı mı: besleme reddedildiyse ch1 bilinmez ve yüklenemez.
            var load = host.InvokeOnEngineThread(engine => NativeBridge.RowlEngine_LoadChapter(engine, "ch1"), NativeBridge.ResultCode.InvalidHandle);
            Assert.Equal(NativeBridge.ResultCode.Ok, load);
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
        }
    }
}
