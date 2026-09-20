using System;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels
{
    public enum ToastType
    {
        Success,
        Warning,
        Error,
        Info
    }

    public partial class ToastService : ObservableObject
    {
        private static readonly Lazy<ToastService> _instance = new(() => new ToastService());
        public static ToastService Instance => _instance.Value;

        [ObservableProperty]
        private string _message = "";

        [ObservableProperty]
        private bool _isVisible = false;

        [ObservableProperty]
        private string _toastBackground =
            ThemeFallbackColors.BrushHex("SuccessBrush", ThemeFallbackColors.Success);

        [ObservableProperty]
        private string _toastIcon = "";

        private CancellationTokenSource? _hideCts;

        public async void Show(string message, ToastType type = ToastType.Success, int durationMs = 3000)
        {
            _hideCts?.Cancel();
            _hideCts = new CancellationTokenSource();
            var token = _hideCts.Token;

            Message = message;
            (ToastIcon, ToastBackground) = type switch
            {
                ToastType.Success => ("", ThemeFallbackColors.BrushHex("SuccessBrush", ThemeFallbackColors.Success)),
                ToastType.Warning => ("", ThemeFallbackColors.BrushHex("WarningBrush", ThemeFallbackColors.Warning)),
                ToastType.Error   => ("", ThemeFallbackColors.BrushHex("ErrorBrush", ThemeFallbackColors.Error)),
                ToastType.Info    => ("", ThemeFallbackColors.BrushHex("InfoBrush", ThemeFallbackColors.Info)),
                _                 => ("", ThemeFallbackColors.BrushHex("SuccessBrush", ThemeFallbackColors.Success))
            };
            IsVisible = true;

            try
            {
                await Task.Delay(durationMs, token);
                IsVisible = false;
            }
            catch (TaskCanceledException)
            {
            }
        }
    }
}
