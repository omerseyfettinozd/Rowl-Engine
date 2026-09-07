namespace RowlEngine.Editor.ViewModels
{
    public partial class LivePreviewViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public LivePreviewViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
        }
    }
}
