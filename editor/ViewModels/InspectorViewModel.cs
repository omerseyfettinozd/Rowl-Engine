using System;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels
{
    public partial class InspectorViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        public InspectorViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            main.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
                {
                    OnPropertyChanged(nameof(SelectedNode));
                    OnPropertyChanged(nameof(SelectedObject));
                    OnPropertyChanged(nameof(HasSelectedObject));
                }
            };
        }

        public NodeViewModel? SelectedNode => MainViewModel.SelectedNode;

        /// <summary>
        /// The GameObject currently selected in the Hierarchy panel.
        /// The Inspector displays this GameObject's properties and attached components.
        /// </summary>
        public FrameObjectViewModel? SelectedObject => MainViewModel.HierarchyViewModel?.SelectedObject;

        public bool HasSelectedObject => SelectedObject != null;

        /// <summary>
        /// Called by HierarchyViewModel when selected GameObject changes.
        /// </summary>
        public void NotifySelectedObjectChanged()
        {
            OnPropertyChanged(nameof(SelectedObject));
            OnPropertyChanged(nameof(HasSelectedObject));
        }
    }
}
