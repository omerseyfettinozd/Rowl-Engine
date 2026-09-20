using Avalonia;
using Avalonia.Controls;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Panels
{
    public partial class NodeInspectorView : UserControl
    {
        public NodeInspectorView()
        {
            InitializeComponent();
        }

        // Unity kromu Dilim G3: Button.Flyout içeriği görsel ağaca bağlı
        // değildir; Command bağları ancak menü açılınca çözülür. Komut
        // referansları sabittir (parametre boşken HierarchyViewModel canlı
        // seçime düşer), o yüzden kablolar burada da kurulur — hem çalışma
        // anında hem de headless kilitte (Test 43) hemen gözlenebilir.
        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == DataContextProperty
                && DataContext is MainWindowViewModel vm)
            {
                var hvm = vm.HierarchyViewModel;
                DuplicateObjectMenuItem.Command = hvm.DuplicateObjectCommand;
                DeleteObjectMenuItem.Command = hvm.DeleteObjectCommand;
                MoveObjectUpMenuItem.Command = hvm.MoveObjectUpCommand;
                MoveObjectDownMenuItem.Command = hvm.MoveObjectDownCommand;
            }
        }
    }
}
