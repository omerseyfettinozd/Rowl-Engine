using Avalonia.Controls;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Views.Player
{
    /// <summary>
    /// Standalone Faz 2 player window. The shell view inherits this window's
    /// DataContext (the session <see cref="PlayerViewModel"/>); closing is
    /// driven by <see cref="PlayerViewModel.ExitRequested"/>.
    /// </summary>
    public partial class PlayerWindow : Window
    {
        public PlayerWindow(PlayerViewModel player)
        {
            DataContext = player;
            InitializeComponent();
            // D01c-2: closing the window is the last explicit save point; the
            // native store otherwise writes pending reads only ~2 s after the change.
            Closed += (_, _) => player.Loop.Close();
        }
    }
}
