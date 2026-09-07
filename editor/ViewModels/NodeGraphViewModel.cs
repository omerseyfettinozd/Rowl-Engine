using System.Collections.ObjectModel;
using RowlEngine.Editor.Models;

namespace RowlEngine.Editor.ViewModels
{
    public partial class NodeGraphViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public NodeGraphViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
        }

        public ObservableCollection<NodeViewModel> Nodes => MainViewModel.Nodes;
        public ObservableCollection<ConnectionViewModel> Connections => MainViewModel.Connections;
    }
}
