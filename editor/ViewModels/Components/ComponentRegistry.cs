using System;
using System.Collections.Generic;
using System.Linq;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Registry and factory for component types.
    /// Provides centralized creation and discovery of available component types.
    /// </summary>
    public static class ComponentRegistry
    {
        private static readonly Dictionary<string, Func<NodeComponentViewModel>> _factories = new()
        {
            ["dialogue"] = () => new DialogueComponentViewModel(),
            ["background"] = () => new BackgroundComponentViewModel(),
            ["character"] = () => new CharacterComponentViewModel(),
            ["audio"] = () => new AudioComponentViewModel(),
            // Backward-compatible aliases
            ["speaker"] = () => new DialogueComponentViewModel(),
            ["dialogue_box"] = () => new DialogueComponentViewModel(),
        };

        /// <summary>
        /// Creates a new component instance by its type key.
        /// </summary>
        /// <param name="typeKey">The type key (e.g. "dialogue", "background", "character", "audio")</param>
        /// <returns>A new component instance</returns>
        public static NodeComponentViewModel Create(string typeKey)
        {
            if (_factories.TryGetValue(typeKey, out var factory))
                return factory();
            throw new KeyNotFoundException($"Unknown component type: '{typeKey}'");
        }

        /// <summary>
        /// Returns primary registered component type keys (excluding aliases).
        /// </summary>
        public static IReadOnlyList<string> AvailableTypes => new[] { "dialogue", "background", "character", "audio" };

        /// <summary>
        /// Returns display info (typeKey, displayName, icon) for primary registered types.
        /// </summary>
        public static IReadOnlyList<(string TypeKey, string DisplayName, string Icon)> GetAvailableComponentInfo()
        {
            return AvailableTypes.Select(typeKey =>
            {
                var instance = Create(typeKey);
                return (typeKey, instance.DisplayName, instance.Icon);
            }).ToList();
        }
    }
}
