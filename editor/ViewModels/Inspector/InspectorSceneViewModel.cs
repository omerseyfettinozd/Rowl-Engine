using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.IO;
using System.Linq;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.ViewModels.Inspector;

// Denetçi modu.
public enum InspectorMode { Empty, Single, Multi }
// Karakter sahne konumu.
public enum StagePlacement { Left, Center, Right }
// Metin hızı.
public enum TextPace { Slow, Normal, Fast, Instant }
// Arka plan yerleşimi.
public enum BackgroundFit { Fill, Fit }
// Geçiş süresi.
public enum TransitionLength { Short, Medium, Long }
// Kamera hareketi.
public enum CameraMove { Still, In, Out }
// Kamera sarsıntısı.
public enum CameraShake { None, Light, Strong }

/// <summary>Denetçi yazar dili uyarlayıcı katmanı (yalnız ViewModel).</summary>
public sealed partial class InspectorSceneViewModel : ViewModelBase, IDisposable
{
    private readonly MainWindowViewModel _main;
    private NodeViewModel? _currentNode;
    private NodeViewModel? _lastEdited;
    // Yalnız seçeneklerin hedef aldığı düğümler dinlenir (başlık değişince satır tazelenir).
    private readonly HashSet<NodeViewModel> _targetWatch = new();
    private bool _disposed;

    // Önbellekli bölüm referansları.
    private DialogueComponentViewModel? _dialogue;
    private CharacterComponentViewModel? _character;
    private BackgroundComponentViewModel? _background;
    private ChoiceComponentViewModel? _choice;
    private AudioComponentViewModel? _audio;
    private TransitionComponentViewModel? _transition;
    private CameraComponentViewModel? _camera;
    private ChoiceComponentViewModel? _choiceSubscribed;

    public InspectorSceneViewModel(MainWindowViewModel main)
    {
        _main = main ?? throw new ArgumentNullException(nameof(main));
        ChoiceRows = new ObservableCollection<ChoiceRowViewModel>();
        _main.PropertyChanged += OnMainPropertyChanged;
        _main.SelectedNodes.CollectionChanged += OnSelectedNodesChanged;
        _main.Nodes.CollectionChanged += OnMainNodesChanged;
        // Bölüm listesi değişirse özetler tazelenir.
        if (_main.Chapters?.Definitions is INotifyCollectionChanged dcc)
            dcc.CollectionChanged += OnChaptersChanged;
        _currentNode = ResolveSingle();
        SubscribeNode(_currentNode);
        FindSections();
        RebuildChoiceRows();
        if (Mode == InspectorMode.Single && _currentNode is not null)
            _lastEdited = _currentNode;
    }

    // Tekil seçim düğümü (çokluda null).
    private NodeViewModel? ResolveSingle()
    {
        if (_main.SelectedNodes.Count > 1) return null;
        return _main.SelectedNode;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _main.PropertyChanged -= OnMainPropertyChanged;
        _main.SelectedNodes.CollectionChanged -= OnSelectedNodesChanged;
        _main.Nodes.CollectionChanged -= OnMainNodesChanged;
        if (_main.Chapters?.Definitions is INotifyCollectionChanged dcc)
            dcc.CollectionChanged -= OnChaptersChanged;
        UnsubscribeNode(_currentNode);
        DetachRows();
        if (_choiceSubscribed is not null)
            _choiceSubscribed.Options.CollectionChanged -= OnChoiceOptionsChanged;
        _choiceSubscribed = null;
        GC.SuppressFinalize(this);
    }

    // ── Seçim izleme ──

    private void OnMainPropertyChanged(object? s, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (_disposed) return;
        if (e.PropertyName == nameof(MainWindowViewModel.SelectedNode))
            HandleSelectionChanged();
    }

    private void OnSelectedNodesChanged(object? s, NotifyCollectionChangedEventArgs e) => HandleSelectionChanged();
    // Düğüm listesi değişimi (2000 düğümlük toplu yüklemede her ekleme): yalnız ucuz işler.
    private void OnMainNodesChanged(object? s, NotifyCollectionChangedEventArgs e)
    {
        if (_disposed) return;
        if (ChoiceRows.Count > 0)
        {
            foreach (var row in ChoiceRows) row.Refresh();
            WatchTargets();
        }
        OnPropertyChanged(nameof(TargetOptions));
        OnPropertyChanged(nameof(ChapterSummaries));
        OnPropertyChanged(nameof(LastEdited));
    }

    private void OnChaptersChanged(object? s, NotifyCollectionChangedEventArgs e) => RefreshAll();

    private void HandleSelectionChanged()
    {
        if (_disposed) return;
        var next = ResolveSingle();
        if (!ReferenceEquals(_currentNode, next))
        {
            UnsubscribeNode(_currentNode);
            _currentNode = next;
            SubscribeNode(_currentNode);
            FindSections();
            RebuildChoiceRows();
        }
        if (Mode == InspectorMode.Single && _currentNode is not null)
            _lastEdited = _currentNode;
        RefreshAll();
    }

    private void WatchTargets()
    {
        var wanted = new HashSet<NodeViewModel>();
        foreach (var row in ChoiceRows)
            if (row.Option.TargetNodeId != 0 && _main.Nodes.FirstOrDefault(n => n.Id == row.Option.TargetNodeId) is { } t)
                wanted.Add(t);
        foreach (var n in _targetWatch.Where(n => !wanted.Contains(n)).ToList())
        {
            n.PropertyChanged -= OnTargetNodeChanged;
            _targetWatch.Remove(n);
        }
        foreach (var n in wanted)
            if (_targetWatch.Add(n))
                n.PropertyChanged += OnTargetNodeChanged;
    }

    private void OnTargetNodeChanged(object? s, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (_disposed || e.PropertyName != nameof(NodeViewModel.Title)) return;
        foreach (var row in ChoiceRows) row.Refresh();
    }

    // ── Düğüm / nesne izleme ──

    private void SubscribeNode(NodeViewModel? node)
    {
        if (node is null) return;
        node.PropertyChanged += OnCurrentNodeChanged;
        node.Objects.CollectionChanged += OnObjectsChanged;
        foreach (var o in node.Objects) SubscribeObject(o);
    }

    private void UnsubscribeNode(NodeViewModel? node)
    {
        if (node is null) return;
        node.PropertyChanged -= OnCurrentNodeChanged;
        node.Objects.CollectionChanged -= OnObjectsChanged;
        foreach (var o in node.Objects) UnsubscribeObject(o);
    }

    private void SubscribeObject(FrameObjectViewModel obj)
    {
        obj.PropertyChanged += OnFrameObjectChanged;
        obj.Components.CollectionChanged += OnComponentsChanged;
        foreach (var c in obj.Components)
            c.PropertyChanged += OnComponentChanged;
    }

    private void UnsubscribeObject(FrameObjectViewModel obj)
    {
        obj.PropertyChanged -= OnFrameObjectChanged;
        obj.Components.CollectionChanged -= OnComponentsChanged;
        foreach (var c in obj.Components)
            c.PropertyChanged -= OnComponentChanged;
    }

    private void OnCurrentNodeChanged(object? s, System.ComponentModel.PropertyChangedEventArgs e)
    {
        // Tuvalde sürükleme (X/Y), seçim çerçevesi gibi değişimler Denetçi'yi ilgilendirmez.
        if (e.PropertyName is nameof(NodeViewModel.Title))
            OnPropertyChanged(nameof(Title));
        else if (e.PropertyName is nameof(NodeViewModel.ChapterId) or nameof(NodeViewModel.ColorTag))
        {
            OnPropertyChanged(nameof(MultiChapter));
            OnPropertyChanged(nameof(MultiColorTag));
            OnPropertyChanged(nameof(ChapterSummaries));
        }
    }

    private void OnFrameObjectChanged(object? s, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (_disposed) return;
        FindSections();
        RebuildChoiceRows();
        RefreshAll();
    }

    private void OnObjectsChanged(object? s, NotifyCollectionChangedEventArgs e)
    {
        if (_disposed) return;
        if (e.OldItems is not null)
            foreach (FrameObjectViewModel o in e.OldItems) UnsubscribeObject(o);
        if (e.NewItems is not null)
            foreach (FrameObjectViewModel o in e.NewItems) SubscribeObject(o);
        FindSections();
        RebuildChoiceRows();
        RefreshAll();
    }

    private void OnComponentsChanged(object? s, NotifyCollectionChangedEventArgs e)
    {
        if (_disposed) return;
        if (e.OldItems is not null)
            foreach (NodeComponentViewModel c in e.OldItems) c.PropertyChanged -= OnComponentChanged;
        if (e.NewItems is not null)
            foreach (NodeComponentViewModel c in e.NewItems) c.PropertyChanged += OnComponentChanged;
        FindSections();
        RebuildChoiceRows();
        RefreshAll();
    }

    private void OnComponentChanged(object? s, System.ComponentModel.PropertyChangedEventArgs e) => RefreshAll();
    private void OnChoiceOptionsChanged(object? s, NotifyCollectionChangedEventArgs e)
    {
        RebuildChoiceRows();
        RefreshAll();
    }

    // Bölüm referanslarını yeniden bul.
    private void FindSections()
    {
        if (_choiceSubscribed is not null)
        {
            _choiceSubscribed.Options.CollectionChanged -= OnChoiceOptionsChanged;
            _choiceSubscribed = null;
        }
        if (_currentNode is null)
        {
            _dialogue = null; _character = null; _background = null; _choice = null;
            _audio = null; _transition = null; _camera = null;
            return;
        }
        var all = _currentNode.AllComponents;
        _dialogue = all.OfType<DialogueComponentViewModel>().FirstOrDefault();
        _character = all.OfType<CharacterComponentViewModel>().FirstOrDefault();
        _background = all.OfType<BackgroundComponentViewModel>().FirstOrDefault();
        _choice = all.OfType<ChoiceComponentViewModel>().FirstOrDefault();
        _audio = all.OfType<AudioComponentViewModel>().FirstOrDefault();
        _transition = all.OfType<TransitionComponentViewModel>().FirstOrDefault();
        _camera = all.OfType<CameraComponentViewModel>().FirstOrDefault();
        if (_choice is not null)
        {
            _choiceSubscribed = _choice;
            _choice.Options.CollectionChanged += OnChoiceOptionsChanged;
        }
    }

    private void RebuildChoiceRows()
    {
        DetachRows();
        ChoiceRows.Clear();
        if (_choice is not null)
        {
            foreach (var opt in _choice.Options)
                ChoiceRows.Add(new ChoiceRowViewModel(_main, opt));
        }
        WatchTargets();
    }

    private void DetachRows()
    {
        foreach (var row in ChoiceRows) row.Detach();
        foreach (var n in _targetWatch) n.PropertyChanged -= OnTargetNodeChanged;
        _targetWatch.Clear();
    }

    // Tüm türetilmiş özellikleri duyur.
    private void RefreshAll()
    {
        OnPropertyChanged(nameof(Mode));
        OnPropertyChanged(nameof(Node));
        OnPropertyChanged(nameof(Title));
        OnPropertyChanged(nameof(Dialogue)); OnPropertyChanged(nameof(Character));
        OnPropertyChanged(nameof(Background)); OnPropertyChanged(nameof(Choice));
        OnPropertyChanged(nameof(Audio)); OnPropertyChanged(nameof(Transition));
        OnPropertyChanged(nameof(Camera));
        OnPropertyChanged(nameof(HasDialogue)); OnPropertyChanged(nameof(HasCharacter));
        OnPropertyChanged(nameof(HasBackground)); OnPropertyChanged(nameof(HasChoice));
        OnPropertyChanged(nameof(HasAudio)); OnPropertyChanged(nameof(HasTransition));
        OnPropertyChanged(nameof(HasCamera));
        OnPropertyChanged(nameof(SpeakerName)); OnPropertyChanged(nameof(Portrait));
        OnPropertyChanged(nameof(Expressions)); OnPropertyChanged(nameof(SelectedExpression));
        OnPropertyChanged(nameof(Placement)); OnPropertyChanged(nameof(Line));
        OnPropertyChanged(nameof(Pace)); OnPropertyChanged(nameof(AutoAdvance));
        OnPropertyChanged(nameof(BackgroundImage)); OnPropertyChanged(nameof(BackgroundLabel));
        OnPropertyChanged(nameof(Fit)); OnPropertyChanged(nameof(BackgroundVisibility));
        OnPropertyChanged(nameof(ChoiceRows)); OnPropertyChanged(nameof(TargetOptions));
        OnPropertyChanged(nameof(MusicLabel)); OnPropertyChanged(nameof(HasMusic));
        OnPropertyChanged(nameof(EffectLabel)); OnPropertyChanged(nameof(HasEffect));
        OnPropertyChanged(nameof(Volume)); OnPropertyChanged(nameof(IsMusicPlaying));
        OnPropertyChanged(nameof(TransitionChoices)); OnPropertyChanged(nameof(TransitionKind));
        OnPropertyChanged(nameof(Length)); OnPropertyChanged(nameof(CameraMove));
        OnPropertyChanged(nameof(CameraShake));
        OnPropertyChanged(nameof(SelectedCount)); OnPropertyChanged(nameof(SelectionSummary));
        OnPropertyChanged(nameof(MultiChapter)); OnPropertyChanged(nameof(MultiColorTag));
        OnPropertyChanged(nameof(ChapterSummaries)); OnPropertyChanged(nameof(LastEdited));
    }

    // ── Durum ──

    public InspectorMode Mode =>
        _main.SelectedNodes.Count > 1 ? InspectorMode.Multi :
        _main.SelectedNode is not null ? InspectorMode.Single : InspectorMode.Empty;

    public NodeViewModel? Node => Mode == InspectorMode.Single ? _main.SelectedNode : null;

    public string Title
    {
        get => Node?.Title ?? string.Empty;
        set { var n = Node; if (n is not null) n.Title = value; }
    }

    // ── Bileşen referansları ──

    public DialogueComponentViewModel? Dialogue => _dialogue;
    public CharacterComponentViewModel? Character => _character;
    public BackgroundComponentViewModel? Background => _background;
    public ChoiceComponentViewModel? Choice => _choice;
    public AudioComponentViewModel? Audio => _audio;
    public TransitionComponentViewModel? Transition => _transition;
    public CameraComponentViewModel? Camera => _camera;

    public bool HasDialogue => _dialogue is not null;
    public bool HasCharacter => _character is not null;
    public bool HasBackground => _background is not null;
    public bool HasChoice => _choice is not null;
    public bool HasAudio => _audio is not null;
    public bool HasTransition => _transition is not null;
    public bool HasCamera => _camera is not null;

    // ── Kim konuşuyor ──

    public string SpeakerName
    {
        get => Dialogue?.Speaker ?? string.Empty;
        set { if (Dialogue is not null) Dialogue.Speaker = value; }
    }

    public Bitmap? Portrait => Character?.SpriteBitmap;

    public IReadOnlyList<string> Expressions
    {
        get
        {
            // Tür değişse de derlensin diye gevşek okuma.
            if (Character?.ExpressionNames is not System.Collections.IEnumerable en)
                return Array.Empty<string>();
            if (en is IReadOnlyList<string> ro) return ro;
            var list = new List<string>();
            foreach (var o in en) if (o is string s) list.Add(s);
            return list;
        }
    }

    public string? SelectedExpression
    {
        get => Character?.SelectedExpressionName;
        set { if (Character is not null) Character.SelectedExpressionName = value; }
    }

    public StagePlacement Placement
    {
        get
        {
            var c = Character;
            if (c is null) return StagePlacement.Center;
            double cx = c.X + c.Width / 2.0;
            if (cx < 640) return StagePlacement.Left;
            if (cx > 1280) return StagePlacement.Right;
            return StagePlacement.Center;
        }
        set
        {
            var c = Character;
            if (c is null) return;
            double center = value == StagePlacement.Left ? 480 : value == StagePlacement.Right ? 1440 : 960;
            c.X = center - c.Width / 2.0;
            c.Position = value == StagePlacement.Left ? "Left" : value == StagePlacement.Right ? "Right" : "Center";
        }
    }

    // ── Ne diyor ──

    public string Line
    {
        get => Dialogue?.DialogueText ?? string.Empty;
        set { if (Dialogue is not null) Dialogue.DialogueText = value; }
    }

    public TextPace Pace
    {
        get
        {
            var d = Dialogue;
            if (d is null) return TextPace.Normal;
            if (!d.TypewriterEnabled) return TextPace.Instant;
            if (d.TextSpeed >= 45) return TextPace.Slow;
            if (d.TextSpeed <= 20) return TextPace.Fast;
            return TextPace.Normal;
        }
        set
        {
            var d = Dialogue;
            if (d is null) return;
            if (value == TextPace.Instant) { d.TypewriterEnabled = false; return; }
            d.TypewriterEnabled = true;
            d.TextSpeed = value == TextPace.Slow ? 60 : value == TextPace.Fast ? 15 : 30;
        }
    }

    public bool AutoAdvance
    {
        get => Dialogue?.AutoAdvance ?? false;
        set { if (Dialogue is not null) Dialogue.AutoAdvance = value; }
    }

    // ── Sahne görünümü ──

    public Bitmap? BackgroundImage => Background?.TextureBitmap;

    public string BackgroundLabel => GetFileLabel(Background?.Texture);

    private static string GetFileLabel(string? path)
    {
        if (string.IsNullOrWhiteSpace(path)) return string.Empty;
        string name;
        try { name = Path.GetFileNameWithoutExtension(path); }
        catch { return string.Empty; }
        if (string.IsNullOrEmpty(name)) return string.Empty;
        if (name.Length >= 24 && name.All(c => (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return EditorStrings.T("Inspector.Scene.ImportedImage");
        return name;
    }

    public BackgroundFit Fit
    {
        get
        {
            var b = Background;
            if (b is null) return BackgroundFit.Fill;
            if (b.X == 0 && b.Y == 0 && b.Width == 1920 && b.Height == 1080)
                return BackgroundFit.Fill;
            return BackgroundFit.Fit;
        }
        set
        {
            var b = Background;
            if (b is null) return;
            if (value == BackgroundFit.Fill)
            {
                b.X = 0; b.Y = 0; b.Width = 1920; b.Height = 1080;
                return;
            }
            var bmp = b.TextureBitmap;
            if (bmp is null) { b.X = 0; b.Y = 0; b.Width = 1920; b.Height = 1080; return; }
            double bw = bmp.PixelSize.Width, bh = bmp.PixelSize.Height;
            if (bw <= 0 || bh <= 0) { b.X = 0; b.Y = 0; b.Width = 1920; b.Height = 1080; return; }
            double scale = Math.Min(1920.0 / bw, 1080.0 / bh);
            double w = bw * scale, h = bh * scale;
            b.Width = w; b.Height = h;
            b.X = (1920 - w) / 2.0; b.Y = (1080 - h) / 2.0;
        }
    }

    public double BackgroundVisibility
    {
        get => Background?.Opacity ?? 1.0;
        set { if (Background is not null) Background.Opacity = Math.Clamp(value, 0, 1); }
    }

    // ── Seçimler ──

    public ObservableCollection<ChoiceRowViewModel> ChoiceRows { get; }

    public IReadOnlyList<NodeViewModel> TargetOptions =>
        _main.Nodes.Where(n => n != Node).OrderBy(n => n.Title, StringComparer.Ordinal).ToList();

    public void SetTarget(ChoiceRowViewModel row, NodeViewModel target)
    {
        if (row is null || target is null) return;
        row.Option.TargetNodeId = target.Id;
    }

    [RelayCommand]
    private void AddChoice()
    {
        var c = Choice;
        string text = EditorStrings.T("Inspector.Choices.NewChoice");
        if (c is null)
        {
            AddSection("choice");
            c = Choice;
            if (c is null) return;
            // Yeni bileşen varsayılan bir seçenekle gelir: ikinci bir satır eklemek yerine onu adlandır.
            if (c.Options.Count > 0)
            {
                c.Options[0].Text = text;
                return;
            }
        }
        c.Options.Add(new ChoiceOptionViewModel { Text = text });
    }

    // ── Ses ──

    public string MusicLabel => GetFileLabel(Audio?.BgmTrack);
    public bool HasMusic => !string.IsNullOrWhiteSpace(Audio?.BgmTrack);
    public string EffectLabel => GetFileLabel(Audio?.SfxTrack);
    public bool HasEffect => !string.IsNullOrWhiteSpace(Audio?.SfxTrack);

    public double Volume
    {
        get => (double)(Audio?.Volume ?? 0f);
        set { if (Audio is not null) Audio.Volume = (float)value; }
    }

    public bool IsMusicPlaying => Audio?.IsPreviewingBgm ?? false;

    [RelayCommand]
    private void ToggleMusicPreview()
    {
        Audio?.ToggleBgmPreview();
        OnPropertyChanged(nameof(IsMusicPlaying));
    }

    // ── Geçiş ve kamera ──

    /// <summary>Geçiş kartları: motor türü + yerelleştirilmiş ad (XAML bağlanabilsin diye kayıt).</summary>
    public IReadOnlyList<TransitionChoice> TransitionChoices { get; } = new[]
    {
        new TransitionChoice("fade_black", "Inspector.Transition.Fade"),
        new TransitionChoice("crossfade", "Inspector.Transition.Crossfade"),
        new TransitionChoice("wipe_left", "Inspector.Transition.Wipe"),
        new TransitionChoice("fade_white", "Inspector.Transition.FadeWhite"),
    };

    public string TransitionKind
    {
        get => Transition?.Kind ?? string.Empty;
        set { if (Transition is not null) Transition.Kind = value; }
    }

    public TransitionLength Length
    {
        get
        {
            var t = Transition;
            if (t is null) return TransitionLength.Medium;
            double d = t.Duration;
            double ds = Math.Abs(d - 0.5), dm = Math.Abs(d - 1.0), dl = Math.Abs(d - 2.0);
            if (ds <= dm && ds <= dl) return TransitionLength.Short;
            if (dm <= dl) return TransitionLength.Medium;
            return TransitionLength.Long;
        }
        set
        {
            if (Transition is null) return;
            Transition.Duration = value == TransitionLength.Short ? 0.5 : value == TransitionLength.Long ? 2.0 : 1.0;
        }
    }

    public CameraMove CameraMove
    {
        get
        {
            var c = Camera;
            if (c is null) return CameraMove.Still;
            if (Math.Abs(c.Zoom - 1.2) <= 0.05) return CameraMove.In;
            if (Math.Abs(c.Zoom - 0.85) <= 0.05) return CameraMove.Out;
            return CameraMove.Still;
        }
        set
        {
            if (Camera is null) return;
            Camera.Zoom = value == CameraMove.In ? 1.2 : value == CameraMove.Out ? 0.85 : 1.0;
        }
    }

    public CameraShake CameraShake
    {
        get
        {
            var p = Camera?.ShakePreset;
            if (string.Equals(p, "None", StringComparison.Ordinal)) return CameraShake.None;
            if (string.Equals(p, "Earthquake", StringComparison.Ordinal)) return CameraShake.Strong;
            return CameraShake.Light;
        }
        set
        {
            if (Camera is null) return;
            Camera.ShakePreset = value == CameraShake.None ? "None" : value == CameraShake.Strong ? "Earthquake" : "Subtle";
        }
    }

    // ── Bölüm ekleme ──

    [RelayCommand]
    private void AddSection(string? kind)
    {
        if (string.IsNullOrWhiteSpace(kind)) return;
        var node = Node;
        if (node is null) return;
        string k = kind.ToLowerInvariant();
        if (k == "background" || k == "character" || k == "dialogue" || k == "audio")
        {
            _main.HierarchyViewModel?.CreateObjectCommand.Execute(k);
        }
        else if (k == "choice")
        {
            node.CreateObject("Choice").AddComponent<ChoiceComponentViewModel>();
            _main.ScheduleSave();
        }
        else if (k == "transition")
        {
            node.CreateObject("Transition").AddComponent<TransitionComponentViewModel>();
            _main.ScheduleSave();
        }
        else if (k == "camera")
        {
            node.CreateObject("Camera").AddComponent<CameraComponentViewModel>();
            _main.ScheduleSave();
        }
        else return;
        FindSections();
        RebuildChoiceRows();
        RefreshAll();
    }

    // ── Çoklu seçim ──

    public int SelectedCount => _main.SelectedNodes.Count;

    public string SelectionSummary
    {
        get
        {
            var nodes = _main.SelectedNodes;
            if (nodes.Count == 0) return string.Empty;
            var titles = nodes.Select(n => n.Title).ToList();
            if (titles.Count <= 3) return string.Join(", ", titles);
            string more = EditorStrings.T("Inspector.Multi.AndMore");
            string formatted;
            try { formatted = string.Format(more, titles.Count - 3); }
            catch { formatted = more; }
            return string.Join(", ", titles.Take(3)) + " " + formatted;
        }
    }

    public string? MultiChapter
    {
        get
        {
            var sel = _main.SelectedNodes;
            if (sel.Count == 0) return null;
            string first = sel[0].ChapterId;
            for (int i = 1; i < sel.Count; i++)
                if (!string.Equals(sel[i].ChapterId, first, StringComparison.Ordinal))
                    return null;
            return first;
        }
        set
        {
            var sel = _main.SelectedNodes.ToList();
            if (sel.Count == 0) return;
            string after = value ?? string.Empty;
            var before = sel.ToDictionary(n => n, n => n.ChapterId);
            foreach (var n in sel) n.ChapterId = after;
            UndoRedoService.Instance.RecordAction(new BulkStringAction(before, after, (n, v) => n.ChapterId = v, "Chapter"));
            RefreshAll();
        }
    }

    public string? MultiColorTag
    {
        get
        {
            var sel = _main.SelectedNodes;
            if (sel.Count == 0) return null;
            string first = sel[0].ColorTag;
            for (int i = 1; i < sel.Count; i++)
                if (!string.Equals(sel[i].ColorTag, first, StringComparison.Ordinal))
                    return null;
            return first;
        }
        set
        {
            var sel = _main.SelectedNodes.ToList();
            if (sel.Count == 0) return;
            string after = value ?? string.Empty;
            var before = sel.ToDictionary(n => n, n => n.ColorTag);
            foreach (var n in sel) n.ColorTag = after;
            UndoRedoService.Instance.RecordAction(new BulkStringAction(before, after, (n, v) => n.ColorTag = v, "ColorTag"));
            RefreshAll();
        }
    }

    [RelayCommand]
    private void DuplicateSelected() => _main.DuplicateSelectedNodes();

    [RelayCommand]
    private void DeleteSelected() => _main.DeleteSelectedNodes();

    // ── Seçim yok ──

    public IReadOnlyList<ChapterSummary> ChapterSummaries
    {
        get
        {
            var list = new List<ChapterSummary>();
            foreach (var g in _main.Nodes.GroupBy(n => n.ChapterId ?? string.Empty))
            {
                string title;
                if (string.IsNullOrEmpty(g.Key))
                    title = ChapterStorageService.DefaultChapterTitle;
                else
                {
                    var def = _main.Chapters.Definitions.FirstOrDefault(c => string.Equals(c.Id, g.Key, StringComparison.Ordinal));
                    if (def is null) title = g.Key;
                    else title = !string.IsNullOrEmpty(def.Title) ? def.Title : def.Id;
                }
                list.Add(new ChapterSummary(title, g.Count()));
            }
            list.Sort((a, b) => string.Compare(a.Title, b.Title, StringComparison.Ordinal));
            return list;
        }
    }

    public NodeViewModel? LastEdited =>
        _lastEdited is not null && _main.Nodes.Contains(_lastEdited) ? _lastEdited : null;

    [RelayCommand]
    private void Continue()
    {
        var n = LastEdited;
        if (n is null) return;
        _main.SelectedNode = n;
        _main.Search.JumpTo(n);
    }

    [RelayCommand]
    private void NewScene() => _main.AddNode();

    // Tek geri alımlı toplu yazma.
    private sealed class BulkStringAction : Services.IUndoAction
    {
        private readonly Dictionary<NodeViewModel, string> _before;
        private readonly string _after;
        private readonly Action<NodeViewModel, string> _apply;
        private readonly string _label;
        public BulkStringAction(Dictionary<NodeViewModel, string> before, string after, Action<NodeViewModel, string> apply, string label)
        {
            _before = before; _after = after; _apply = apply; _label = label;
        }
        public string Description => _label;
        public void Undo() { foreach (var kv in _before) _apply(kv.Key, kv.Value); }
        public void Redo() { foreach (var kv in _before) _apply(kv.Key, _after); }
    }
}

/// <summary>Seçim satırı: metin + hedef düğüm.</summary>
public sealed partial class ChoiceRowViewModel : ViewModelBase
{
    private readonly MainWindowViewModel _main;
    public ChoiceOptionViewModel Option { get; }

    public ChoiceRowViewModel(MainWindowViewModel main, ChoiceOptionViewModel option)
    {
        _main = main;
        Option = option;
        Option.PropertyChanged += OnOptionChanged;
    }

    private void OnOptionChanged(object? s, System.ComponentModel.PropertyChangedEventArgs e)
    {
            if (e.PropertyName == nameof(ChoiceOptionViewModel.Text) || string.IsNullOrEmpty(e.PropertyName))
                OnPropertyChanged(nameof(Text));
            if (e.PropertyName == nameof(ChoiceOptionViewModel.TargetNodeId) || string.IsNullOrEmpty(e.PropertyName))
            {
                OnPropertyChanged(nameof(TargetTitle));
                OnPropertyChanged(nameof(HasTarget));
                OnPropertyChanged(nameof(IsTargetMissing));
            }
    }

    /// <summary>Düğüm listesi ya da hedef başlığı değişti.</summary>
    public void Refresh()
    {
        OnPropertyChanged(nameof(TargetTitle));
        OnPropertyChanged(nameof(HasTarget));
        OnPropertyChanged(nameof(IsTargetMissing));
    }

    public void Detach() => Option.PropertyChanged -= OnOptionChanged;

    public string Text
    {
        get => Option.Text ?? string.Empty;
        set => Option.Text = value;
    }

    public string TargetTitle
    {
        get
        {
            ulong id = Option.TargetNodeId;
            if (id == 0) return string.Empty;
            return _main.Nodes.FirstOrDefault(n => n.Id == id)?.Title ?? string.Empty;
        }
    }

    public bool HasTarget
    {
        get
        {
            ulong id = Option.TargetNodeId;
            if (id == 0) return false;
            return _main.Nodes.Any(n => n.Id == id);
        }
    }

    public bool IsTargetMissing
    {
        get
        {
            ulong id = Option.TargetNodeId;
            if (id == 0) return false;
            return !_main.Nodes.Any(n => n.Id == id);
        }
    }

    [RelayCommand]
    private void GoToTarget()
    {
        ulong id = Option.TargetNodeId;
        if (id == 0) return;
        var node = _main.Nodes.FirstOrDefault(n => n.Id == id);
        if (node is null) return;
        _main.Search.JumpTo(node);
    }
}

/// <summary>Seçim yok durumunda bölüm satırı.</summary>
public sealed record ChapterSummary(string Title, int Count);

/// <summary>Geçiş kartı: motorun tür adı ve yerelleştirilmiş görünen ad.</summary>
public sealed record TransitionChoice(string Kind, string Key)
{
    public string Label => EditorStrings.T(Key);
}
