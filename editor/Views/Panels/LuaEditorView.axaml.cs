#nullable enable
using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using AvaloniaEdit;
using AvaloniaEdit.Search;
using RowlEngine.Editor.Services.Scripting;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Panels
{
    // Lua paneli kod arkası: metin eşitleme, vurgulama ve arama paneli.
    public partial class LuaEditorView : UserControl
    {
        private TextEditor? _editor;
        private SearchPanel? _searchPanel;
        private LuaEditorViewModel? _vm;
        private bool _syncing;

        public LuaEditorView()
        {
            InitializeComponent();
            _editor = this.FindControl<TextEditor>("PART_CodeEditor");
            if (_editor is not null)
            {
                // Prototip: Tab = 2 boşluk, boşluklara çevir.
                _editor.Options.IndentationSize = 2;
                _editor.Options.ConvertTabsToSpaces = true;
                _editor.ShowLineNumbers = true;
                try { _searchPanel = SearchPanel.Install(_editor); } catch { _searchPanel = null; }
                _editor.TextChanged += OnEditorTextChanged;
                try
                {
                    if (_editor.TextArea?.Caret is not null)
                        _editor.TextArea.Caret.PositionChanged += OnCaretChanged;
                }
                catch { }
                _editor.AddHandler(KeyDownEvent, OnEditorKeyDown, Avalonia.Interactivity.RoutingStrategies.Tunnel);
            }
            this.DataContextChanged += OnDataContextChanged;
            this.GotFocus += OnGotFocus;
            // MenuFlyout bir Control değil (FindControl bulamaz): sahibi düğmenin Flyout'u üzerinden.
            var flyout = this.FindControl<Button>("PART_FilesButton")?.Flyout as MenuFlyout;
            if (flyout is not null)
                flyout.Opened += OnFilesOpened;
        }

        private void OnDataContextChanged(object? sender, EventArgs e)
        {
            if (_vm is not null)
            {
                _vm.SearchRequested -= OnSearchRequested;
                _vm.ToastRequested -= OnToast;
                _vm.ErrorRequested -= OnToast;
                try { _vm.PropertyChanged -= OnVmChanged; } catch { }
            }
            _vm = DataContext as LuaEditorViewModel;
            if (_vm is not null)
            {
                _vm.SearchRequested += OnSearchRequested;
                _vm.ToastRequested += OnToast;
                _vm.ErrorRequested += OnToast;
                _vm.PropertyChanged += OnVmChanged;
            }
            ApplyHighlighting();
            SyncEditorFromVm();
        }

        private void OnVmChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(LuaEditorViewModel.ActivePath))
                SyncEditorFromVm();
        }

        // Tema fırçalarıyla Lua vurgulamasını kurar.
        private void ApplyHighlighting()
        {
            if (_editor is null)
                return;
            try
            {
                IBrush? comment = FindBrush("LuaCommentBrush");
                IBrush? keyword = FindBrush("LuaKeywordBrush");
                IBrush? str = FindBrush("LuaStringBrush");
                IBrush? number = FindBrush("LuaNumberBrush");
                if (comment is not null || keyword is not null || str is not null || number is not null)
                    _editor.SyntaxHighlighting = LuaHighlighting.Create(comment, keyword, str, number);
                else
                    _editor.SyntaxHighlighting = LuaHighlighting.Create(null, null, null, null);
            }
            catch { }
        }

        private IBrush? FindBrush(string key)
        {
            try
            {
                if (this.TryFindResource(key, out object? v) && v is IBrush b)
                    return b;
            }
            catch { }
            return null;
        }

        // Etkin sekme değişince düzenleyiciyi tampon metniyle besler.
        private void SyncEditorFromVm()
        {
            if (_editor is null || _vm is null)
                return;
            string text = "";
            string? active = _vm.ActivePath;
            if (!string.IsNullOrEmpty(active))
            {
                foreach (var buf in _vm.Workspace.OpenBuffers)
                {
                    if (string.Equals(buf.RelativePath, active, StringComparison.OrdinalIgnoreCase))
                    {
                        text = buf.Text ?? "";
                        break;
                    }
                }
            }
            _syncing = true;
            try
            {
                if (_editor.Document is not null && !string.Equals(_editor.Document.Text, text, StringComparison.Ordinal))
                    _editor.Document.Text = text;
                else if (_editor.Text is null || !string.Equals(_editor.Text, text, StringComparison.Ordinal))
                    _editor.Text = text;
            }
            finally { _syncing = false; }
            try { _vm.UpdateCaret(_editor.CaretOffset); } catch { }
        }

        private void OnEditorTextChanged(object? sender, EventArgs e)
        {
            if (_syncing || _vm is null || _editor is null)
                return;
            try { _vm.UpdateActiveText(_editor.Text); } catch { }
        }

        private void OnCaretChanged(object? sender, EventArgs e)
        {
            if (_syncing || _vm is null || _editor is null)
                return;
            try { _vm.UpdateCaret(_editor.CaretOffset); } catch { }
        }

        private void OnEditorKeyDown(object? sender, KeyEventArgs e)
        {
            if (_vm is null)
                return;
            bool ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
            if (ctrl && e.Key == Key.S)
            {
                try { _vm.Save(_vm.ActivePath); } catch { }
                e.Handled = true;
            }
        }

        private void OnSearchRequested(object? sender, EventArgs e)
        {
            try { _searchPanel?.Open(); } catch { }
            try { _editor?.Focus(); } catch { }
        }

        private void OnToast(object? sender, string message)
        {
            // Gerçek bildirim kabuğa aittir; burada yalnızca tanılama düşer.
            Debug.WriteLine("[Lua] " + message);
        }

        private void OnGotFocus(object? sender, GotFocusEventArgs e)
        {
            try { _vm?.CheckExternalChanges(); SyncEditorFromVm(); } catch { }
        }

        // Dosyalar açılır listesini o anki listeyle kurar.
        private void OnFilesOpened(object? sender, EventArgs e)
        {
            if (sender is not MenuFlyout flyout || _vm is null)
                return;
            flyout.Items.Clear();
            foreach (string f in _vm.Files)
            {
                string path = f;
                var item = new MenuItem { Header = path };
                item.Click += (_, __) =>
                {
                    try { _vm.OpenFile(path); SyncEditorFromVm(); } catch { }
                };
                flyout.Items.Add(item);
            }
            foreach (string s in _vm.SkippedFiles)
            {
                var item = new MenuItem { Header = s, IsEnabled = false };
                try { ToolTip.SetTip(item, EditorStrings.T("Lua.Skipped.Tip", "Dosya çok büyük, atlandı", "File too large, skipped")); } catch { }
                flyout.Items.Add(item);
            }
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            base.OnPointerPressed(e);
            // Orta tuşla sekme kapatma (prototip davranışı).
            try
            {
                var props = e.GetCurrentPoint(this).Properties;
                if (props.IsMiddleButtonPressed && _vm is not null)
                {
                    if (e.Source is Control c && c.DataContext is LuaTabViewModel tab)
                    {
                        _vm.CloseTab(tab.RelativePath);
                        e.Handled = true;
                    }
                }
            }
            catch { }
        }
    }
}
