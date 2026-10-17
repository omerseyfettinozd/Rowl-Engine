using System.Runtime.CompilerServices;
using RowlEngine.Editor.Tools.RowlEye;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Avalonia <c>Dispatcher.UIThread</c>'i süreç başına bir kez, ona ilk dokunulduğu andaki platforma
/// bağlar. Bir test başsız çalışma zamanı kurulmadan dokunursa (ör. <see cref="RowlEyeHeadlessRuntime.EnsureInitialized"/>
/// çağrılmadan <c>Dispatcher.UIThread.Invoke(() =&gt; new MainWindowViewModel(...))</c>), dispatcher
/// <c>NullDispatcherImpl</c> ile kurulur; onun <c>CheckAccess()</c>'i her iş parçacığında true döner:
/// <c>Invoke</c> iş parçacığı değiştirmez ve <c>RowlEye-HeadlessUI</c> döngüsünün <c>RunJobs()</c>'u aynı
/// pencereler üzerinde test iş parçacığıyla eşzamanlı düzen geçişi koşar. Sonraki başsız UI testleri
/// sıraya bağlı düşer (öteki geçiş <c>_running</c> olduğu için düzen atlanır, bayat yerleşim okunur).
/// Başsız çalışma zamanını her test kodundan önce kurmak sırayı önemsiz kılar.
/// </summary>
internal static class HeadlessUiThreadInitializer
{
#pragma warning disable CA2255 // test derlemesi: süreç geneli UI iş parçacığı her testten önce var olmalı
    [ModuleInitializer]
    internal static void Initialize() => RowlEyeHeadlessRuntime.EnsureInitialized();
#pragma warning restore CA2255
}
