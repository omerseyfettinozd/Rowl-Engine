using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Service managing floating inline toast notifications across the editor.
    /// Provides auto-dismissal, severity levels, and thread-safe UI dispatching.
    /// </summary>
    public partial class EditorNotificationService : ObservableObject
    {
        public ObservableCollection<ToastNotification> Notifications { get; } = new();

        private readonly object _lock = new();

        public ToastNotification Show(
            string message,
            NotificationType type = NotificationType.Info,
            string? title = null,
            int autoDismissMs = 4000)
        {
            var notification = new ToastNotification(message, type, title);

            RunOnUIThread(() =>
            {
                lock (_lock)
                {
                    // Cap max visible notifications to 5 to avoid UI overcrowding
                    while (Notifications.Count >= 5)
                    {
                        Notifications.RemoveAt(0);
                    }
                    Notifications.Add(notification);
                }
            });

            if (autoDismissMs > 0)
            {
                _ = Task.Run(async () =>
                {
                    await Task.Delay(autoDismissMs).ConfigureAwait(false);
                    Dismiss(notification);
                });
            }

            return notification;
        }

        public ToastNotification ShowInfo(string message, string? title = null, int autoDismissMs = 4000)
            => Show(message, NotificationType.Info, title, autoDismissMs);

        public ToastNotification ShowSuccess(string message, string? title = null, int autoDismissMs = 4000)
            => Show(message, NotificationType.Success, title, autoDismissMs);

        public ToastNotification ShowWarning(string message, string? title = null, int autoDismissMs = 5000)
            => Show(message, NotificationType.Warning, title, autoDismissMs);

        public ToastNotification ShowError(string message, string? title = null, int autoDismissMs = 7000)
            => Show(message, NotificationType.Error, title, autoDismissMs);

        public void Dismiss(ToastNotification notification)
        {
            if (notification == null) return;

            RunOnUIThread(() =>
            {
                lock (_lock)
                {
                    notification.IsDismissed = true;
                    Notifications.Remove(notification);
                }
            });
        }

        public void ClearAll()
        {
            RunOnUIThread(() =>
            {
                lock (_lock)
                {
                    foreach (var n in Notifications) n.IsDismissed = true;
                    Notifications.Clear();
                }
            });
        }

        // Edge-trigger state for audio-device transitions: toast once per
        // change instead of on every diagnostics poll.
        private bool _lastAudioDeviceAvailable = true;

        /// <summary>
        /// Routes a native runtime diagnostic into toast + inline notification + log.
        /// No-op when <paramref name="code"/> is <see cref="RuntimeErrorCode.Ok"/>.
        /// </summary>
        public void ReportEngineDiagnostic(
            RuntimeErrorCode code,
            string operation,
            string message,
            string target,
            Action<string>? log = null)
        {
            if (code == RuntimeErrorCode.Ok) return;

            string msg = string.IsNullOrEmpty(message)
                ? $"Engine Diagnostic ({code}): {operation}"
                : $"[{code}] {message}";

            bool isWarning = code == RuntimeErrorCode.FileNotFound
                || code == RuntimeErrorCode.InvalidArgument;
            var toastType = isWarning ? ToastType.Warning : ToastType.Error;

            ToastService.Instance.Show(msg, toastType, 4000);
            Show(msg, isWarning ? NotificationType.Warning : NotificationType.Error, "Motor Uyarısı", 4000);
            log?.Invoke($"⚠️ [Motor Tanı] {code} — {operation}: {message} ({target})");
        }

        /// <summary>Publishes structured build/package failures to toast, notification list and log.</summary>
        public void ReportBuildDiagnostic(BuildDiagnostic diagnostic, Action<string>? log = null)
        {
            ArgumentNullException.ThrowIfNull(diagnostic);
            string detail = string.IsNullOrWhiteSpace(diagnostic.Detail)
                ? string.Empty
                : $" — {diagnostic.Detail}";
            string message = $"[{diagnostic.Code}] {diagnostic.Message}";
            string logLine = $"[Build Tanı] {diagnostic.Code} — {diagnostic.Operation}: {diagnostic.Message} " +
                $"(hedef: {diagnostic.Target}{(diagnostic.ExitCode.HasValue ? $", çıkış: {diagnostic.ExitCode}" : string.Empty)}){detail}";

            RunOnUIThread(() =>
            {
                switch (diagnostic.Severity)
                {
                    case BuildDiagnosticSeverity.Info:
                        ToastService.Instance.Show(message, ToastType.Info, 3500);
                        ShowInfo(message, "Build", 3500);
                        break;
                    case BuildDiagnosticSeverity.Warning:
                        ToastService.Instance.Show(message, ToastType.Warning, 5000);
                        ShowWarning(message, "Build Uyarısı", 5000);
                        break;
                    default:
                        ToastService.Instance.Show(message, ToastType.Error, 7000);
                        ShowError(message, "Build Hatası", 7000);
                        break;
                }
            });

            string prefix = diagnostic.Severity switch
            {
                BuildDiagnosticSeverity.Info => "ℹ️",
                BuildDiagnosticSeverity.Warning => "⚠️",
                _ => "❌"
            };
            log?.Invoke($"{prefix} {logLine}");
        }

        /// <summary>
        /// Edge-triggered audio-device observer: toasts only on transitions so a
        /// missing device does not spam on every diagnostics poll.
        /// Returns true when a transition was reported.
        /// </summary>
        public bool ReportAudioDeviceTransition(bool deviceAvailable, Action<string>? log = null)
        {
            if (deviceAvailable == _lastAudioDeviceAvailable) return false;
            _lastAudioDeviceAvailable = deviceAvailable;
            if (!deviceAvailable)
            {
                const string msg = "Ses cihazı kayboldu — sessiz moda geçildi, çalma niyeti korunuyor.";
                ToastService.Instance.Show(msg, ToastType.Warning, 4000);
                ShowWarning(msg, "Ses Cihazı");
                log?.Invoke("⚠️ [Ses] Cihaz kaybı algılandı; motor sessiz fallback + niyet korumasında.");
            }
            else
            {
                const string msg = "Ses cihazı geri geldi — çıkış yeniden açıldı.";
                ToastService.Instance.Show(msg, ToastType.Success, 4000);
                ShowSuccess(msg, "Ses Cihazı");
                log?.Invoke("✅ [Ses] Cihaz geri geldi; çıkış akışları yeniden kuruldu.");
            }
            return true;
        }

        private static void RunOnUIThread(Action action)
        {
            if (Dispatcher.UIThread.CheckAccess())
            {
                action();
            }
            else
            {
                Dispatcher.UIThread.Post(action);
            }
        }
    }
}
