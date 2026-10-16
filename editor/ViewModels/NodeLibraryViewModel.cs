using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services.NodeLibrary;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels;

// Kaynak seçimi (üstteki iki dilimli anahtar).
public enum LibrarySource
{
    Builtin,
    User
}

// Panel satır türü.
public enum LibraryRowKind
{
    Package,
    BuiltinNode,
    Item
}

// Klasör ağacı düğümü (TreeView satırı).
public partial class LibraryFolderNodeViewModel : ObservableObject
{
    public string? FolderId { get; }

    [ObservableProperty]
    private string _name = string.Empty;

    [ObservableProperty]
    private int _itemCount;

    [ObservableProperty]
    private bool _isSelected;

    [ObservableProperty]
    private bool _isExpanded;

    public ObservableCollection<LibraryFolderNodeViewModel> Children { get; } = new();

    public IRelayCommand SelectCommand { get; }

    public IRelayCommand ManageCommand { get; }

    public IRelayCommand RemoveCommand { get; }

    public bool CanManage => FolderId is not null;

    private readonly Action<string?, bool>? _expandedChanged;

    public LibraryFolderNodeViewModel(
        string? folderId,
        string name,
        Action<string?> select,
        Action<string> manage,
        Action<string> remove,
        Action<string?, bool>? expandedChanged)
    {
        FolderId = folderId;
        _name = name;
        _expandedChanged = expandedChanged;
        SelectCommand = new RelayCommand(() => select(FolderId));
        ManageCommand = new RelayCommand(() => { if (FolderId is not null) manage(FolderId); });
        RemoveCommand = new RelayCommand(() => { if (FolderId is not null) remove(FolderId); });
    }

    partial void OnIsExpandedChanged(bool value)
    {
        _expandedChanged?.Invoke(FolderId, value);
    }
}

// Ekmek kırıntısı dilimi.
public sealed class LibraryCrumbViewModel
{
    public string Title { get; }

    public bool IsNavigable { get; }

    public IRelayCommand NavigateCommand { get; }

    public LibraryCrumbViewModel(string title, bool isNavigable, IRelayCommand navigateCommand)
    {
        Title = title;
        IsNavigable = isNavigable;
        NavigateCommand = navigateCommand;
    }
}

// Klasör seçeneği (yönet formundaki ComboBox).
public sealed class LibraryParentOptionViewModel
{
    public string? FolderId { get; }

    public string Title { get; }

    public LibraryParentOptionViewModel(string? folderId, string title)
    {
        FolderId = folderId;
        Title = title;
    }
}

// Taşıma hedefi (satır "…" alt menüsü).
public sealed class LibraryMoveTargetViewModel
{
    public string? FolderId { get; }

    public string Title { get; }

    public IRelayCommand MoveCommand { get; }

    public LibraryMoveTargetViewModel(string? folderId, string title, IRelayCommand moveCommand)
    {
        FolderId = folderId;
        Title = title;
        MoveCommand = moveCommand;
    }
}

// Panel satırı (paket / hazır düğüm / kullanıcı kaydı).
public sealed class LibraryRowViewModel
{
    public LibraryRowKind Kind { get; }

    public string Title { get; }

    public string Summary { get; }

    public string? ItemId { get; }

    public bool CanMove { get; }

    public bool CanRemove { get; }

    public IRelayCommand ActivateCommand { get; }

    public IRelayCommand RemoveCommand { get; }

    public ObservableCollection<LibraryMoveTargetViewModel> MoveTargets { get; } = new();

    public LibraryRowViewModel(
        LibraryRowKind kind,
        string title,
        string summary,
        string? itemId,
        bool canMove,
        bool canRemove,
        IRelayCommand activateCommand,
        IRelayCommand removeCommand)
    {
        Kind = kind;
        Title = title;
        Summary = summary;
        ItemId = itemId;
        CanMove = canMove;
        CanRemove = canRemove;
        ActivateCommand = activateCommand;
        RemoveCommand = removeCommand;
    }
}

// Faz 7 D3 — düğüm kütüphanesi panel + Inspector bölümü görünüm modeli.
public partial class NodeLibraryViewModel : ObservableObject, IDisposable
{
    private readonly MainWindowViewModel _main;
    private readonly string _path;
    private Func<DateTimeOffset> _clock;
    private readonly HashSet<string> _expandedFolderIds = new(StringComparer.Ordinal);
    private NodeViewModel? _hookedNode;
    private bool _disposed;

    [ObservableProperty]
    private NodeLibraryState _state = NodeLibraryState.Empty;

    [ObservableProperty]
    private LibrarySource _source = LibrarySource.Builtin;

    [ObservableProperty]
    private string? _currentFolderId;

    [ObservableProperty]
    private string _query = string.Empty;

    [ObservableProperty]
    private bool _isPackageOpen;

    [ObservableProperty]
    private bool _isReadOnly;

    [ObservableProperty]
    private string? _loadWarning;

    [ObservableProperty]
    private bool _isNewFolderFormOpen;

    [ObservableProperty]
    private string _newFolderName = string.Empty;

    [ObservableProperty]
    private bool _isManageFolderFormOpen;

    [ObservableProperty]
    private string? _manageFolderId;

    [ObservableProperty]
    private string _manageFolderName = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SelectedManageParentOption))]
    private string? _manageFolderParentId;

    /// <summary>Yönet formundaki üst klasör seçimi (ComboBox SelectedItem); kaynak <see cref="ManageFolderParentId"/>.</summary>
    public LibraryParentOptionViewModel? SelectedManageParentOption
    {
        get => ManageFolderParentOptions.FirstOrDefault(o => o.FolderId == ManageFolderParentId);
        set { if (value is not null) ManageFolderParentId = value.FolderId; }
    }

    [ObservableProperty]
    private bool _isRemoveFolderConfirmOpen;

    [ObservableProperty]
    private string? _pendingRemoveFolderId;

    [ObservableProperty]
    private string _pendingRemoveFolderName = string.Empty;

    [ObservableProperty]
    private bool _isRemoveItemConfirmOpen;

    [ObservableProperty]
    private string? _pendingRemoveItemId;

    [ObservableProperty]
    private string _pendingRemoveItemTitle = string.Empty;

    [ObservableProperty]
    private bool _hasSelectedNode;

    [ObservableProperty]
    private bool _isSelectedNodeSaved;

    [ObservableProperty]
    private string? _selectedNodeRecordHint;

    [ObservableProperty]
    private string _description = string.Empty;

    [ObservableProperty]
    private string _emptyMessage = string.Empty;

    [ObservableProperty]
    private bool _isEmpty = true;

    public ObservableCollection<LibraryFolderNodeViewModel> FolderTree { get; } = new();

    public ObservableCollection<LibraryCrumbViewModel> Crumbs { get; } = new();

    public ObservableCollection<LibraryRowViewModel> Rows { get; } = new();

    public ObservableCollection<LibraryParentOptionViewModel> ManageFolderParentOptions { get; } = new();

    // Kütüphane dosya yolu (testler için salt okunur).
    public string LibraryPath => _path;

    // Saat (testler sabit zaman verir).
    public Func<DateTimeOffset> Clock
    {
        get => _clock;
        set => _clock = value ?? (() => DateTimeOffset.UtcNow);
    }

    public bool IsUserSource => Source == LibrarySource.User;

    public bool IsBuiltinSource => Source == LibrarySource.Builtin;

    public bool HasRows => !IsEmpty;

    public NodeLibraryViewModel(MainWindowViewModel main, string? libraryPath = null, Func<DateTimeOffset>? clock = null)
    {
        _main = main ?? throw new ArgumentNullException(nameof(main));
        _path = libraryPath ?? main.NodeLibraryPath;
        _clock = clock ?? (() => DateTimeOffset.UtcNow);

        NodeLibraryLoadResult result;
        try
        {
            result = NodeLibraryStore.Load(_path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            result = new NodeLibraryLoadResult(NodeLibraryState.Empty, false, ex.Message);
        }

        _state = result.State;
        _isReadOnly = result.ReadOnly;
        _loadWarning = result.Warning;
        if (!string.IsNullOrEmpty(_loadWarning))
        {
            try { _main.AppendLog("[Library] " + _loadWarning); } catch { }
        }

        _main.PropertyChanged += OnMainPropertyChanged;
        HookSelectedNode(_main.SelectedNode);
        RefreshPanel();
        RefreshInspector();
    }

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;
        try { _main.PropertyChanged -= OnMainPropertyChanged; } catch { }
        UnhookSelectedNode();
        GC.SuppressFinalize(this);
    }

    // Ana seçim değişince Inspector'ı tazele.
    private void OnMainPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
        {
            HookSelectedNode(_main.SelectedNode);
            RefreshInspector();
        }
    }

    private void HookSelectedNode(NodeViewModel? node)
    {
        UnhookSelectedNode();
        _hookedNode = node;
        if (_hookedNode is not null)
        {
            try { _hookedNode.PropertyChanged += OnHookedNodePropertyChanged; } catch { }
        }
    }

    private void UnhookSelectedNode()
    {
        if (_hookedNode is not null)
        {
            try { _hookedNode.PropertyChanged -= OnHookedNodePropertyChanged; } catch { }
            _hookedNode = null;
        }
    }

    private void OnHookedNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(NodeViewModel.Title))
            RefreshInspector();
    }

    partial void OnSourceChanged(LibrarySource value)
    {
        _currentFolderId = null;
        _isPackageOpen = false;
        _query = string.Empty;
        OnPropertyChanged(nameof(CurrentFolderId));
        OnPropertyChanged(nameof(Query));
        OnPropertyChanged(nameof(IsPackageOpen));
        OnPropertyChanged(nameof(IsUserSource));
        OnPropertyChanged(nameof(IsBuiltinSource));
        CloseAllForms();
        RefreshPanel();
    }

    partial void OnCurrentFolderIdChanged(string? value)
    {
        if (value is not null)
        {
            foreach (LibraryFolder f in NodeLibraryOperations.PathTo(State, value))
                _expandedFolderIds.Add(f.Id);
        }
        RefreshPanel();
    }

    partial void OnQueryChanged(string value)
    {
        RefreshRows();
    }

    partial void OnIsPackageOpenChanged(bool value)
    {
        RefreshPanel();
    }

    partial void OnStateChanged(NodeLibraryState value)
    {
        RefreshPanel();
        RefreshInspector();
    }

    partial void OnIsEmptyChanged(bool value)
    {
        OnPropertyChanged(nameof(HasRows));
    }

    // Kaynak düğmeleri.
    [RelayCommand]
    public void ShowBuiltin()
    {
        Source = LibrarySource.Builtin;
    }

    [RelayCommand]
    public void ShowUser()
    {
        Source = LibrarySource.User;
    }

    // Klasör seçimi (null = kök).
    [RelayCommand]
    public void SelectFolder(string? id)
    {
        string? norm = id;
        if (norm is not null && !State.Folders.Any(f => f.Id == norm))
            norm = null;
        CurrentFolderId = norm;
        if (!string.IsNullOrEmpty(Query))
            Query = string.Empty;
    }

    [RelayCommand]
    public void NavigateRoot()
    {
        CurrentFolderId = null;
        IsPackageOpen = false;
        if (!string.IsNullOrEmpty(Query))
            Query = string.Empty;
    }

    // Hazır paket aç/kapat.
    [RelayCommand]
    public void OpenPackage()
    {
        IsPackageOpen = true;
        if (!string.IsNullOrEmpty(Query))
            Query = string.Empty;
    }

    [RelayCommand]
    public void ClosePackage()
    {
        IsPackageOpen = false;
        if (!string.IsNullOrEmpty(Query))
            Query = string.Empty;
    }

    // Yeni klasör formu.
    [RelayCommand]
    public void ShowNewFolderForm()
    {
        NewFolderName = string.Empty;
        IsNewFolderFormOpen = true;
    }

    [RelayCommand]
    public void CancelNewFolderForm()
    {
        IsNewFolderFormOpen = false;
        NewFolderName = string.Empty;
    }

    [RelayCommand]
    public void ConfirmNewFolder()
    {
        string name = NewFolderName ?? string.Empty;
        string? parent = CurrentFolderId;
        NodeLibraryState before = State;
        if (!CreateFolder(name, parent))
            return;
        foreach (LibraryFolder f in State.Folders)
        {
            bool existed = false;
            foreach (LibraryFolder b in before.Folders)
            {
                if (b.Id == f.Id)
                {
                    existed = true;
                    break;
                }
            }
            if (!existed)
            {
                CurrentFolderId = f.Id;
                break;
            }
        }
        IsNewFolderFormOpen = false;
        NewFolderName = string.Empty;
    }

    // Klasör oluşturur (testler doğrudan çağırır).
    public bool CreateFolder(string name, string? parentId)
    {
        return ApplyOperation(
            s => NodeLibraryOperations.CreateFolder(s, name, parentId),
            "Library.Toast.FolderCreated", "Klasör oluşturuldu.", "Folder created.");
    }

    // Klasör yönet formu.
    [RelayCommand]
    public void BeginManageFolder(string? folderId)
    {
        if (string.IsNullOrEmpty(folderId))
            return;
        LibraryFolder? f = State.Folders.FirstOrDefault(x => x.Id == folderId);
        if (f is null)
            return;
        ManageFolderId = f.Id;
        ManageFolderName = f.Name;
        RebuildManageParentOptions(f.Id);
        ManageFolderParentId = f.ParentId;
        OnPropertyChanged(nameof(SelectedManageParentOption));
        IsManageFolderFormOpen = true;
        IsRemoveFolderConfirmOpen = false;
    }

    [RelayCommand]
    public void CancelManageFolder()
    {
        IsManageFolderFormOpen = false;
        ManageFolderId = null;
    }

    [RelayCommand]
    public void ConfirmManageFolder()
    {
        if (string.IsNullOrEmpty(ManageFolderId))
            return;
        if (!MoveFolder(ManageFolderId, ManageFolderParentId, ManageFolderName ?? string.Empty))
            return;
        IsManageFolderFormOpen = false;
        ManageFolderId = null;
    }

    // Klasörü yeniden adlandırır ve taşır.
    public bool MoveFolder(string id, string? parentId, string name)
    {
        return ApplyOperation(
            s => NodeLibraryOperations.MoveFolder(s, id, parentId, name),
            "Library.Toast.FolderUpdated", "Klasör güncellendi.", "Folder updated.");
    }

    // Klasör kaldırma onayı.
    [RelayCommand]
    public void BeginRemoveFolder(string? folderId)
    {
        if (string.IsNullOrEmpty(folderId))
            return;
        LibraryFolder? f = State.Folders.FirstOrDefault(x => x.Id == folderId);
        if (f is null)
            return;
        PendingRemoveFolderId = f.Id;
        PendingRemoveFolderName = f.Name;
        IsRemoveFolderConfirmOpen = true;
    }

    [RelayCommand]
    public void CancelRemoveFolder()
    {
        IsRemoveFolderConfirmOpen = false;
        PendingRemoveFolderId = null;
    }

    [RelayCommand]
    public void ConfirmRemoveFolder()
    {
        if (string.IsNullOrEmpty(PendingRemoveFolderId))
            return;
        string id = PendingRemoveFolderId;
        if (!RemoveFolder(id))
            return;
        IsRemoveFolderConfirmOpen = false;
        PendingRemoveFolderId = null;
    }

    // Klasörü kaldırır; içindekiler üst klasöre taşınır.
    public bool RemoveFolder(string id)
    {
        string? parentBefore = State.Folders.FirstOrDefault(f => f.Id == id)?.ParentId;
        bool wasCurrent = string.Equals(CurrentFolderId, id, StringComparison.Ordinal);
        bool ok = ApplyOperation(
            s => NodeLibraryOperations.RemoveFolder(s, id),
            "Library.Toast.FolderRemoved", "Klasör kaldırıldı.", "Folder removed.");
        if (!ok)
            return false;
        _expandedFolderIds.Remove(id);
        if (string.Equals(ManageFolderId, id, StringComparison.Ordinal))
        {
            IsManageFolderFormOpen = false;
            ManageFolderId = null;
        }
        if (wasCurrent)
        {
            string? target = parentBefore;
            if (target is not null && !State.Folders.Any(f => f.Id == target))
                target = null;
            CurrentFolderId = target;
        }
        return true;
    }

    // Kayıt kaldırma onayı.
    [RelayCommand]
    public void BeginRemoveItem(string? itemId)
    {
        if (string.IsNullOrEmpty(itemId))
            return;
        LibraryItem? item = State.Items.FirstOrDefault(x => x.Id == itemId);
        if (item is null)
            return;
        PendingRemoveItemId = item.Id;
        PendingRemoveItemTitle = item.Template.Title;
        IsRemoveItemConfirmOpen = true;
    }

    [RelayCommand]
    public void CancelRemoveItem()
    {
        IsRemoveItemConfirmOpen = false;
        PendingRemoveItemId = null;
    }

    [RelayCommand]
    public void ConfirmRemoveItem()
    {
        if (string.IsNullOrEmpty(PendingRemoveItemId))
            return;
        string id = PendingRemoveItemId;
        if (!RemoveItem(id))
            return;
        IsRemoveItemConfirmOpen = false;
        PendingRemoveItemId = null;
    }

    // Kaydı siler.
    public bool RemoveItem(string itemId)
    {
        if (!State.Items.Any(i => i.Id == itemId))
            return true;
        return ApplyOperation(
            s => NodeLibraryOperations.RemoveItem(s, itemId),
            "Library.Toast.Removed", "Kütüphane kaydı kaldırıldı.", "Library record removed.");
    }

    // Kaydı başka klasöre taşır.
    public bool MoveItem(string itemId, string? folderId)
    {
        if (folderId is not null && !State.Folders.Any(f => f.Id == folderId))
        {
            string msg;
            try { msg = EditorStrings.T("Library.Error.ParentNotFound"); }
            catch { msg = "Parent folder not found."; }
            try { _main.Toast.Show(msg, ToastType.Warning); } catch { }
            return false;
        }
        if (!State.Items.Any(i => i.Id == itemId))
            return false;
        return ApplyOperation(
            s => NodeLibraryOperations.MoveItem(s, itemId, folderId),
            "Library.Toast.ItemMoved", "Kayıt taşındı.", "Record moved.");
    }

    // Satır eylemi.
    public void ActivateRow(LibraryRowViewModel? row)
    {
        if (row is null)
            return;
        switch (row.Kind)
        {
            case LibraryRowKind.Package:
                OpenPackage();
                break;
            case LibraryRowKind.BuiltinNode:
                AddBuiltinToScene();
                break;
            case LibraryRowKind.Item:
                if (row.ItemId is not null)
                    AddItemToScene(row.ItemId);
                break;
        }
    }

    // Kaydı sahneye ekler.
    public void AddItemToScene(string itemId)
    {
        LibraryItem? found = State.Items.FirstOrDefault(i => i.Id == itemId);
        if (found is null)
        {
            string msg;
            try { msg = EditorStrings.T("Library.Error.ItemNotFound"); }
            catch { msg = "Library item not found."; }
            try { _main.Toast.Show(msg, ToastType.Warning); } catch { }
            return;
        }
        try
        {
            _main.AddNodeFromTemplate(found.Template.DeepCopy());
            ShowSuccess("Library.Toast.Added", "Düğüm sahneye eklendi.", "Node added to the scene.");
        }
        catch (Exception ex)
        {
            try { _main.AppendLog("[Library] Add failed: " + ex.Message); } catch { }
        }
    }

    // Hazır düğümü sahneye ekler.
    public void AddBuiltinToScene()
    {
        try
        {
            _main.AddNode();
            ShowSuccess("Library.Toast.Added", "Düğüm sahneye eklendi.", "Node added to the scene.");
        }
        catch (Exception ex)
        {
            try { _main.AppendLog("[Library] Add failed: " + ex.Message); } catch { }
        }
    }

    // Inspector yıldızı (kaydet / kaldır).
    [RelayCommand]
    public void ToggleSaveSelectedNode()
    {
        NodeViewModel? node = _main.SelectedNode;
        if (node is null)
            return;
        string key = NodeTemplateConverter.SourceKey(_main.ProjectLibraryKey, node.Id);
        LibraryItem? existing = NodeLibraryOperations.FindBySource(State, key);
        if (existing is not null)
        {
            RemoveItem(existing.Id);
            return;
        }
        string? folderId = Source == LibrarySource.User ? CurrentFolderId : null;
        if (folderId is not null && !State.Folders.Any(f => f.Id == folderId))
            folderId = null;
        NodeTemplate template;
        try
        {
            template = NodeTemplateConverter.Capture(node);
        }
        catch (Exception ex)
        {
            try { _main.AppendLog("[Library] Capture failed: " + ex.Message); } catch { }
            return;
        }
        ApplyOperation(
            s => NodeLibraryOperations.Save(s, key, template, folderId, _clock()),
            "Library.Toast.Saved", "Düğüm kütüphaneye kaydedildi.", "Node saved to the library.");
    }

    // Kaydı güncel düğümle değiştirir.
    [RelayCommand]
    public void UpdateSelectedNodeRecord()
    {
        NodeViewModel? node = _main.SelectedNode;
        if (node is null)
            return;
        string key = NodeTemplateConverter.SourceKey(_main.ProjectLibraryKey, node.Id);
        LibraryItem? existing = NodeLibraryOperations.FindBySource(State, key);
        if (existing is null)
            return;
        NodeTemplate template;
        try
        {
            template = NodeTemplateConverter.Capture(node);
        }
        catch (Exception ex)
        {
            try { _main.AppendLog("[Library] Capture failed: " + ex.Message); } catch { }
            return;
        }
        ApplyOperation(
            s => NodeLibraryOperations.Update(s, existing.Id, template, _clock()),
            "Library.Toast.Updated", "Kütüphane kaydı güncellendi.", "Library record updated.");
    }

    // Saf işlemi uygular, hatada eski durumu korur. Salt okunur kütüphanede (daha yeni bir editörün
    // yazdığı dosya) hiçbir değişiklik uygulanmaz: bellekte değişip dosyaya yazılmayan bir kayıt,
    // yeniden açılışta sessizce kaybolurdu.
    private bool ApplyOperation(Func<NodeLibraryState, NodeLibraryState> op, string? successKey, string successTr, string successEn)
    {
        if (IsReadOnly)
        {
            string roMsg = EditorStrings.T("Library.Toast.ReadOnly", "Salt okunur kütüphane; değişiklik yapılamaz.", "Read-only library; changes are not allowed.");
            try { _main.Toast.Show(roMsg, ToastType.Warning); } catch { }
            try { _main.AppendLog("[Library] " + roMsg); } catch { }
            return false;
        }
        NodeLibraryState before = State;
        NodeLibraryState next;
        try
        {
            next = op(before);
        }
        catch (NodeLibraryException ex)
        {
            string errKey = "Library.Error." + ex.Error.ToString();
            string errMsg;
            try { errMsg = EditorStrings.T(errKey, ex.Message, ex.Message); }
            catch { errMsg = ex.Message; }
            try { _main.Toast.Show(errMsg, ToastType.Warning); } catch { }
            return false;
        }
        State = next;
        {
            try
            {
                NodeLibraryStore.Save(_path, State);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidOperationException)
            {
                string failMsg;
                try { failMsg = EditorStrings.T("Library.Toast.SaveFailed", "Kütüphane dosyasına yazılamadı.", "Could not write the library file."); }
                catch { failMsg = ex.Message; }
                try { _main.Toast.Show(failMsg, ToastType.Error); } catch { }
                try { _main.AppendLog("[Library] Save failed: " + ex.Message); } catch { }
            }
        }
        if (!string.IsNullOrEmpty(successKey))
            ShowSuccess(successKey, successTr, successEn);
        RefreshPanel();
        RefreshInspector();
        return true;
    }

    private void ShowSuccess(string key, string tr, string en)
    {
        string msg;
        try { msg = EditorStrings.T(key, tr, en); }
        catch { msg = tr; }
        try { _main.Toast.Show(msg, ToastType.Success); } catch { }
    }

    private void CloseAllForms()
    {
        IsNewFolderFormOpen = false;
        IsManageFolderFormOpen = false;
        IsRemoveFolderConfirmOpen = false;
        IsRemoveItemConfirmOpen = false;
    }

    // Kök + yol görünen adı.
    private string FolderPath(string? folderId)
    {
        if (folderId is null)
            return string.Empty;
        IReadOnlyList<LibraryFolder> chain = NodeLibraryOperations.PathTo(State, folderId);
        return string.Join(" / ", chain.Select(f => f.Name));
    }

    private string FolderDisplayPath(string? folderId)
    {
        string root;
        try { root = EditorStrings.T("Library.Source.User", "Benim düğümlerim", "My nodes"); }
        catch { root = "Benim düğümlerim"; }
        string inner = FolderPath(folderId);
        return string.IsNullOrEmpty(inner) ? root : root + " / " + inner;
    }

    private void OnNodeExpanded(string? id, bool expanded)
    {
        if (id is null)
            return;
        if (expanded)
            _expandedFolderIds.Add(id);
        else
            _expandedFolderIds.Remove(id);
    }

    private void RebuildManageParentOptions(string excludeId)
    {
        ManageFolderParentOptions.Clear();
        string root;
        try { root = EditorStrings.T("Library.Source.User", "Benim düğümlerim", "My nodes"); }
        catch { root = "Benim düğümlerim"; }
        ManageFolderParentOptions.Add(new LibraryParentOptionViewModel(null, root));
        var childrenMap = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        foreach (LibraryFolder fld in State.Folders)
        {
            if (fld.ParentId is null)
                continue;
            if (!childrenMap.TryGetValue(fld.ParentId, out List<string>? list))
            {
                list = new List<string>();
                childrenMap[fld.ParentId] = list;
            }
            list.Add(fld.Id);
        }
        var excluded = new HashSet<string>(StringComparer.Ordinal) { excludeId };
        var queue = new Queue<string>();
        queue.Enqueue(excludeId);
        while (queue.Count > 0)
        {
            string cur = queue.Dequeue();
            if (!childrenMap.TryGetValue(cur, out List<string>? kids))
                continue;
            foreach (string k in kids)
            {
                if (excluded.Add(k))
                    queue.Enqueue(k);
            }
        }
        StringComparer trComparer = StringComparer.Create(CultureInfo.GetCultureInfo("tr-TR"), ignoreCase: true);
        List<LibraryFolder> candidates = State.Folders
            .Where(x => !excluded.Contains(x.Id))
            .OrderBy(x => FolderPath(x.Id), trComparer)
            .ToList();
        foreach (LibraryFolder c in candidates)
            ManageFolderParentOptions.Add(new LibraryParentOptionViewModel(c.Id, FolderDisplayPath(c.Id)));
    }

    private void RefreshPanel()
    {
        if (_currentFolderId is not null && !State.Folders.Any(f => f.Id == _currentFolderId))
        {
            _currentFolderId = null;
            OnPropertyChanged(nameof(CurrentFolderId));
        }
        if (_currentFolderId is not null)
        {
            foreach (LibraryFolder f in NodeLibraryOperations.PathTo(State, _currentFolderId))
                _expandedFolderIds.Add(f.Id);
        }
        RefreshFolderTree();
        RefreshCrumbs();
        RefreshDescription();
        RefreshRows();
        OnPropertyChanged(nameof(IsUserSource));
        OnPropertyChanged(nameof(IsBuiltinSource));
    }

    private void RefreshFolderTree()
    {
        string rootName;
        try { rootName = EditorStrings.T("Library.Source.User", "Benim düğümlerim", "My nodes"); }
        catch { rootName = "Benim düğümlerim"; }
        int rootCount = State.Items.Count(i => i.FolderId is null);
        var byParent = new Dictionary<string, List<LibraryFolder>>(StringComparer.Ordinal);
        var tops = new List<LibraryFolder>();
        foreach (LibraryFolder f in State.Folders)
        {
            if (f.ParentId is null)
                tops.Add(f);
            else
            {
                if (!byParent.TryGetValue(f.ParentId, out List<LibraryFolder>? list))
                {
                    list = new List<LibraryFolder>();
                    byParent[f.ParentId] = list;
                }
                list.Add(f);
            }
        }
        StringComparer trComparer = StringComparer.Create(CultureInfo.GetCultureInfo("tr-TR"), ignoreCase: true);
        tops.Sort((a, b) => trComparer.Compare(a.Name, b.Name));
        foreach (List<LibraryFolder> list in byParent.Values)
            list.Sort((a, b) => trComparer.Compare(a.Name, b.Name));

        LibraryFolderNodeViewModel Build(LibraryFolder f)
        {
            var vm = new LibraryFolderNodeViewModel(
                f.Id,
                f.Name,
                id => SelectFolder(id),
                id => BeginManageFolder(id),
                id => BeginRemoveFolder(id),
                (id, exp) => OnNodeExpanded(id, exp));
            vm.ItemCount = State.Items.Count(i => i.FolderId == f.Id);
            vm.IsSelected = string.Equals(_currentFolderId, f.Id, StringComparison.Ordinal);
            vm.IsExpanded = _expandedFolderIds.Contains(f.Id);
            if (byParent.TryGetValue(f.Id, out List<LibraryFolder>? kids))
            {
                foreach (LibraryFolder k in kids)
                    vm.Children.Add(Build(k));
            }
            return vm;
        }

        FolderTree.Clear();
        var root = new LibraryFolderNodeViewModel(
            null,
            rootName,
            id => SelectFolder(id),
            id => BeginManageFolder(id),
            id => BeginRemoveFolder(id),
            (id, exp) => { });
        root.ItemCount = rootCount;
        root.IsSelected = _currentFolderId is null;
        root.IsExpanded = true;
        foreach (LibraryFolder t in tops)
            root.Children.Add(Build(t));
        FolderTree.Add(root);
    }

    private void RefreshCrumbs()
    {
        Crumbs.Clear();
        if (Source == LibrarySource.Builtin)
        {
            string root;
            try { root = EditorStrings.T("Library.Source.Builtin", "Hazır paketler", "Ready-made packages"); }
            catch { root = "Hazır paketler"; }
            Crumbs.Add(new LibraryCrumbViewModel(root, true, new RelayCommand(() => ClosePackage())));
            if (IsPackageOpen)
            {
                string pkg;
                try { pkg = EditorStrings.T("Library.Package.Name", "Rowl", "Rowl"); }
                catch { pkg = "Rowl"; }
                Crumbs.Add(new LibraryCrumbViewModel(pkg, false, new RelayCommand(() => { })));
            }
            return;
        }
        string userRoot;
        try { userRoot = EditorStrings.T("Library.Source.User", "Benim düğümlerim", "My nodes"); }
        catch { userRoot = "Benim düğümlerim"; }
        Crumbs.Add(new LibraryCrumbViewModel(userRoot, true, new RelayCommand(() => SelectFolder(null))));
        foreach (LibraryFolder f in NodeLibraryOperations.PathTo(State, _currentFolderId))
        {
            string fid = f.Id;
            Crumbs.Add(new LibraryCrumbViewModel(f.Name, true, new RelayCommand(() => SelectFolder(fid))));
        }
    }

    private void RefreshDescription()
    {
        try
        {
            Description = Source == LibrarySource.Builtin
                ? EditorStrings.T("Library.Desc.Builtin", "Salt okunur · Sahneye eklenen kopyayı düzenleyebilirsin.", "Read-only · You can edit the copy added to the scene.")
                : EditorStrings.T("Library.Desc.User", "Düğümün tamamını Inspector'daki yıldızla kaydet.", "Save the whole node with the star in the Inspector.");
        }
        catch
        {
            Description = string.Empty;
        }
    }

    private static bool MatchesTr(string name, string query)
    {
        if (string.IsNullOrEmpty(query))
            return true;
        CompareInfo cmp = CultureInfo.GetCultureInfo("tr-TR").CompareInfo;
        return cmp.IndexOf(name ?? string.Empty, query, CompareOptions.IgnoreCase) >= 0;
    }

    private void RefreshRows()
    {
        Rows.Clear();
        string q = (Query ?? string.Empty).Trim();
        StringComparer trComparer = StringComparer.Create(CultureInfo.GetCultureInfo("tr-TR"), ignoreCase: true);

        if (Source == LibrarySource.Builtin)
        {
            string pkgName;
            string pkgSummary;
            string builtinTitle;
            string builtinSummary;
            try
            {
                pkgName = EditorStrings.T("Library.Package.Name", "Rowl", "Rowl");
                pkgSummary = EditorStrings.T("Library.Package.Summary", "Hazır paket · Salt okunur", "Ready-made package · Read-only");
                builtinTitle = EditorStrings.T("Library.Builtin.Title", "Standart düğüm", "Default node");
                builtinSummary = EditorStrings.T("Library.Builtin.Summary", "Mevcut varsayılan düğüm · Ekle", "Current default node · Add");
            }
            catch
            {
                pkgName = "Rowl";
                pkgSummary = string.Empty;
                builtinTitle = string.Empty;
                builtinSummary = string.Empty;
            }
            if (!IsPackageOpen)
            {
                if (MatchesTr(pkgName, q))
                {
                    var row = new LibraryRowViewModel(
                        LibraryRowKind.Package, pkgName, pkgSummary, null,
                        canMove: false, canRemove: false,
                        new RelayCommand(() => OpenPackage()),
                        new RelayCommand(() => { }));
                    Rows.Add(row);
                }
            }
            else if (MatchesTr(builtinTitle, q))
            {
                var row = new LibraryRowViewModel(
                    LibraryRowKind.BuiltinNode, builtinTitle, builtinSummary, null,
                    canMove: false, canRemove: false,
                    new RelayCommand(() => AddBuiltinToScene()),
                    new RelayCommand(() => { }));
                Rows.Add(row);
            }
        }
        else
        {
            string rootName;
            try { rootName = EditorStrings.T("Library.Source.User", "Benim düğümlerim", "My nodes"); }
            catch { rootName = "Benim düğümlerim"; }
            List<LibraryItem> items = State.Items
                .Where(i => i.FolderId == _currentFolderId)
                .Where(i => MatchesTr(i.Template.Title, q))
                .OrderBy(i => i.Template.Title, trComparer)
                .ToList();
            List<LibraryMoveTargetViewModel> targets = BuildMoveTargets(rootName);
            foreach (LibraryItem item in items)
            {
                string itemId = item.Id;
                int objects = item.Template.Objects.Count;
                int comps = 0;
                foreach (TemplateObject o in item.Template.Objects)
                    comps += o.Components.Count;
                string template;
                try { template = EditorStrings.T("Library.Item.Summary", "{0} obje · {1} bileşen · Sahneye ekle", "{0} objects · {1} components · Add to scene"); }
                catch { template = "{0} · {1}"; }
                string summary;
                try { summary = string.Format(CultureInfo.CurrentCulture, template, objects, comps); }
                catch (FormatException) { summary = template; }
                var row = new LibraryRowViewModel(
                    LibraryRowKind.Item, item.Template.Title, summary, itemId,
                    canMove: true, canRemove: true,
                    new RelayCommand(() => AddItemToScene(itemId)),
                    new RelayCommand(() => BeginRemoveItem(itemId)));
                foreach (LibraryMoveTargetViewModel t in targets)
                {
                    string? fid = t.FolderId;
                    row.MoveTargets.Add(new LibraryMoveTargetViewModel(
                        fid, t.Title, new RelayCommand(() => MoveItem(itemId, fid))));
                }
                Rows.Add(row);
            }
        }

        IsEmpty = Rows.Count == 0;
        if (Rows.Count > 0)
        {
            EmptyMessage = string.Empty;
        }
        else if (!string.IsNullOrEmpty(q))
        {
            try { EmptyMessage = EditorStrings.T("Library.Empty.NoMatch", "Bu klasörde eşleşme yok.", "No matches in this folder."); }
            catch { EmptyMessage = string.Empty; }
        }
        else
        {
            try { EmptyMessage = EditorStrings.T("Library.Empty.Folder", "Bu klasörde düğüm yok. Inspector'daki yıldızla buraya kaydet veya bir kaydı bu klasöre taşı.", "No nodes in this folder. Save here with the star in the Inspector or move a record to this folder."); }
            catch { EmptyMessage = string.Empty; }
        }
    }

    private List<LibraryMoveTargetViewModel> BuildMoveTargets(string rootName)
    {
        StringComparer trComparer = StringComparer.Create(CultureInfo.GetCultureInfo("tr-TR"), ignoreCase: true);
        var list = new List<(string? Id, string Title)>
        {
            (null, rootName)
        };
        foreach (LibraryFolder f in State.Folders)
            list.Add((f.Id, FolderDisplayPath(f.Id)));
        list.Sort((a, b) => trComparer.Compare(a.Title, b.Title));
        var result = new List<LibraryMoveTargetViewModel>(list.Count);
        foreach ((string? Id, string Title) entry in list)
        {
            string? fid = entry.Id;
            result.Add(new LibraryMoveTargetViewModel(fid, entry.Title, new RelayCommand(() => { })));
        }
        return result;
    }

    private void RefreshInspector()
    {
        NodeViewModel? node = _main.SelectedNode;
        HasSelectedNode = node is not null;
        if (node is null)
        {
            IsSelectedNodeSaved = false;
            SelectedNodeRecordHint = null;
            return;
        }
        string key = NodeTemplateConverter.SourceKey(_main.ProjectLibraryKey, node.Id);
        LibraryItem? found = NodeLibraryOperations.FindBySource(State, key);
        IsSelectedNodeSaved = found is not null;
        if (found is null)
        {
            SelectedNodeRecordHint = null;
            return;
        }
        string template;
        try { template = EditorStrings.T("Library.Inspector.SavedHint", "Kayıt: {0}. Değişiklikleri kayda aktarmak için Kaydı güncelle'yi kullan.", "Record: {0}. Use Update record to apply changes to the record."); }
        catch { template = "{0}"; }
        string full = FolderDisplayPath(found.FolderId) + " / " + found.Template.Title;
        try { SelectedNodeRecordHint = string.Format(CultureInfo.CurrentCulture, template, full); }
        catch (FormatException) { SelectedNodeRecordHint = full; }
    }
}
