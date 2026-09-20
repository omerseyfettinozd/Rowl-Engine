using System.ComponentModel;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Kod-inceleme bulgusu #9 — SetPlayState init-sonrası guard sıralaması.
/// Init edilmemiş hostta SetPlayState(true) bayrağı çevirmemeli ve
/// IsPlaying bildirimi göndermemelidir (yerel taraf zaten fail-closed).
/// </summary>
public sealed class EditorPlayStateGuardTests
{
    [Fact]
    public void Uninitialized_SetPlayStateTrue_KeepsIsPlayingFalse()
    {
        using var host = new EngineHost();
        Assert.False(host.IsInitialized);

        host.SetPlayState(true);

        Assert.False(host.IsPlaying);
    }

    [Fact]
    public void Uninitialized_SetPlayStateTrue_RaisesNoNotification()
    {
        using var host = new EngineHost();
        int isPlayingNotifications = 0;
        host.PropertyChanged += OnPropertyChanged;

        host.SetPlayState(true);

        Assert.Equal(0, isPlayingNotifications);

        void OnPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EngineHost.IsPlaying))
                isPlayingNotifications++;
        }
    }
}
