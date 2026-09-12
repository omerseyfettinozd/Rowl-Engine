using Avalonia;
using System;
using System.Collections.Generic;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorViewModelThinningTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 28]: ViewModel Thinning Equivalence (Rotation, Selection-Rect, Diagnostics, Telemetry Routing)...");

        Console.WriteLine("    [Step 28.1]: Selection-rect normalization...");
        var forward = EditorSelectionCoordinator.ComputeSelectionRect(new Point(10, 20), new Point(110, 120));
        if (forward.X != 10 || forward.Y != 20 || forward.Width != 100 || forward.Height != 100)
            throw new Exception($"Forward rect mismatch: {forward}");
        var reverse = EditorSelectionCoordinator.ComputeSelectionRect(new Point(110, 120), new Point(10, 20));
        if (reverse.X != 10 || reverse.Y != 20 || reverse.Width != 100 || reverse.Height != 100)
            throw new Exception($"Reverse rect mismatch: {reverse}");

        Console.WriteLine("    [Step 28.2]: Scene rotation reset service...");
        EditorLayoutAssistService.ResetSceneRotation(null);
        var rotationNode = new NodeViewModel(2801, "Rotation Service Node", 0, 0, bare: false);
        rotationNode.BackgroundRotation = 90.0;
        rotationNode.CharacterRotation = 270.0;
        EditorLayoutAssistService.ResetSceneRotation(rotationNode);
        if (Math.Abs(rotationNode.BackgroundRotation) > 0.001 || Math.Abs(rotationNode.CharacterRotation) > 0.001)
            throw new Exception("ResetSceneRotation failed to zero bg/char rotations");

        Console.WriteLine("    [Step 28.3]: MainWindowViewModel.ResetRotation equivalence...");
        mainVm.SelectNodeQuiet(rotationNode);
        rotationNode.BackgroundRotation = 45.0;
        rotationNode.CharacterRotation = 135.0;
        mainVm.ResetRotation();
        if (Math.Abs(rotationNode.BackgroundRotation) > 0.001 || Math.Abs(rotationNode.CharacterRotation) > 0.001)
            throw new Exception("MainWindowViewModel.ResetRotation lost behavior after thinning");

        Console.WriteLine("    [Step 28.4]: Engine diagnostic routing...");
        var diagnostics = new EditorNotificationService();
        var diagnosticLogs = new List<string>();
        diagnostics.ReportEngineDiagnostic(RuntimeErrorCode.Ok, "op", "msg", "tgt", diagnosticLogs.Add);
        if (diagnostics.Notifications.Count != 0 || diagnosticLogs.Count != 0)
            throw new Exception("Ok diagnostic must be a no-op");
        diagnostics.ReportEngineDiagnostic(RuntimeErrorCode.FileNotFound, "LoadGraph", "", "graph.json", diagnosticLogs.Add);
        if (diagnostics.Notifications.Count != 1 || diagnostics.Notifications[0].Type != NotificationType.Warning)
            throw new Exception("FileNotFound must route to a warning notification");
        if (diagnosticLogs.Count != 1 || !diagnosticLogs[0].Contains("LoadGraph"))
            throw new Exception("Diagnostic log format mismatch");
        diagnostics.ReportEngineDiagnostic(RuntimeErrorCode.ParseError, "Parse", "bad json", "story.json", diagnosticLogs.Add);
        if (diagnostics.Notifications.Count != 2 || diagnostics.Notifications[1].Type != NotificationType.Error)
            throw new Exception("ParseError must route to an error notification");

        Console.WriteLine("    [Step 28.5]: Audio-device transition edge-triggering...");
        var audioNotifications = new EditorNotificationService();
        var audioLogs = new List<string>();
        if (!audioNotifications.ReportAudioDeviceTransition(false, audioLogs.Add))
            throw new Exception("Loss transition must report true");
        if (audioNotifications.Notifications.Count != 1 || audioNotifications.Notifications[0].Type != NotificationType.Warning)
            throw new Exception("Loss transition must toast a warning");
        if (audioNotifications.ReportAudioDeviceTransition(false, audioLogs.Add))
            throw new Exception("Repeated loss poll must report false");
        if (audioNotifications.Notifications.Count != 1)
            throw new Exception("Repeated loss poll must stay silent");
        if (!audioNotifications.ReportAudioDeviceTransition(true, audioLogs.Add))
            throw new Exception("Recovery transition must report true");
        if (audioNotifications.Notifications.Count != 2 || audioNotifications.Notifications[1].Type != NotificationType.Success)
            throw new Exception("Recovery transition must toast a success");

        Console.WriteLine("    [Step 28.6]: Audio telemetry routing...");
        float PeakBy(int channel, int index) => channel * 10 + index + 0.5f;
        float RmsBy(int channel, int index) => channel * 10 + index + 0.25f;
        float peakLeft = -1, peakRight = -1, rmsLeft = -1, rmsRight = -1;
        var routedAudio = new AudioComponentViewModel();
        EditorSceneSyncService.RouteAudioTelemetry(
            0.7f, 0.8f, 0.5f, 0.6f,
            PeakBy, RmsBy,
            (pLeft, pRight, rLeft, rRight) =>
            {
                peakLeft = pLeft;
                peakRight = pRight;
                rmsLeft = rLeft;
                rmsRight = rRight;
            },
            routedAudio);
        if (Math.Abs(peakLeft - 0.7f) > 0.001 || Math.Abs(peakRight - 0.8f) > 0.001 ||
            Math.Abs(rmsLeft - 0.5f) > 0.001 || Math.Abs(rmsRight - 0.6f) > 0.001)
            throw new Exception("Master sample must reach the preview updater unchanged");
        if (Math.Abs(routedAudio.BgmPeakL - 0.5f) > 0.001 || Math.Abs(routedAudio.BgmPeakR - 1.5f) > 0.001 ||
            Math.Abs(routedAudio.BgmRmsL - 0.25f) > 0.001 || Math.Abs(routedAudio.BgmRmsR - 1.25f) > 0.001)
            throw new Exception("BGM channel mapping mismatch");
        if (Math.Abs(routedAudio.SfxPeakL - 20.5f) > 0.001 || Math.Abs(routedAudio.SfxPeakR - 21.5f) > 0.001 ||
            Math.Abs(routedAudio.SfxRmsL - 20.25f) > 0.001 || Math.Abs(routedAudio.SfxRmsR - 21.25f) > 0.001)
            throw new Exception("SFX channel mapping mismatch");

        Console.WriteLine("    [Step 28.7]: Null-tolerant routing...");
        EditorSceneSyncService.RouteAudioTelemetry(0, 0, 0, 0, PeakBy, RmsBy, null, null);
        EditorSceneSyncService.RouteAudioTelemetry(0, 0, 0, 0, null, null, null, routedAudio);

        Console.WriteLine("  ✅ [PASS] ViewModel thinning equivalence verified");
    }
}
