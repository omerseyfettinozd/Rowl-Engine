#nullable enable
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Scripting;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.ViewModels
{
    // Sekme satırı için küçük görünüm modeli (dosya adı, etkinlik, kirli nokta).
    public sealed partial class LuaTabViewModel : ObservableObject
    {
        [ObservableProperty]
        private string _relativePath = "";

        [ObservableProperty]
        private bool _isActive;

        [ObservableProperty]
        private bool _isDirty;

        public string FileName
        {
            get
            {
                string p = RelativePath ?? "";
                int idx = p.LastIndexOf('/');
                return idx >= 0 ? p[(idx + 1)..] : p;
            }
        }

        partial void OnRelativePathChanged(string value)
            => OnPropertyChanged(nameof(FileName));
    }

    // Lua düzenleme panelinin görünüm modeli (prototip lua-editor.js).
    public sealed partial class LuaEditorViewModel : ObservableObject
    {
        private readonly MainWindowViewModel _main;
        private readonly LuaScriptWorkspace _workspace;
        private readonly HashSet<LuaScriptWorkspace.ScriptBuffer> _subscribed = new();
        private bool _suppressBound;

        public LuaScriptWorkspace Workspace => _workspace;

        public ObservableCollection<LuaTabViewModel> Tabs { get; } = new();
        public ObservableCollection<string> Files { get; } = new();
        public ObservableCollection<string> SkippedFiles { get; } = new();
        public ObservableCollection<FrameObjectViewModel> BoundObjects { get; } = new();

        [ObservableProperty]
        private FrameObjectViewModel? _selectedBoundObject;

        [ObservableProperty]
        private string _statusCaret = "";

        [ObservableProperty]
        private bool _isActiveDirty;

        [ObservableProperty]
        private bool _isExternalBannerVisible;

        [ObservableProperty]
        private string? _externalChangePath;

        [ObservableProperty]
        private bool _isCloseConfirmVisible;

        [ObservableProperty]
        private string? _pendingClosePath;

        public string? ActivePath => _workspace.ActivePath;
        public bool HasTabs => Tabs.Count > 0;
        public bool HasNoTabs => Tabs.Count == 0;

        public string? LastToast { get; private set; }
        public string? LastError { get; private set; }

        public event EventHandler? SearchRequested;
        public event EventHandler<string>? ToastRequested;
        public event EventHandler<string>? ErrorRequested;

        public LuaEditorViewModel(MainWindowViewModel main, string? assetsRoot = null)
        {
            _main = main ?? throw new ArgumentNullException(nameof(main));
            string? staticRoot = null;
            try { staticRoot = MainWindowViewModel.AssetsPath; } catch { staticRoot = null; }
            string root = assetsRoot ?? staticRoot ?? MainWindowViewModel.ProjectRoot;
            _workspace = new LuaScriptWorkspace(root);
            _main.PropertyChanged += OnMainPropertyChanged;
            RefreshFiles();
            EnsureInitialTabs();
            RefreshBoundObjects();
            SyncSelectedBoundObject();
            UpdateCaret(0);
        }

        private void OnMainPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
            {
                RefreshBoundObjects();
                SyncSelectedBoundObject();
            }
        }

        partial void OnSelectedBoundObjectChanged(FrameObjectViewModel? value)
        {
            if (_suppressBound)
                return;
            if (value is null)
                return; // "(yok)" seçimi hiçbir şeyi çözmez
            BindObject(value);
        }

        // Başlangıç sekmeleri: seçili düğümün betikleri, yoksa ilk dosya.
        private void EnsureInitialTabs()
        {
            var refs = new List<string>();
            try
            {
                var node = _main.SelectedNode;
                if (node is not null)
                {
                    foreach (var obj in node.Objects)
                    {
                        foreach (var comp in obj.Components.OfType<ScriptComponentViewModel>())
                        {
                            string p = (comp.ScriptPath ?? "").Replace('\\', '/').Trim();
                            if (p.Length == 0)
                                continue;
                            bool exists = false;
                            foreach (string f in _workspace.Files)
                            {
                                if (string.Equals(f, p, StringComparison.OrdinalIgnoreCase))
                                {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists)
                                continue;
                            bool dup = false;
                            foreach (string r in refs)
                            {
                                if (string.Equals(r, p, StringComparison.OrdinalIgnoreCase))
                                {
                                    dup = true;
                                    break;
                                }
                            }
                            if (!dup)
                                refs.Add(p);
                        }
                    }
                }
            }
            catch { }

            try
            {
                if (refs.Count > 0)
                {
                    foreach (string r in refs)
                    {
                        try { _workspace.Open(r); } catch { }
                    }
                    try { _workspace.Activate(refs[0]); } catch { }
                }
                else if (_workspace.Files.Count > 0)
                {
                    try
                    {
                        _workspace.Open(_workspace.Files[0]);
                        _workspace.Activate(_workspace.Files[0]);
                    }
                    catch { }
                }
            }
            catch { }
            RebuildTabs();
        }

        private void RefreshFiles()
        {
            Files.Clear();
            foreach (string f in _workspace.Files)
                Files.Add(f);
            SkippedFiles.Clear();
            foreach (string s in _workspace.SkippedFiles)
                SkippedFiles.Add(s);
        }

        private LuaScriptWorkspace.ScriptBuffer? FindBuffer(string path)
        {
            foreach (var b in _workspace.OpenBuffers)
            {
                if (string.Equals(b.RelativePath, path, StringComparison.OrdinalIgnoreCase))
                    return b;
            }
            return null;
        }

        // Açık tamponlarla sekme listesini eşitler.
        private void RebuildTabs()
        {
            var open = _workspace.OpenBuffers.ToList();
            var byPath = new Dictionary<string, LuaTabViewModel>(StringComparer.OrdinalIgnoreCase);
            foreach (var t in Tabs)
                byPath[t.RelativePath] = t;

            for (int i = Tabs.Count - 1; i >= 0; i--)
            {
                bool alive = false;
                foreach (var b in open)
                {
                    if (string.Equals(b.RelativePath, Tabs[i].RelativePath, StringComparison.OrdinalIgnoreCase))
                    {
                        alive = true;
                        break;
                    }
                }
                if (!alive)
                    Tabs.RemoveAt(i);
            }

            foreach (var buf in open)
            {
                if (!_subscribed.Contains(buf))
                {
                    buf.Changed += OnBufferChanged;
                    _subscribed.Add(buf);
                }
                if (!byPath.TryGetValue(buf.RelativePath, out LuaTabViewModel? tab))
                {
                    tab = new LuaTabViewModel { RelativePath = buf.RelativePath };
                    Tabs.Add(tab);
                }
                tab.IsActive = string.Equals(buf.RelativePath, _workspace.ActivePath, StringComparison.OrdinalIgnoreCase);
                tab.IsDirty = buf.IsDirty;
            }

            OnPropertyChanged(nameof(ActivePath));
            OnPropertyChanged(nameof(HasTabs));
            OnPropertyChanged(nameof(HasNoTabs));
            UpdateActiveDirty();
        }

        private void OnBufferChanged(object? sender, EventArgs e)
        {
            if (sender is not LuaScriptWorkspace.ScriptBuffer buf)
                return;
            foreach (var t in Tabs)
            {
                if (string.Equals(t.RelativePath, buf.RelativePath, StringComparison.OrdinalIgnoreCase))
                {
                    t.IsDirty = buf.IsDirty;
                    break;
                }
            }
            UpdateActiveDirty();
        }

        private void UpdateActiveDirty()
        {
            bool dirty = false;
            string? active = _workspace.ActivePath;
            if (!string.IsNullOrEmpty(active))
            {
                var buf = FindBuffer(active);
                if (buf is not null)
                    dirty = buf.IsDirty;
            }
            IsActiveDirty = dirty;
        }

        // Bağlı obje listesini seçili düğümden tazeler.
        public void RefreshBoundObjects()
        {
            BoundObjects.Clear();
            try
            {
                var node = _main.SelectedNode;
                if (node is not null)
                {
                    foreach (var o in node.Objects)
                        BoundObjects.Add(o);
                }
            }
            catch { }
        }

        private static string Norm(string? p) => (p ?? "").Replace('\\', '/').Trim();

        private void SyncSelectedBoundObject()
        {
            if (_suppressBound)
                return;
            string? active = _workspace.ActivePath?.Replace('\\', '/');
            FrameObjectViewModel? match = null;
            try
            {
                var node = _main.SelectedNode;
                if (active is not null && node is not null)
                {
                    foreach (var obj in node.Objects)
                    {
                        var sc = obj.Components.OfType<ScriptComponentViewModel>().FirstOrDefault();
                        if (sc is not null && string.Equals(Norm(sc.ScriptPath), active, StringComparison.OrdinalIgnoreCase))
                        {
                            match = obj;
                            break;
                        }
                    }
                }
            }
            catch { }
            _suppressBound = true;
            try { SelectedBoundObject = match; }
            finally { _suppressBound = false; }
        }

        // Seçili objeyi etkin betiğe bağlar (yoksa bileşen ekler).
        public void BindObject(FrameObjectViewModel? obj)
        {
            if (obj is null)
                return;
            string? active = _workspace.ActivePath;
            if (string.IsNullOrEmpty(active))
                return;
            string normActive = active.Replace('\\', '/');
            var existing = obj.Components.OfType<ScriptComponentViewModel>().FirstOrDefault();
            if (existing is not null)
            {
                string old = existing.ScriptPath ?? "";
                if (string.Equals(Norm(old), normActive, StringComparison.OrdinalIgnoreCase))
                    return;
                existing.ScriptPath = normActive;
                UndoRedoService.Instance.RecordAction(new ScriptPathChangeAction(existing, old, normActive));
            }
            else
            {
                // Kayıtlı "script" türünden yeni bileşen (prototip bağlama davranışı).
                var comp = new ScriptComponentViewModel { ScriptPath = normActive };
                obj.AddComponent(comp);
                int idx = obj.Components.IndexOf(comp);
                UndoRedoService.Instance.RecordAction(new ComponentAddAction(obj, comp, idx));
            }
            try { _main.ScheduleSave(); } catch { }
            SyncSelectedBoundObject();
        }

        // Kod alanından gelen metni etkin tampona yazar.
        public void UpdateActiveText(string? text)
        {
            if (text is null)
                return;
            string? active = _workspace.ActivePath;
            if (string.IsNullOrEmpty(active))
                return;
            var buf = FindBuffer(active);
            if (buf is null)
                return;
            if (!string.Equals(buf.Text, text, StringComparison.Ordinal))
                buf.Text = text;
        }

        // İmleç konumunu durum çubuğu metnine çevirir.
        public void UpdateCaret(int offset)
        {
            string text = "";
            string? active = _workspace.ActivePath;
            if (!string.IsNullOrEmpty(active))
            {
                var buf = FindBuffer(active);
                if (buf is not null)
                    text = buf.Text ?? "";
            }
            var (line, col) = LuaScriptWorkspace.CaretPosition(text, offset);
            string fmt = EditorStrings.T("Lua.Status.Caret", "Satır {0} · Sütun {1}", "Line {0} · Column {1}");
            try { StatusCaret = string.Format(fmt, line, col); }
            catch { StatusCaret = line + " " + col; }
        }

        public void Save(string? path)
        {
            if (string.IsNullOrEmpty(path))
                return;
            try
            {
                _workspace.Save(path);
                RefreshFiles();
                RebuildTabs();
                LastToast = EditorStrings.T("Lua.Saved", "Betik kaydedildi.", "Script saved.");
                ToastRequested?.Invoke(this, LastToast);
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                ErrorRequested?.Invoke(this, LastError);
            }
        }

        [RelayCommand]
        private void SaveActive()
        {
            Save(_workspace.ActivePath);
        }

        // Yeni betik dosyası oluşturur ve etkin yapar.
        public string CreateNewFile()
        {
            string initial = EditorStrings.T("Lua.New.Content", "-- Yeni bir hikâye\n\n", "-- New story\n\n");
            string path = _workspace.CreateNew(initial);
            RefreshFiles();
            RebuildTabs();
            SyncSelectedBoundObject();
            return path;
        }

        [RelayCommand]
        private void CreateNew()
        {
            CreateNewFile();
        }

        [RelayCommand]
        private void OpenSearch()
        {
            SearchRequested?.Invoke(this, EventArgs.Empty);
        }

        [RelayCommand]
        public void OpenFile(string? path)
        {
            if (string.IsNullOrEmpty(path))
                return;
            try
            {
                _workspace.Open(path);
                _workspace.Activate(path);
                RefreshFiles();
                RebuildTabs();
                SyncSelectedBoundObject();
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                ErrorRequested?.Invoke(this, LastError);
            }
        }

        [RelayCommand]
        private void ActivateTab(string? path)
        {
            if (string.IsNullOrEmpty(path))
                return;
            try
            {
                _workspace.Activate(path);
                RebuildTabs();
                SyncSelectedBoundObject();
            }
            catch { }
        }

        [RelayCommand]
        public void CloseTab(string? path)
        {
            if (string.IsNullOrEmpty(path))
                return;
            var buf = FindBuffer(path);
            if (buf is null)
                return;
            if (buf.IsDirty)
            {
                PendingClosePath = buf.RelativePath;
                IsCloseConfirmVisible = true;
                return;
            }
            _workspace.Close(buf.RelativePath, true);
            RefreshFiles();
            RebuildTabs();
            SyncSelectedBoundObject();
        }

        [RelayCommand]
        private void ConfirmCloseSave()
        {
            string? p = PendingClosePath;
            if (string.IsNullOrEmpty(p))
            {
                IsCloseConfirmVisible = false;
                return;
            }
            try { _workspace.Save(p); }
            catch (Exception ex)
            {
                LastError = ex.Message;
                ErrorRequested?.Invoke(this, LastError);
                return;
            }
            _workspace.Close(p, true);
            PendingClosePath = null;
            IsCloseConfirmVisible = false;
            RefreshFiles();
            RebuildTabs();
        }

        [RelayCommand]
        private void ConfirmCloseDiscard()
        {
            string? p = PendingClosePath;
            if (string.IsNullOrEmpty(p))
            {
                IsCloseConfirmVisible = false;
                return;
            }
            _workspace.Close(p, true);
            PendingClosePath = null;
            IsCloseConfirmVisible = false;
            RefreshFiles();
            RebuildTabs();
        }

        [RelayCommand]
        private void ConfirmCloseCancel()
        {
            PendingClosePath = null;
            IsCloseConfirmVisible = false;
        }

        // Panel odağa dönünce dış değişiklikleri denetler.
        public void CheckExternalChanges()
        {
            bool banner = false;
            string? bannerPath = null;
            foreach (var buf in _workspace.OpenBuffers.ToList())
            {
                bool ext;
                try { ext = _workspace.HasExternalChange(buf.RelativePath); }
                catch { continue; }
                if (!ext)
                    continue;
                if (!buf.IsDirty)
                {
                    try { _workspace.Reload(buf.RelativePath); } catch { }
                }
                else if (string.Equals(buf.RelativePath, _workspace.ActivePath, StringComparison.OrdinalIgnoreCase))
                {
                    banner = true;
                    bannerPath = buf.RelativePath;
                }
            }
            RefreshFiles();
            RebuildTabs();
            if (banner)
            {
                ExternalChangePath = bannerPath;
                IsExternalBannerVisible = true;
            }
            else if (ExternalChangePath is null || FindBuffer(ExternalChangePath) is null || !FindBuffer(ExternalChangePath)!.IsDirty)
            {
                // Dış değişiklik kalmadıysa şeridi kapat.
                if (!banner)
                {
                    IsExternalBannerVisible = false;
                    ExternalChangePath = null;
                }
            }
        }

        [RelayCommand]
        private void ReloadExternal()
        {
            string? p = ExternalChangePath;
            if (string.IsNullOrEmpty(p))
            {
                IsExternalBannerVisible = false;
                return;
            }
            try
            {
                _workspace.Close(p, true);
                _workspace.Open(p);
                _workspace.Activate(p);
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                ErrorRequested?.Invoke(this, LastError);
            }
            ExternalChangePath = null;
            IsExternalBannerVisible = false;
            RefreshFiles();
            RebuildTabs();
        }

        [RelayCommand]
        private void DismissExternal()
        {
            ExternalChangePath = null;
            IsExternalBannerVisible = false;
        }
    }
}
