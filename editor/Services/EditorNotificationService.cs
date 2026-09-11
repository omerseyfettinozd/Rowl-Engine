using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Models;

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
