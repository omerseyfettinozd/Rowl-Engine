using System;
using System.Globalization;
using System.IO;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels
{
    // Kartın açılabilirlik durumu.
    public enum ProjectCardState { Ready, Missing, Invalid }

    public partial class ProjectCardViewModel : ViewModelBase
    {
        private readonly ProjectHubViewModel _hub;
        private readonly ProjectRegistryService _registry;

        public ProjectInfo Info { get; private set; }

        public string Id => Info.Id;
        public string Name => Info.Name;
        public string Path => Info.Path;
        public DateTime LastOpenedAt => Info.LastOpenedAt;

        // XAML'deki gömülü StringFormat yerine viewmodel metni.
        public string LastOpenedText => string.Format(
            CultureInfo.CurrentCulture,
            EditorStrings.T("Hub.LastOpened"),
            LastOpenedAt.ToLocalTime().ToString("g", CultureInfo.CurrentCulture));

        public bool HasCover => Info.HasCover;

        // Durum değişince türetilmişler + Open CanExecute tazelenir.
        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(CanOpen), nameof(IsMissing), nameof(IsUnavailable), nameof(StateText))]
        [NotifyCanExecuteChangedFor(nameof(OpenCommand))]
        private ProjectCardState _state;

        [ObservableProperty]
        private Bitmap? _coverBitmap;

        [ObservableProperty]
        private bool _isCoverLoaded;

        public bool CanOpen => State == ProjectCardState.Ready;
        public bool IsMissing => State == ProjectCardState.Missing;
        public bool IsUnavailable => State != ProjectCardState.Ready;

        public string StateText => State switch
        {
            ProjectCardState.Missing => EditorStrings.T("Hub.State.Missing"),
            ProjectCardState.Invalid => EditorStrings.T("Hub.State.Invalid"),
            _ => string.Empty,
        };

        public ProjectCardViewModel(ProjectInfo info, ProjectHubViewModel hub, ProjectRegistryService registry)
        {
            Info = info;
            _hub = hub;
            _registry = registry;
            State = ProjectHubViewModel.ProbeProject(Info.Path);
            LoadCover();
        }

        public void Refresh(ProjectInfo newInfo)
        {
            Info = newInfo;
            OnPropertyChanged(nameof(Name));
            OnPropertyChanged(nameof(Path));
            OnPropertyChanged(nameof(LastOpenedAt));
            OnPropertyChanged(nameof(LastOpenedText));
            OnPropertyChanged(nameof(HasCover));
            OnPropertyChanged(nameof(Id));
            State = ProjectHubViewModel.ProbeProject(Info.Path);
            LoadCover();
        }

        // Diskteki güncel duruma bak.
        public void Reprobe()
        {
            State = ProjectHubViewModel.ProbeProject(Info.Path);
        }

        private void LoadCover()
        {
            CoverBitmap?.Dispose();
            CoverBitmap = null;
            IsCoverLoaded = false;
            if (Info.HasCover)
            {
                try
                {
                    string abs = Info.CoverAbsolutePath;
                    if (File.Exists(abs))
                    {
                        CoverBitmap = new Bitmap(abs);
                        IsCoverLoaded = true;
                    }
                }
                catch { }
            }
        }

        [RelayCommand]
        public async System.Threading.Tasks.Task RenameAsync()
        {
            await _hub.RenameProjectAsync(this);
            LoadCover();
        }

        [RelayCommand]
        public async System.Threading.Tasks.Task SetCoverAsync()
        {
            await _hub.SetCoverAsync(this);
            LoadCover();
            OnPropertyChanged(nameof(HasCover));
        }

        [RelayCommand]
        public async System.Threading.Tasks.Task DeleteAsync()
        {
            await _hub.DeleteProjectAsync(this);
        }

        [RelayCommand(CanExecute = nameof(CanOpen))]
        public void Open()
        {
            _hub.OpenProject(this);
        }

        // Yalnız listeden çıkar, diske dokunmaz.
        [RelayCommand]
        public void RemoveFromList()
        {
            _hub.RemoveProject(this);
        }
    }
}
