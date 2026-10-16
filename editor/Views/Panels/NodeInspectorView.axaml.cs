using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Inspector;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Panels
{
    public partial class NodeInspectorView : UserControl
    {
        private InspectorEditUndoRecorder? _editRecorder;
        private MainWindowViewModel? _watchedVm;

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

                // Denetçi geri alma kaydı (E3).
                if (!ReferenceEquals(vm, _watchedVm))
                    AttachRecorder(vm);
            }
            else if (change.Property == DataContextProperty)
            {
                DetachRecorder();
            }
        }

        // Düzen değişiminde panel ağaçtan çıkıp geri girebilir; DataContext aynı kaldığı
        // için kayıtçı burada yeniden kurulur.
        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            if (_editRecorder is null && DataContext is MainWindowViewModel vm)
                AttachRecorder(vm);
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnDetachedFromVisualTree(e);
            DetachRecorder();
        }

        // Kayıtçıyı yeni VM'ye bağlar.
        private void AttachRecorder(MainWindowViewModel vm)
        {
            DetachRecorder();
            _watchedVm = vm;
            vm.PropertyChanged += OnWatchedVmPropertyChanged;
            _editRecorder = new InspectorEditUndoRecorder();
            _editRecorder.Attach(this, () => _watchedVm!.SelectedNode, UndoRedoService.Instance);
        }

        private void DetachRecorder()
        {
            if (_watchedVm != null)
                _watchedVm.PropertyChanged -= OnWatchedVmPropertyChanged;
            _watchedVm = null;
            _editRecorder?.Dispose();
            _editRecorder = null;
        }

        private void OnWatchedVmPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
                _editRecorder?.RefreshTrackedNode();
        }
    }
}
