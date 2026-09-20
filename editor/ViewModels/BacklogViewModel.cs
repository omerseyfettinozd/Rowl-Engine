using System.Collections.Generic;
using System.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>Read-only projection of the currently running player's dialogue history.</summary>
    public sealed partial class BacklogViewModel : ViewModelBase
    {
        private readonly MainWindowViewModel _main;

        public BacklogViewModel(MainWindowViewModel main)
        {
            _main = main;
            main.EngineHost.PropertyChanged += OnEnginePropertyChanged;
        }

        public IReadOnlyList<DialogueHistoryEntry> Entries => _main.EngineHost.DialogueHistory;

        /// <summary>Faz 5: kayıt var mı? / boş-durum paneli görünürlüğü.</summary>
        public bool HasEntries => Entries.Count > 0;
        public bool IsEmpty => !HasEntries;

        /// <summary>Faz 5: boş-durum eylemi — oyuncu önizlemeyi açar.</summary>
        [RelayCommand]
        private void OpenPlayer() => _main.ShowPanel("EnginePreview");

        public void Refresh()
        {
            OnPropertyChanged(nameof(Entries));
            OnPropertyChanged(nameof(HasEntries));
            OnPropertyChanged(nameof(IsEmpty));
        }

        private void OnEnginePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(EngineHost.DialogueHistory))
            {
                OnPropertyChanged(nameof(Entries));
                OnPropertyChanged(nameof(HasEntries));
                OnPropertyChanged(nameof(IsEmpty));
            }
        }
    }
}
