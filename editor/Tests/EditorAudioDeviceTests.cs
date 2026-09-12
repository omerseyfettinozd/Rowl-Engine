using System;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorAudioDeviceTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 27]: Audio Device Status Observer & Edge-Triggered Toasts...");
        mainVm.NotificationService.ClearAll();

        Console.WriteLine("    [Step 27.1]: Device-loss transition...");
        mainVm.CheckAudioDeviceStatus(false);
        if (mainVm.NotificationService.Notifications.Count != 1)
            throw new Exception($"Device loss should toast exactly once, count={mainVm.NotificationService.Notifications.Count}");
        if (mainVm.NotificationService.Notifications[0].Type != NotificationType.Warning)
            throw new Exception("Device-loss toast must be a warning");

        Console.WriteLine("    [Step 27.2]: Repeated loss polls stay silent...");
        mainVm.CheckAudioDeviceStatus(false);
        mainVm.CheckAudioDeviceStatus(false);
        if (mainVm.NotificationService.Notifications.Count != 1)
            throw new Exception($"Repeated loss polls must not re-toast, count={mainVm.NotificationService.Notifications.Count}");

        Console.WriteLine("    [Step 27.3]: Device-recovery transition...");
        mainVm.CheckAudioDeviceStatus(true);
        if (mainVm.NotificationService.Notifications.Count != 2)
            throw new Exception($"Device recovery should toast exactly once, count={mainVm.NotificationService.Notifications.Count}");
        if (mainVm.NotificationService.Notifications[1].Type != NotificationType.Success)
            throw new Exception("Device-recovery toast must be a success");

        Console.WriteLine("    [Step 27.4]: Steady polls stay silent...");
        mainVm.CheckAudioDeviceStatus(true);
        if (mainVm.NotificationService.Notifications.Count != 2)
            throw new Exception("Steady available polls must not toast");

        Console.WriteLine("    [Step 27.5]: Native audio observer P/Invoke...");
        if (!mainVm.EngineHost.IsInitialized)
            throw new Exception("EngineHost must be initialized before observer check");
        bool suspendedOnce = mainVm.EngineHost.IsAudioOutputSuspended;
        bool suspendedTwice = mainVm.EngineHost.IsAudioOutputSuspended;
        bool deviceOnce = mainVm.EngineHost.IsAudioDeviceAvailable;
        bool deviceTwice = mainVm.EngineHost.IsAudioDeviceAvailable;
        if (suspendedOnce || suspendedTwice)
            throw new Exception("Fresh runtime must not report suspended output");
        if (deviceOnce != deviceTwice)
            throw new Exception("Device observer must report stable values across polls");

        Console.WriteLine("    [Step 27.6]: Save-slot failure diagnostics path...");
        mainVm.SaveSlotsViewModel.SaveCommand.Execute(new SaveSlotEntry(999, false));
        if (!mainVm.LogOutput.Contains("kaydedilemedi"))
            throw new Exception("Save failure did not log the expected message");

        Console.WriteLine("  ✅ [PASS] Audio device status observer & edge-triggered toasts verified");
    }
}
