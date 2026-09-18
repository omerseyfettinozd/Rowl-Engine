using System;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

// B5: adapter'daki 20 doğrudan NativeBridge çağrısının worker-dispatch'a
// göçü (K-trio). Kırmızı-başlangıç: bu testler göçten ÖNCE fail-closed
// kırmızısı verir (değer yazılmaz, default okunur); trio taşındıkça yeşile
// döner. Okuma worker üzerinden yapılır (test-thread bridge-direkt okuyamaz
// — B4 canlı-dersi).
public sealed class EditorBridgeWorkerMigrationSliceTests
{
    private static EngineHost CreateLiveHost()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        var host = new EngineHost();
        if (!host.Initialize(64, 64, false))
            throw new Exception("B5: failed to init offscreen host");
        if (host.Runtime == null)
            throw new Exception("B5: worker access missing");
        return host;
    }

    private static float ReadAmbience(EngineHost host) =>
        host.Runtime!.Invoke(NativeBridge.RowlEngine_GetAmbienceVolume);

    [Fact]
    public void B5_Mixer_AmbienceVolume_ReachesNative()
    {
        using var host = CreateLiveHost();
        var adapter = new EngineHostPlayerAdapter(host);
        adapter.SetAmbienceVolume(0.5f);
        if (ReadAmbience(host) != 0.5f)
            throw new Exception("B5: ambience write did not reach native (fail-closed?)");
    }

    [Fact]
    public void B5_Character_UnknownPresetError_ReachesNative()
    {
        // Kayıtlı-olmayan preset native'de lastError yazar ("unknown preset",
        // c_api_character_layers.cpp:292); göçten önce fail-closed-boştu.
        using var host = CreateLiveHost();
        var adapter = new EngineHostPlayerAdapter(host);
        adapter.ApplyCharacterExpression("b5missing");
        string error = adapter.GetLastCharacterError();
        if (!error.Contains("unknown preset"))
            throw new Exception($"B5: character write did not reach native: [{error}]");
    }

    [Fact]
    public void B5_Chapter_IndexLoad_ReachesNative()
    {
        // Göçten önce fail-closed → string.Empty; göç-sonrası native boş
        // index'i yazar, GetLoadedChaptersJson dolu döner.
        using var host = CreateLiveHost();
        var adapter = new EngineHostPlayerAdapter(host);
        adapter.LoadChapterIndexJson("{\"chapters\":[]}");
        string loaded = adapter.GetLoadedChaptersJson();
        if (string.IsNullOrEmpty(loaded))
            throw new Exception("B5: chapter write did not reach native (fail-closed?)");
        if (adapter.PumpPrefetch(4.0f) < 0)
            throw new Exception("B5: pump returned negative");
    }
}
