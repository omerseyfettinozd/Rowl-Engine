using System;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorNotificationTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 21]: EditorNotificationService & Runtime Diagnostic Toasts...");

        // Step 21.1: Notification creation, severity, accent colors and icon symbols
        Console.WriteLine("    [Step 21.1]: Notification Models & Severity...");
        var notifService = new EditorNotificationService();
        var info = notifService.ShowInfo("System initialized");
        var success = notifService.ShowSuccess("Project saved");
        var warning = notifService.ShowWarning("Disk slot nearly full");
        var error = notifService.ShowError("Compilation failed");

        if (info.Type != NotificationType.Info || info.IconSymbol != "ℹ️" || info.AccentColor != "#3B82F6")
            throw new Exception("NotificationType.Info properties mismatch");
        if (success.Type != NotificationType.Success || success.IconSymbol != "✅" || success.AccentColor != "#10B981")
            throw new Exception("NotificationType.Success properties mismatch");
        if (warning.Type != NotificationType.Warning || warning.IconSymbol != "⚠️" || warning.AccentColor != "#F59E0B")
            throw new Exception("NotificationType.Warning properties mismatch");
        if (error.Type != NotificationType.Error || error.IconSymbol != "❌" || error.AccentColor != "#EF4444")
            throw new Exception("NotificationType.Error properties mismatch");

        // Step 21.2: Queue capping
        Console.WriteLine("    [Step 21.2]: Notification Queue Capping (Max 5)...");
        notifService.ShowInfo("Item 5");
        notifService.ShowInfo("Item 6");
        if (notifService.Notifications.Count > 5)
            throw new Exception($"Notification queue exceeded limit of 5: count={notifService.Notifications.Count}");

        // Step 21.3: Dismissal
        Console.WriteLine("    [Step 21.3]: Dismissal & ClearAll...");
        notifService.Dismiss(error);
        if (notifService.Notifications.Contains(error))
            throw new Exception("Dismiss failed to remove notification.");

        notifService.ClearAll();
        if (notifService.Notifications.Count != 0)
            throw new Exception("ClearAll failed to empty notifications.");

        // Step 21.4: CheckEngineDiagnostics Integration
        Console.WriteLine("    [Step 21.4]: MainWindowViewModel CheckEngineDiagnostics...");
        mainVm.CheckEngineDiagnostics(); // In test environment without native handle, should be graceful no-op

        Console.WriteLine("  ✅ [PASS] EditorNotificationService & Diagnostics Integration verified");
    }
}
