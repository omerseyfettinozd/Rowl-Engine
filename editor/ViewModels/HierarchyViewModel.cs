using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// ViewModel for the Hierarchy panel.
    /// In Unity, the Hierarchy panel displays all GameObjects in the scene.
    /// Here, it displays all GameObjects (FrameObjects) in the currently selected Node (Frame).
    /// </summary>
    public partial class HierarchyViewModel : ObservableObject
    {
        public MainWindowViewModel MainViewModel { get; }
        private NodeViewModel? _observedNode;

        public HierarchyViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            main.PropertyChanged += OnMainViewModelPropertyChanged;
        }

        // ── Selected GameObject (drives the Inspector) ──

        /// <summary>
        /// The GameObject currently selected in the Hierarchy.
        /// The Inspector displays this GameObject's properties and components.
        /// </summary>
        [ObservableProperty]
        private FrameObjectViewModel? _selectedObject;

        partial void OnSelectedObjectChanged(FrameObjectViewModel? value)
        {
            MainViewModel.InspectorViewModel?.NotifySelectedObjectChanged();
        }

        // ── Current Node Info (displayed at top of Hierarchy) ──

        public NodeViewModel? CurrentNode => MainViewModel.SelectedNode;
        public string CurrentNodeTitle => CurrentNode?.Title ?? "No Node Selected";
        public string CurrentNodeId => CurrentNode != null ? $"Node #{CurrentNode.Id}" : "—";
        public bool HasCurrentNode => CurrentNode != null;
        public bool HasObjects => CurrentNode?.Objects.Count > 0;

        // ── Create Object Menu ──

        [ObservableProperty]
        private bool _isCreateObjectMenuOpen;

        [RelayCommand]
        public void ToggleCreateObjectMenu()
        {
            IsCreateObjectMenuOpen = !IsCreateObjectMenuOpen;
        }

        [RelayCommand]
        public void CreateObject(string typeKey)
        {
            if (CurrentNode == null) return;

            FrameObjectViewModel newObj;

            switch (typeKey?.ToLowerInvariant())
            {
                case "background":
                    newObj = CurrentNode.CreateObject("Background");
                    newObj.AddComponent<BackgroundComponentViewModel>();
                    break;
                case "character":
                    newObj = CurrentNode.CreateObject("Character");
                    newObj.AddComponent<CharacterComponentViewModel>();
                    break;
                case "dialogue":
                    newObj = CurrentNode.CreateObject("Dialogue Box");
                    newObj.AddComponent<DialogueComponentViewModel>();
                    break;
                case "audio":
                    newObj = CurrentNode.CreateObject("Audio");
                    newObj.AddComponent<AudioComponentViewModel>();
                    break;
                default:
                    newObj = CurrentNode.CreateObject("New GameObject");
                    break;
            }

            SelectedObject = newObj;
            IsCreateObjectMenuOpen = false;
            MainViewModel.AppendLog($"📦 Created {newObj.Name} in Node #{CurrentNode.Id}");
            MainViewModel.ScheduleSave();
        }

        [RelayCommand]
        public void DeleteObject(FrameObjectViewModel? obj)
        {
            var target = obj ?? SelectedObject;
            if (CurrentNode == null || target == null) return;

            string name = target.Name;
            CurrentNode.RemoveObject(target);

            if (SelectedObject == target)
            {
                SelectedObject = CurrentNode.Objects.FirstOrDefault();
            }

            MainViewModel.AppendLog($"🗑️ Deleted object '{name}' from Node #{CurrentNode.Id}");
            MainViewModel.ScheduleSave();
        }

        [RelayCommand]
        public void DuplicateObject(FrameObjectViewModel? obj)
        {
            var target = obj ?? SelectedObject;
            if (CurrentNode == null || target == null) return;

            var copy = CurrentNode.DuplicateObject(target);
            SelectedObject = copy;

            MainViewModel.AppendLog($"📋 Duplicated object '{target.Name}' in Node #{CurrentNode.Id}");
            MainViewModel.ScheduleSave();
        }

        [RelayCommand]
        public void MoveObjectUp(FrameObjectViewModel? obj)
        {
            var target = obj ?? SelectedObject;
            if (CurrentNode == null || target == null) return;
            CurrentNode.MoveObjectUp(target);
            MainViewModel.ScheduleSave();
        }

        [RelayCommand]
        public void MoveObjectDown(FrameObjectViewModel? obj)
        {
            var target = obj ?? SelectedObject;
            if (CurrentNode == null || target == null) return;
            CurrentNode.MoveObjectDown(target);
            MainViewModel.ScheduleSave();
        }

        // ── Event Handling ──

        private void OnMainViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
            {
                IsCreateObjectMenuOpen = false;

                if (_observedNode != null)
                {
                    _observedNode.PropertyChanged -= OnCurrentNodePropertyChanged;
                }

                _observedNode = CurrentNode;
                if (_observedNode != null)
                {
                    _observedNode.PropertyChanged += OnCurrentNodePropertyChanged;
                }

                OnPropertyChanged(nameof(CurrentNode));
                OnPropertyChanged(nameof(CurrentNodeTitle));
                OnPropertyChanged(nameof(CurrentNodeId));
                OnPropertyChanged(nameof(HasCurrentNode));
                OnPropertyChanged(nameof(HasObjects));

                // Auto-select first object or null
                SelectedObject = CurrentNode?.Objects.FirstOrDefault();
            }
        }

        private void OnCurrentNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(NodeViewModel.Objects))
            {
                OnPropertyChanged(nameof(HasObjects));
            }
        }
    }
}
