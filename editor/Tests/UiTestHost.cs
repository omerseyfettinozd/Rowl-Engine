using System;
using System.Runtime.CompilerServices;
using Avalonia.Threading;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Ortak test temeli: <see cref="MainWindowViewModel"/> (ve Dispatcher zamanlayıcısı yaratan her
/// VM) yalnızca headless UI iş parçacığında kurulur / atılır.
/// <para>
/// Neden: <c>Dispatcher.UIThread</c>'e süreçte İLK dokunan iş parçacığı Avalonia'nın UI iş
/// parçacığı olur. VM'i headless çalışma zamanı yokken bir xUnit işçisinde kurmak bu rolü o
/// işçiye verir; sonra koşan headless UI testleri "Visual was invalidated during the render pass"
/// / "Source array was not long enough" ile düşer (sıra bağımlı kirlilik). Önce çalışma zamanı,
/// sonra UI iş parçacığında kurulum bunu kapatır. Bu sınıf dışında teste
/// <c>new MainWindowViewModel(</c> yazılmaz; <c>UiTestHostGuardTests</c> bunu kilitler.
/// </para>
/// </summary>
internal static class UiTestHost
{
    /// <summary>
    /// Kökten kapı: headless çalışma zamanı, test derlemesindeki HİÇBİR test kodu çalışmadan önce
    /// başlar. VM kurmayan testler de (ör. <c>NodeViewModel</c> / <c>ConnectionViewModel</c> üreten
    /// servis testleri) <c>Dispatcher.UIThread</c>'e dokunabildiği için "VM'i UI'da kur" tek başına
    /// yetmez; ilk dokunan iş parçacığı artık her zaman headless UI iş parçacığıdır.
    /// </summary>
    [ModuleInitializer]
    internal static void BootstrapHeadlessRuntime() => EnsureRuntime();

    /// <summary>Headless çalışma zamanını (ve UI iş parçacığını) yoksa başlatır; tekrar çağrılabilir.</summary>
    internal static void EnsureRuntime() => RowlEyeHeadlessRuntime.EnsureInitialized();

    /// <summary>İşi headless UI iş parçacığında çalıştırır (önce çalışma zamanını sağlar).</summary>
    internal static void Run(Action action)
    {
        EnsureRuntime();
        Dispatcher.UIThread.Invoke(action);
    }

    /// <summary>İşi headless UI iş parçacığında çalıştırıp sonucu döndürür.</summary>
    internal static T Run<T>(Func<T> func)
    {
        EnsureRuntime();
        return Dispatcher.UIThread.Invoke(func);
    }

    /// <summary>Motora bağlanmayan VM'i UI iş parçacığında kurar.</summary>
    internal static MainWindowViewModel CreateViewModel(string projectRoot)
        => Run(() => new MainWindowViewModel(projectRoot, connectEngine: false));

    /// <summary>VM'i UI iş parçacığında atar; atma hataları test temizliğini bozmaz.</summary>
    internal static void DisposeViewModel(MainWindowViewModel? viewModel)
    {
        if (viewModel == null) return;
        try { Run(viewModel.Dispose); } catch (Exception) { }
    }
}
