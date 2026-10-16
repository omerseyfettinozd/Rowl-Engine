using System;
using System.Collections.Generic;
using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Abstract base class for all node components.
    /// Each component represents a modular piece of functionality
    /// that can be attached to a FrameObjectViewModel (similar to Unity's Component model).
    /// </summary>
    public abstract partial class NodeComponentViewModel : ObservableObject
    {
        private static readonly object s_liveGate = new();
        private static readonly List<WeakReference<NodeComponentViewModel>> s_liveInstances = new();
        private static bool s_isStringsHooked;

        protected NodeComponentViewModel()
        {
            lock (s_liveGate)
            {
                s_liveInstances.Add(new WeakReference<NodeComponentViewModel>(this));
                if (!s_isStringsHooked)
                {
                    EditorStrings.Instance.PropertyChanged += OnEditorStringsPropertyChanged;
                    s_isStringsHooked = true;
                }
                if (s_liveInstances.Count % 64 == 0)
                {
                    PruneDeadInstancesLocked();
                }
            }
        }

        private static void OnEditorStringsPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (!string.IsNullOrEmpty(e.PropertyName) &&
                e.PropertyName != EditorStrings.IndexerPropertyName &&
                e.PropertyName != nameof(EditorStrings.Language))
            {
                return;
            }

            List<NodeComponentViewModel> alive;
            lock (s_liveGate)
            {
                alive = new List<NodeComponentViewModel>(s_liveInstances.Count);
                for (int i = s_liveInstances.Count - 1; i >= 0; i--)
                {
                    if (s_liveInstances[i].TryGetTarget(out var vm))
                        alive.Add(vm);
                    else
                        s_liveInstances.RemoveAt(i);
                }
            }

            foreach (var vm in alive)
            {
                vm.OnPropertyChanged(nameof(DisplayName));
            }
        }

        private static void PruneDeadInstancesLocked()
        {
            for (int i = s_liveInstances.Count - 1; i >= 0; i--)
            {
                if (!s_liveInstances[i].TryGetTarget(out _))
                    s_liveInstances.RemoveAt(i);
            }
        }

        /// <summary>
        /// Unique identifier for this component instance (used for serialization & referencing).
        /// </summary>
        [ObservableProperty]
        private string _componentId = Guid.NewGuid().ToString("N")[..8];

        /// <summary>
        /// Catalog key for the human-readable display name shown in the Inspector header.
        /// </summary>
        public abstract string DisplayNameKey { get; }

        /// <summary>
        /// Source-language (tr) fallback for the display name.
        /// </summary>
        protected abstract string DisplayNameTrFallback { get; }

        /// <summary>
        /// Fallback-language (en) fallback for the display name.
        /// </summary>
        protected abstract string DisplayNameEnFallback { get; }

        /// <summary>
        /// Human-readable display name shown in the Inspector header.
        /// </summary>
        public virtual string DisplayName =>
            EditorStrings.Instance.Get(DisplayNameKey, DisplayNameTrFallback, DisplayNameEnFallback);

        /// <summary>
        /// Emoji icon for the component header.
        /// </summary>
        public abstract string Icon { get; }

        /// <summary>
        /// Type key used for serialization (e.g. "speaker", "background", "character").
        /// </summary>
        public abstract string TypeKey { get; }

        /// <summary>
        /// Whether the component section is expanded in the Inspector.
        /// </summary>
        [ObservableProperty]
        private bool _isExpanded = true;

        /// <summary>
        /// Whether this component is active. Disabled components are not rendered by the engine.
        /// </summary>
        [ObservableProperty]
        private bool _isEnabled = true;

        /// <summary>
        /// Reference to the GameObject (FrameObjectViewModel) owning this component.
        /// </summary>
        [ObservableProperty]
        private FrameObjectViewModel? _ownerObject;

        /// <summary>
        /// Reference to the parent NodeViewModel owning this component instance.
        /// </summary>
        [ObservableProperty]
        private NodeViewModel? _node;

        /// <summary>
        /// Removes this component from its parent object / node.
        /// </summary>
        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void RemoveSelf()
        {
            if (OwnerObject != null)
            {
                OwnerObject.RemoveComponent(this);
            }
            else
            {
                Node?.RemoveComponent(this);
            }
        }

        /// <summary>
        /// Moves this component up in the parent object's component list.
        /// </summary>
        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void MoveUp()
        {
            if (OwnerObject != null)
            {
                OwnerObject.MoveComponentUp(this);
            }
            else
            {
                Node?.MoveComponentUp(this);
            }
        }

        /// <summary>
        /// Moves this component down in the parent object's component list.
        /// </summary>
        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void MoveDown()
        {
            if (OwnerObject != null)
            {
                OwnerObject.MoveComponentDown(this);
            }
            else
            {
                Node?.MoveComponentDown(this);
            }
        }

        /// <summary>
        /// Serializes this component's data to a dictionary for JSON output.
        /// </summary>
        public abstract Dictionary<string, object> Serialize();

        /// <summary>
        /// Deserializes data from a dictionary (parsed from JSON) into this component.
        /// </summary>
        public abstract void Deserialize(Dictionary<string, object?> data);
    }
}
