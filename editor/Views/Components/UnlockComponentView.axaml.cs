using Avalonia;
using Avalonia.Controls;
using RowlEngine.Editor.Services.Extras;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Views.Components
{
    public partial class UnlockComponentView : UserControl
    {
        public UnlockComponentView()
        {
            InitializeComponent();
            var open = this.FindControl<Button>("OpenCatalogButton");
            if (open != null)
                open.Click += (_, _) => ExtrasCatalogEditorCoordinator.Open(MainWindowViewModel.AssetsPath);
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            // The catalog is a file the author may have just edited (extras catalog editor).
            ExtrasCatalogStore.Saved += OnCatalogSaved;
            (DataContext as UnlockComponentViewModel)?.RefreshCatalog();
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            ExtrasCatalogStore.Saved -= OnCatalogSaved;
            base.OnDetachedFromVisualTree(e);
        }

        private void OnCatalogSaved() =>
            Avalonia.Threading.Dispatcher.UIThread.Post(() => (DataContext as UnlockComponentViewModel)?.RefreshCatalog());
    }
}
