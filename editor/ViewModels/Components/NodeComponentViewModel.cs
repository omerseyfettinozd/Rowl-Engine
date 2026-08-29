using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Abstract base class for all node components.
    /// Each component represents a modular piece of functionality
    /// that can be attached to a NodeViewModel (similar to Unity's Component model).
    /// </summary>
    public abstract partial class NodeComponentViewModel : ObservableObject
    {
        /// <summary>
        /// Unique identifier for this component instance (used for serialization & referencing).
        /// </summary>
        [ObservableProperty]
        private string _componentId = Guid.NewGuid().ToString("N")[..8];

        /// <summary>
        /// Human-readable display name shown in the Inspector header.
        /// </summary>
        public abstract string DisplayName { get; }

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
        /// Reference to the parent NodeViewModel owning this component instance.
        /// </summary>
        [ObservableProperty]
        private NodeViewModel? _node;

        /// <summary>
        /// Removes this component from its parent node.
        /// </summary>
        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void RemoveSelf()
        {
            Node?.RemoveComponent(this);
        }

        /// <summary>
        /// Moves this component up in the parent node's component list.
        /// </summary>
        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void MoveUp()
        {
            Node?.MoveComponentUp(this);
        }

        /// <summary>
        /// Moves this component down in the parent node's component list.
        /// </summary>
        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void MoveDown()
        {
            Node?.MoveComponentDown(this);
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
