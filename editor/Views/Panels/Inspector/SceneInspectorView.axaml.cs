using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace RowlEngine.Editor.Views.Panels.Inspector;

public partial class SceneInspectorView : UserControl
{
    public SceneInspectorView()
    {
        InitializeComponent();
    }

    private void InitializeComponent()
    {
        AvaloniaXamlLoader.Load(this);
    }
}
