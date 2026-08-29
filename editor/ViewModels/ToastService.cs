using System;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;

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
        private string _toastBackground = "#22C55E";

        [ObservableProperty]
        private string _toastIcon = "✅";

        private CancellationTokenSource? _hideCts;

        public async void Show(string message, ToastType type = ToastType.Success, int durationMs = 3000)
        {
            _hideCts?.Cancel();
            _hideCts = new CancellationTokenSource();
            var token = _hideCts.Token;

            Message = message;
            (ToastIcon, ToastBackground) = type switch
            {
                ToastType.Success => ("✅", "#16A34A"),
                ToastType.Warning => ("⚠️", "#D97706"),
                ToastType.Error   => ("❌", "#DC2626"),
                ToastType.Info    => ("ℹ️", "#2563EB"),
                _                 => ("✅", "#16A34A")
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
