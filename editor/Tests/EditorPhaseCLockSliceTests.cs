using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

// C0: Faz C strangler'ının kilit-dilimi — SAF TEST, üretim-koduna sıfır
// dokunuş. (1) EditorDialogService headless fail-closed kilidi (pencere
// yoksa null, throw yok); (2) LivePreviewViewModel telemetri-matematiğinin
// characterization-kilidi (C2 bölünmesi bu kilide dayanır — kilitsiz
// bölünme YASAK). Her iki sınıf da UI-thread gerektirmez.
public sealed class EditorPhaseCLockSliceTests
{
    [Fact]
    public async Task C0_DialogService_HeadlessNullFailClosed()
    {
        if (EditorDialogService.GetMainWindow() != null)
            throw new Exception("C0: headless suite has no main window");
        if (await EditorDialogService.PickAssetFilesAsync() != null)
            throw new Exception("C0: asset picker must be null without window");
        if (await EditorDialogService.PickFolderAsync("C0 probe") != null)
            throw new Exception("C0: folder picker must be null without window");
    }

    [Fact]
    public void C0_LivePreview_TelemetryCharacterization()
    {
        var preview = new LivePreviewViewModel(null!);
        preview.UpdateAudioTelemetry(0.5f, 0.25f, 0.1f, 0.05f);
        if (preview.MasterPeakL != 0.5f || preview.MasterPeakR != 0.25f ||
            preview.MasterRmsL != 0.1f || preview.MasterRmsR != 0.05f)
            throw new Exception("C0: telemetry assignment changed");
        if (!preview.IsAudioActive)
            throw new Exception("C0: active signal must flag active");

        preview.UpdateAudioTelemetry(0.0f, 0.0f, 0.0f, 0.0f);
        if (preview.IsAudioActive)
            throw new Exception("C0: silence must flag inactive");

        // Eşik `>` 0.005f'dir (eşitlik aktif DEĞİLDİR) — C2'de korunacak sınır.
        preview.UpdateAudioTelemetry(0.005f, 0.005f, 0.0f, 0.0f);
        if (preview.IsAudioActive)
            throw new Exception("C0: threshold equality must stay inactive");
        preview.UpdateAudioTelemetry(0.0051f, 0.0f, 0.0f, 0.0f);
        if (!preview.IsAudioActive)
            throw new Exception("C0: above-threshold must flag active");
        preview.UpdateAudioTelemetry(-1.0f, -1.0f, 0.0f, 0.0f);
        if (preview.IsAudioActive)
            throw new Exception("C0: negative peaks must flag inactive");
    }
}
