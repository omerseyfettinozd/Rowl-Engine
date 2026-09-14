using System;
using System.ComponentModel;
using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Views.Player
{
    /// <summary>
    /// Faz 2 Dilim 5 player shell: owns the per-frame driver tick and the
    /// keyboard funnel into <see cref="PlayerInputMapper"/>. All decisions
    /// live in <see cref="PlayerViewModel"/>; this file only moves time and
    /// raw input across the boundary. Closing is driven by
    /// <see cref="PlayerViewModel.ExitRequested"/>.
    /// </summary>
    public partial class PlayerShellView : UserControl
    {
        private readonly DispatcherTimer _driverTimer;
        private long _lastTickTimestamp;
        private bool _exitHooked;

        public PlayerShellView()
        {
            InitializeComponent();
            _driverTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(1.0 / 30.0),
            };
            _driverTimer.Tick += (_, _) => DriveFrame();
            AttachedToVisualTree += (_, _) => StartDriving();
            DetachedFromVisualTree += (_, _) => StopDriving();
            DataContextChanged += (_, _) => HookExit();
        }

        private PlayerViewModel? Player => DataContext as PlayerViewModel;

        private void StartDriving()
        {
            _lastTickTimestamp = Stopwatch.GetTimestamp();
            _driverTimer.Start();
            Focus();
        }

        private void StopDriving() => _driverTimer.Stop();

        private void DriveFrame()
        {
            var player = Player;
            if (player is null)
                return;
            long now = Stopwatch.GetTimestamp();
            float dt = (float)(now - _lastTickTimestamp) / Stopwatch.Frequency;
            _lastTickTimestamp = now;
            if (dt <= 0.0f || dt > 0.5f)
                return;
            try
            {
                player.Tick(Math.Min(dt, 0.25f));
            }
            catch (Exception failure)
            {
                Debug.WriteLine($"Player driver tick failed: {failure.Message}");
                StopDriving();
            }
        }

        private void HookExit()
        {
            if (_exitHooked || Player is null)
                return;
            _exitHooked = true;
            Player.PropertyChanged += OnPlayerPropertyChanged;
        }

        private void OnPlayerPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(PlayerViewModel.ExitRequested) &&
                Player?.ExitRequested == true &&
                TopLevel.GetTopLevel(this) is Window window)
            {
                window.Close();
            }
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            var player = Player;
            if (player is null)
                return;
            var command = PlayerInputMapper.MapKey(
                e.Key,
                e.KeyModifiers.HasFlag(KeyModifiers.Control),
                e.KeyModifiers.HasFlag(KeyModifiers.Shift),
                e.KeyModifiers.HasFlag(KeyModifiers.Meta) ||
                e.KeyModifiers.HasFlag(KeyModifiers.Alt));
            if (command is not null)
            {
                player.HandleCommand(command.Value);
                e.Handled = true;
            }
        }
    }
}
