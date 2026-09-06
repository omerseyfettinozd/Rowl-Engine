using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// Represents a GameObject / Entity within a Node (Frame/Scene).
    /// Similar to Unity's GameObject, it is an empty container with a name,
    /// active state, and a collection of modular components attached to it.
    /// </summary>
    public partial class FrameObjectViewModel : ObservableObject
    {
        [ObservableProperty]
        private string _id = Guid.NewGuid().ToString("N")[..8];

        [ObservableProperty]
        private string _name = "GameObject";

        [ObservableProperty]
        private bool _isActive = true;

        [ObservableProperty]
        private NodeViewModel? _node;

        /// <summary>
        /// Collection of components attached to this GameObject.
        /// </summary>
        public ObservableCollection<NodeComponentViewModel> Components { get; } = new();

        /// <summary>
        /// Dynamic icon based on primary component, or 📦 for empty object.
        /// </summary>
        public string Icon
        {
            get
            {
                var first = Components.FirstOrDefault();
                if (first == null) return "📦";
                return first.Icon;
            }
        }

        public string SummaryText
        {
            get
            {
                if (Components.Count == 0) return "Empty Object";
                if (Components.Count == 1) return Components[0].DisplayName;
                return $"{Components.Count} Components";
            }
        }

        public FrameObjectViewModel()
        {
            Components.CollectionChanged += (s, e) =>
            {
                OnPropertyChanged(nameof(Icon));
                OnPropertyChanged(nameof(SummaryText));
            };
        }

        public FrameObjectViewModel(string name, NodeViewModel? node = null) : this()
        {
            Name = name;
            Node = node;
        }

        public T AddComponent<T>() where T : NodeComponentViewModel, new()
        {
            var comp = new T
            {
                OwnerObject = this,
                Node = this.Node
            };
            Components.Add(comp);
            comp.PropertyChanged += OnComponentPropertyChanged;

            if (comp is BackgroundComponentViewModel bg) bg.RefreshBitmap();
            else if (comp is CharacterComponentViewModel ch) ch.RefreshBitmap();
            else if (comp is DialogueComponentViewModel dlgComp && Node != null)
            {
                int existingCount = Node.AllComponents.OfType<DialogueComponentViewModel>().Count(d => d != dlgComp);
                if (existingCount > 0 && Math.Abs(dlgComp.X - 80.0) < 0.01 && Math.Abs(dlgComp.Y - 860.0) < 0.01)
                {
                    double newY = 860.0 - (200.0 * existingCount);
                    if (newY < 60.0) newY = 60.0 + (30.0 * (existingCount % 5));
                    dlgComp.Y = newY;
                    dlgComp.X = Math.Min(80.0 + (30.0 * existingCount), 300.0);
                }
            }

            OnPropertyChanged(nameof(Icon));
            OnPropertyChanged(nameof(SummaryText));
            Node?.NotifyObjectsChanged();
            return comp;
        }

        public void AddComponent(NodeComponentViewModel comp)
        {
            comp.OwnerObject = this;
            comp.Node = this.Node;
            Components.Add(comp);
            comp.PropertyChanged += OnComponentPropertyChanged;

            if (comp is BackgroundComponentViewModel bg) bg.RefreshBitmap();
            else if (comp is CharacterComponentViewModel ch) ch.RefreshBitmap();
            else if (comp is DialogueComponentViewModel dlgComp && Node != null)
            {
                int existingCount = Node.AllComponents.OfType<DialogueComponentViewModel>().Count(d => d != dlgComp);
                if (existingCount > 0 && Math.Abs(dlgComp.X - 80.0) < 0.01 && Math.Abs(dlgComp.Y - 860.0) < 0.01)
                {
                    double newY = 860.0 - (200.0 * existingCount);
                    if (newY < 60.0) newY = 60.0 + (30.0 * (existingCount % 5));
                    dlgComp.Y = newY;
                    dlgComp.X = Math.Min(80.0 + (30.0 * existingCount), 300.0);
                }
            }

            OnPropertyChanged(nameof(Icon));
            OnPropertyChanged(nameof(SummaryText));
            Node?.NotifyObjectsChanged();
        }

        public void RemoveComponent(NodeComponentViewModel comp)
        {
            comp.PropertyChanged -= OnComponentPropertyChanged;
            Components.Remove(comp);
            comp.OwnerObject = null;
            comp.Node = null;

            OnPropertyChanged(nameof(Icon));
            OnPropertyChanged(nameof(SummaryText));
            Node?.NotifyObjectsChanged();
        }

        public void MoveComponentUp(NodeComponentViewModel comp)
        {
            int idx = Components.IndexOf(comp);
            if (idx > 0)
            {
                Components.Move(idx, idx - 1);
                OnPropertyChanged(nameof(Icon));
                Node?.NotifyObjectsChanged();
            }
        }

        public void MoveComponentDown(NodeComponentViewModel comp)
        {
            int idx = Components.IndexOf(comp);
            if (idx >= 0 && idx < Components.Count - 1)
            {
                Components.Move(idx, idx + 1);
                OnPropertyChanged(nameof(Icon));
                Node?.NotifyObjectsChanged();
            }
        }

        public T? GetComponent<T>() where T : NodeComponentViewModel =>
            Components.OfType<T>().FirstOrDefault();

        public IEnumerable<T> GetComponents<T>() where T : NodeComponentViewModel =>
            Components.OfType<T>();

        public bool HasComponent<T>() where T : NodeComponentViewModel =>
            Components.OfType<T>().Any();

        private void OnComponentPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            Node?.NotifyComponentPropertyChanged(sender, e);
        }

        partial void OnIsActiveChanged(bool value)
        {
            Node?.NotifyObjectsChanged();
        }

        partial void OnNodeChanged(NodeViewModel? value)
        {
            foreach (var comp in Components)
            {
                comp.Node = value;
            }
        }
    }
}
