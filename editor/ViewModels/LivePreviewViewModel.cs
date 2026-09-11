using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels
{
    public partial class LivePreviewViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        [ObservableProperty]
        private float _masterPeakL = 0.0f;

        [ObservableProperty]
        private float _masterPeakR = 0.0f;

        [ObservableProperty]
        private float _masterRmsL = 0.0f;

        [ObservableProperty]
        private float _masterRmsR = 0.0f;

        [ObservableProperty]
        private bool _isAudioActive = false;

        public LivePreviewViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
        }

        public void UpdateAudioTelemetry(float peakL, float peakR, float rmsL, float rmsR)
        {
            MasterPeakL = peakL;
            MasterPeakR = peakR;
            MasterRmsL = rmsL;
            MasterRmsR = rmsR;
            IsAudioActive = (peakL > 0.005f || peakR > 0.005f);
        }
    }
}
