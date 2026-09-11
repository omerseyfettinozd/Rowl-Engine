using System;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.Models
{
    public enum NotificationType
    {
        Info,
        Success,
        Warning,
        Error
    }

    public partial class ToastNotification : ObservableObject
    {
        public Guid Id { get; } = Guid.NewGuid();

        [ObservableProperty]
        private string _title = string.Empty;

        [ObservableProperty]
        private string _message = string.Empty;

        [ObservableProperty]
        private NotificationType _type = NotificationType.Info;

        [ObservableProperty]
        private DateTime _timestamp = DateTime.Now;

        [ObservableProperty]
        private bool _isDismissed = false;

        public string AccentColor => Type switch
        {
            NotificationType.Success => "#10B981", // Emerald Green
            NotificationType.Warning => "#F59E0B", // Amber
            NotificationType.Error   => "#EF4444", // Crimson Red
            _                        => "#3B82F6"  // Dodger Blue
        };

        public string IconSymbol => Type switch
        {
            NotificationType.Success => "✅",
            NotificationType.Warning => "⚠️",
            NotificationType.Error   => "❌",
            _                        => "ℹ️"
        };

        public ToastNotification(string message, NotificationType type = NotificationType.Info, string? title = null)
        {
            Message = message;
            Type = type;
            Title = title ?? type switch
            {
                NotificationType.Success => "Başarılı",
                NotificationType.Warning => "Uyarı",
                NotificationType.Error   => "Hata",
                _                        => "Bilgi"
            };
        }
    }
}
