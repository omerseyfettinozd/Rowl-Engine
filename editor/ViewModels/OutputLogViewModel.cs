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
                else if (e.PropertyName == nameof(MainWindowViewModel.LogErrorCount))
                    OnPropertyChanged(nameof(LogErrorCount));
                else if (e.PropertyName == nameof(MainWindowViewModel.LogWarningCount))
                    OnPropertyChanged(nameof(LogWarningCount));
                else if (e.PropertyName == nameof(MainWindowViewModel.LogInfoCount))
                    OnPropertyChanged(nameof(LogInfoCount));
            };
        }

        public string LogOutput => MainViewModel.LogOutput;

        /// <summary>
        /// Unity kromu Dilim E: rozet sayaç proxy'leri (görünüm MainVM'e bağlı;
        /// bu cephe yeniden hedeflenirse hazır).
        /// </summary>
        public int LogErrorCount => MainViewModel.LogErrorCount;
        public int LogWarningCount => MainViewModel.LogWarningCount;
        public int LogInfoCount => MainViewModel.LogInfoCount;
    }
}
