using System;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels
{
    public partial class OutputLogViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public OutputLogViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            main.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.LogOutput))
                    OnPropertyChanged(nameof(LogOutput));
            };
        }

        public string LogOutput => MainViewModel.LogOutput;
    }
}
