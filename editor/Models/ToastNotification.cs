using System;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services;

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
            NotificationType.Success => ThemeFallbackColors.BrushHex("SuccessBrush", ThemeFallbackColors.Success),
            NotificationType.Warning => ThemeFallbackColors.BrushHex("WarningBrush", ThemeFallbackColors.Warning),
            NotificationType.Error => ThemeFallbackColors.BrushHex("ErrorBrush", ThemeFallbackColors.Error),
            _ => ThemeFallbackColors.BrushHex("InfoBrush", ThemeFallbackColors.Info)
        };

        public string IconSymbol => Type switch
        {
            NotificationType.Success => "",
            NotificationType.Warning => "",
            NotificationType.Error   => "",
            _                        => ""
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
