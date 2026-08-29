using System;
using System.IO;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels
{
    public partial class ProjectCardViewModel : ViewModelBase
    {
        private readonly ProjectHubViewModel _hub;
        private readonly ProjectRegistryService _registry;

        public ProjectInfo Info { get; private set; }

        public string Id => Info.Id;
        public string Name => Info.Name;
        public string Path => Info.Path;
        public DateTime LastOpenedAt => Info.LastOpenedAt;
        public string LastOpenedText => LastOpenedAt.ToString("dd MMM yyyy HH:mm");
        public bool HasCover => Info.HasCover;

        [ObservableProperty]
        private Bitmap? _coverBitmap;

        [ObservableProperty]
        private bool _isCoverLoaded;

        public ProjectCardViewModel(ProjectInfo info, ProjectHubViewModel hub, ProjectRegistryService registry)
        {
            Info = info;
            _hub = hub;
            _registry = registry;
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
            LoadCover();
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

        [RelayCommand]
        public void Open()
        {
            _hub.OpenProject(this);
        }
    }
}
