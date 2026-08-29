using Avalonia.Controls;
using Avalonia.Controls.Templates;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Views.Components
{
    /// <summary>
    /// Selects the appropriate DataTemplate for each component type.
    /// Used by the Inspector's ItemsControl to render component-specific editors.
    /// </summary>
    public class ComponentTemplateSelector : IDataTemplate
    {
        public bool Match(object? data) => data is NodeComponentViewModel;

        public Control Build(object? data)
        {
            return data switch
            {
                DialogueComponentViewModel   => new DialogueComponentView(),
                BackgroundComponentViewModel => new BackgroundComponentView(),
                CharacterComponentViewModel  => new CharacterComponentView(),
                AudioComponentViewModel      => new AudioComponentView(),
                _ => new TextBlock { Text = "Unknown Component" }
            };
        }
    }
}
