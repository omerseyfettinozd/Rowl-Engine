using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Chaple (2026-10-22): editörde Duraklat oyunun Esc duraklatma menüsünü açıyordu (native
/// SetPaused = oyuncu menüsü); Durdur'dan sonra müzik arka planda çalmaya devam ediyordu.
/// Kilit: Duraklat Unity gibi oyunu ve sesi menüsüz dondurur; Durdur müziği keser.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorPlayPauseStopTests
{
    [Fact]
    public void EditorFreeze_DoesNotOpenGameMenu_AndMutesUntilResumed()
    {
        using var fixture = new AudioProjectFixture();
        using var host = new EngineHost { StartupProjectRoot = fixture.ProjectRoot };
        Assert.True(host.Initialize(64, 64, false), host.LastError);
        host.SetMasterVolume(0.6f);
        host.SetPlayState(true);

        host.SetEditorFrozen(true);
        Assert.True(host.IsEditorFrozen);
        Assert.False(host.IsPaused(), "editör Duraklat'ı oyunun duraklatma menüsünü açmamalı");
        Assert.Equal(0f, host.GetMasterVolume());

        // Donukken gelen ses ayarı sessizliği bozmaz, çözülünce uygulanır.
        host.SetMasterVolume(0.3f);
        Assert.Equal(0f, host.GetMasterVolume());

        host.SetEditorFrozen(false);
        Assert.False(host.IsEditorFrozen);
        Assert.Equal(0.3f, host.GetMasterVolume(), 3);

        // Donukken Durdur: dondurma kalkar, ses geri gelir.
        host.SetEditorFrozen(true);
        host.SetPlayState(false);
        Assert.False(host.IsEditorFrozen);
        Assert.Equal(0.3f, host.GetMasterVolume(), 3);
    }

    [Fact]
    public void StopPlayMode_StopsBackgroundMusic()
    {
        using var fixture = new AudioProjectFixture();
        using var host = new EngineHost { StartupProjectRoot = fixture.ProjectRoot };
        Assert.True(host.Initialize(64, 64, false), host.LastError);
        host.SetPlayState(true);
        host.PlayAudio("audio/tone.wav", channelType: 0);
        host.Step(1f / 60f);
        Assert.True(host.IsBgmPlaying, "ön koşul: müzik çalmalı — " + host.LastAudioError);

        EditorPlayModeCoordinator.StopPlayMode(host, () => null, _ => { }, _ => { }, _ => { });

        Assert.False(host.IsPlaying);
        Assert.False(host.IsBgmPlaying, "Durdur'dan sonra müzik arka planda çalmamalı");
    }

    [Fact]
    public void EditModePreview_DoesNotStartMusic_PlayModeDoes()
    {
        const string scene = """
            [ { "type": "audio", "enabled": true, "data": { "bgm_track": "audio/tone.wav", "volume": 0.7 } } ]
            """;
        using var fixture = new AudioProjectFixture();
        using var host = new EngineHost { StartupProjectRoot = fixture.ProjectRoot };
        Assert.True(host.Initialize(64, 64, false), host.LastError);

        host.UpdateSceneFromComponents(scene);
        Assert.False(host.IsBgmPlaying, "düzenleme önizlemesi müziği başlatmamalı");

        host.SetPlayState(true);
        host.UpdateSceneFromComponents(scene);
        host.Step(1f / 60f);
        Assert.True(host.IsBgmPlaying, "oyun modunda sahnenin müziği çalmalı — " + host.LastAudioError);

        host.SetPlayState(false);
        host.StopBgm();
        host.UpdateSceneFromComponents(scene); // Durdur sonrası ilk kare yeniden itilir
        Assert.False(host.IsBgmPlaying, "Durdur'dan sonra ilk kare müziği yeniden başlatmamalı");
    }

    [Fact]
    public void RealEditorFlow_MusicOnlyPlaysInPlayMode()
    {
        using var fixture = new AudioProjectFixture();
        using var host = new EngineHost { StartupProjectRoot = fixture.ProjectRoot };
        Assert.True(host.Initialize(64, 64, false), host.LastError);

        // Proje açılışı: hikâye yüklenir, önizleme ilk kareyi başlangıca sıfırlayarak çizer.
        host.LoadStoryGraph(fixture.GraphPath);
        host.ResetToStartNode();
        host.Step(1f / 60f);
        Assert.False(host.IsBgmPlaying, "proje açılınca (oynamadan) müzik çalmamalı");

        // Oynat (EditorPlayModeCoordinator sırası): oyun modu → başlangıca sıfırla.
        host.SetPlayState(true);
        host.ResetToStartNode();
        host.Step(1f / 60f);
        Assert.True(host.IsBgmPlaying, "oyun modunda başlangıç müziği çalmalı — " + host.LastAudioError);

        // Durdur → önizleme ilk kareyi yeniden çizer.
        EditorPlayModeCoordinator.StopPlayMode(host, () => null, _ => { }, _ => { }, _ => { });
        host.ResetToStartNode();
        Assert.False(host.IsBgmPlaying, "Durdur'dan sonra müzik arka planda çalmamalı");

        // Denetçi'deki elle ses önizlemesi düzenleme susturmasından muaf.
        host.PlayAudio("audio/tone.wav", channelType: 0);
        host.ResetToStartNode();
        Assert.True(host.IsBgmPlaying, "elle başlatılan ses önizlemesi susturulmamalı");
    }

    private sealed class AudioProjectFixture : IDisposable
    {
        private readonly string _root = Path.Combine(Path.GetTempPath(), "rowl_play_pause_" + Guid.NewGuid().ToString("N"));

        internal string ProjectRoot { get; }
        internal string GraphPath { get; }

        internal AudioProjectFixture()
        {
            NativeEnvironment.EnsureDisplayFreeDrivers();
            ProjectRoot = Path.Combine(_root, "project");
            Directory.CreateDirectory(Path.Combine(ProjectRoot, "Assets", "audio"));
            Directory.CreateDirectory(Path.Combine(ProjectRoot, "Assets", "json"));
            WriteTone(Path.Combine(ProjectRoot, "Assets", "audio", "tone.wav"), seconds: 2);
            // Başlangıç düğümünde müzik bileşeni olan tek düğümlük hikâye (gerçek projelerdeki gibi).
            GraphPath = Path.Combine(ProjectRoot, "Assets", "json", "full_story_graph.json");
            File.WriteAllText(GraphPath, """
                { "format_version": 4, "start_node_id": 1, "nodes": [ { "id": 1, "title": "A", "objects": [
                  { "id": "o", "name": "Ses", "is_active": true, "components": [
                    { "type": "audio", "id": "a", "enabled": true, "data": { "bgm_track": "audio/tone.wav", "volume": 0.7 } },
                    { "type": "dialogue", "id": "d", "enabled": true, "data": { "speaker": "S", "dialogue": "Merhaba" } } ] } ] } ] }
                """);
        }

        private static void WriteTone(string path, int seconds)
        {
            const int rate = 22050;
            int samples = rate * seconds;
            using var w = new BinaryWriter(File.Create(path));
            w.Write("RIFF"u8.ToArray()); w.Write(36 + samples * 2); w.Write("WAVE"u8.ToArray());
            w.Write("fmt "u8.ToArray()); w.Write(16); w.Write((short)1); w.Write((short)1);
            w.Write(rate); w.Write(rate * 2); w.Write((short)2); w.Write((short)16);
            w.Write("data"u8.ToArray()); w.Write(samples * 2);
            for (int i = 0; i < samples; i++)
                w.Write((short)(Math.Sin(2 * Math.PI * 440 * i / rate) * 6000));
        }

        public void Dispose()
        {
            try { Directory.Delete(_root, recursive: true); } catch (IOException) { }
        }
    }
}
