using System.Collections.Generic;
using System.ComponentModel;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>Read-only projection of the currently running player's dialogue history.</summary>
    public sealed class BacklogViewModel : ViewModelBase
    {
        private readonly MainWindowViewModel _main;

        public BacklogViewModel(MainWindowViewModel main)
        {
            _main = main;
            main.EngineHost.PropertyChanged += OnEnginePropertyChanged;
        }

        public IReadOnlyList<DialogueHistoryEntry> Entries => _main.EngineHost.DialogueHistory;

        private void OnEnginePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EngineHost.DialogueHistory))
                OnPropertyChanged(nameof(Entries));
        }
    }
}
