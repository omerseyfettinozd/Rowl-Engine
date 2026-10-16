using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Controls;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

// Küçük sink soyutlaması (UndoRedoService bunu uygular).
namespace RowlEngine.Editor.Services
{
    public interface IUndoActionSink
    {
        void RecordAction(IUndoAction a);
    }
}

namespace RowlEngine.Editor.Services.Inspector
{
    // Tek oturumluk alan düzenlemesi (yansıma ile uygular).
    public sealed class InspectorEditAction : IUndoAction
    {
        private readonly List<(object Target, PropertyInfo Prop, object? Before, object? After)> _entries;

        public InspectorEditAction(List<(object Target, PropertyInfo Prop, object? Before, object? After)> entries)
        {
            _entries = new List<(object, PropertyInfo, object?, object?)>(entries);
        }

        // Tek alan adı, çok alan sayı gösterir.
        public string Description => _entries.Count == 1
            ? $"Edit {_entries[0].Prop.Name}"
            : $"Edit {_entries.Count} fields";

        public int FieldCount => _entries.Count;

        public void Undo()
        {
            // Ters sırada geri al.
            for (int i = _entries.Count - 1; i >= 0; i--)
            {
                var e = _entries[i];
                try { e.Prop.SetValue(e.Target, e.Before); } catch { /* yut, model canlı kalsın */ }
            }
        }

        public void Redo()
        {
            for (int i = 0; i < _entries.Count; i++)
            {
                var e = _entries[i];
                try { e.Prop.SetValue(e.Target, e.After); } catch { /* yut */ }
            }
        }
    }

    // UI'siz saf çekirdek: izle + anlık görüntü + tek eylem üret.
    public sealed class InspectorEditSession : IDisposable
    {
        private sealed class PendingEntry
        {
            public object Target;
            public PropertyInfo Prop;
            public object? Before;
            public object? After;
            public PendingEntry(object t, PropertyInfo p, object? b, object? a)
            {
                Target = t; Prop = p; Before = b; After = a;
            }
        }

        // Adret listesi (Ordinal).
        private static readonly HashSet<string> s_excludedExact = new(StringComparer.Ordinal)
        {
            "IsSelected", "IsHighlighted", "IsExpanded", "IsEditing",
            "IsHovered", "IsStartNode", "IsDragging", "IsVisibleInViewport",
            // Düğüm kartının türetilmiş görünüm durumu (seçim/arama/alt grafik ile değişir).
            "Id", "FilterOpacity", "IsSearchHighlighted", "IsSubgraphBoundary", "SubgraphBadge", "HasColorTag",
        };

        // Türe özgü ret: düğüm kartının tuval konumu/boyutu (MoveNodesAction'ın işi) ve
        // seçim çerçevesi rengi. Bileşenlerdeki aynı adlı alanlar (diyalog kutusu X/Y/
        // Width/Height, BorderColor) gerçek yazar ayarlarıdır ve kaydedilir.
        private static readonly HashSet<string> s_nodeOnlyExcluded = new(StringComparer.Ordinal)
        {
            "X", "Y", "Width", "Height", "BorderColor",
        };

        private static bool IsExcludedForType(Type type, string name) =>
            typeof(NodeViewModel).IsAssignableFrom(type) && s_nodeOnlyExcluded.Contains(name);

        private static readonly Dictionary<Type, List<PropertyInfo>> s_recordableCache = new();
        private static readonly Dictionary<Type, List<PropertyInfo>> s_collectionCache = new();
        private static readonly object s_cacheGate = new();

        private readonly Func<bool> _isExecuting;
        private readonly HashSet<object> _visited = new(ReferenceEqualityComparer.Instance);
        private readonly HashSet<object> _collVisited = new(ReferenceEqualityComparer.Instance);
        private readonly List<(INotifyPropertyChanged Obj, PropertyChangedEventHandler Handler)> _propSubs = new();
        private readonly List<(INotifyCollectionChanged Coll, NotifyCollectionChangedEventHandler Handler)> _collSubs = new();
        private readonly Dictionary<object, Dictionary<string, object?>> _snapshots = new(ReferenceEqualityComparer.Instance);
        private readonly List<PendingEntry> _ordered = new();
        private readonly Dictionary<object, Dictionary<string, int>> _index = new(ReferenceEqualityComparer.Instance);

        private object? _root;
        private bool _isActive;
        private bool _disposed;

        public InspectorEditSession(Func<bool>? isExecuting = null)
        {
            // Varsayılan gerçek servis bayrağı.
            _isExecuting = isExecuting ?? (() => UndoRedoService.Instance.IsExecuting);
        }

        public bool IsActive => _isActive;

        // Kökü ve altındaki düzenlenebilir nesneleri izler.
        public void Track(object? root)
        {
            if (_disposed || root is null)
                return;
            UntrackInternal();
            _root = root;
            TrackObject(root);
        }

        public void Untrack()
        {
            UntrackInternal();
            _root = null;
            _isActive = false;
        }

        public void Begin()
        {
            if (_disposed || _isActive)
                return;
            _ordered.Clear();
            _index.Clear();
            _isActive = true;
        }

        // before≠after olanları tek eylem yapar.
        public void Commit(IUndoActionSink sink)
        {
            if (!_isActive)
                return;
            _isActive = false;
            if (_ordered.Count == 0)
            {
                _index.Clear();
                return;
            }

            var filtered = new List<(object, PropertyInfo, object?, object?)>();
            foreach (var p in _ordered)
            {
                if (!Equals(p.Before, p.After))
                    filtered.Add((p.Target, p.Prop, p.Before, p.After));
            }

            _ordered.Clear();
            _index.Clear();
            if (filtered.Count == 0)
                return;

            var action = new InspectorEditAction(filtered);
            sink.RecordAction(action);
        }

        public void Cancel()
        {
            _ordered.Clear();
            _index.Clear();
            _isActive = false;
        }

        public void Dispose()
        {
            if (_disposed)
                return;
            _disposed = true;
            UntrackInternal();
            _root = null;
            _isActive = false;
        }

        // İzinli değer türü mü?
        private static bool IsAllowedType(Type t)
        {
            if (t == typeof(string))
                return true;
            if (t == typeof(Color))
                return true;
            if (t == typeof(decimal))
                return true;
            if (t.IsEnum)
                return true;
            if (t.IsPrimitive)
                return true;
            if (t == typeof(ulong))
                return true;
            var under = Nullable.GetUnderlyingType(t);
            if (under != null)
                return IsAllowedType(under);
            return false;
        }

        private static bool IsExcludedName(string name)
        {
            if (s_excludedExact.Contains(name))
                return true;
            // Is…Visible ve Display… önekleri.
            if (name.StartsWith("Display", StringComparison.Ordinal))
                return true;
            if (name.StartsWith("Is", StringComparison.Ordinal) && name.EndsWith("Visible", StringComparison.Ordinal))
                return true;
            return false;
        }

        private static List<PropertyInfo> GetRecordableProps(Type type)
        {
            lock (s_cacheGate)
            {
                if (s_recordableCache.TryGetValue(type, out var hit))
                    return hit;
                var list = new List<PropertyInfo>();
                foreach (var p in type.GetProperties(BindingFlags.Instance | BindingFlags.Public))
                {
                    if (!p.CanRead || !p.CanWrite)
                        continue;
                    if (p.GetMethod?.IsPublic != true || p.SetMethod?.IsPublic != true)
                        continue;
                    if (p.GetIndexParameters().Length != 0)
                        continue;
                    if (IsExcludedName(p.Name) || IsExcludedForType(type, p.Name))
                        continue;
                    if (!IsAllowedType(p.PropertyType))
                        continue;
                    list.Add(p);
                }

                s_recordableCache[type] = list;
                return list;
            }
        }

        private static bool IsTrackedElementType(Type e)
        {
            // Grafiğe yayılma yok.
            if (typeof(NodeViewModel).IsAssignableFrom(e))
                return false;
            if (e == typeof(FrameObjectViewModel))
                return true;
            if (typeof(NodeComponentViewModel).IsAssignableFrom(e))
                return true;
            if (string.Equals(e.Namespace, "RowlEngine.Editor.ViewModels.Components", StringComparison.Ordinal))
                return true;
            return false;
        }

        private static Type? GetEnumerableElement(Type type)
        {
            if (type == typeof(string))
                return null;
            if (type.IsArray)
                return type.GetElementType();
            if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(IEnumerable<>))
                return type.GetGenericArguments()[0];
            foreach (var i in type.GetInterfaces())
            {
                if (i.IsGenericType && i.GetGenericTypeDefinition() == typeof(IEnumerable<>))
                    return i.GetGenericArguments()[0];
            }

            return null;
        }

        private static List<PropertyInfo> GetTrackedCollectionProps(Type type)
        {
            lock (s_cacheGate)
            {
                if (s_collectionCache.TryGetValue(type, out var hit))
                    return hit;
                var list = new List<PropertyInfo>();
                foreach (var p in type.GetProperties(BindingFlags.Instance | BindingFlags.Public))
                {
                    if (!p.CanRead)
                        continue;
                    if (p.GetIndexParameters().Length != 0)
                        continue;
                    if (!typeof(IEnumerable).IsAssignableFrom(p.PropertyType))
                        continue;
                    if (p.PropertyType == typeof(string))
                        continue;
                    var elem = GetEnumerableElement(p.PropertyType);
                    if (elem == null)
                        continue;
                    if (!IsTrackedElementType(elem))
                        continue;
                    list.Add(p);
                }

                s_collectionCache[type] = list;
                return list;
            }
        }

        private void TrackObject(object? obj)
        {
            if (obj is null || _disposed)
                return;
            // Kök dışı düğüme inme.
            if (obj is NodeViewModel && !ReferenceEquals(obj, _root))
                return;
            if (!_visited.Add(obj))
                return;

            if (obj is INotifyPropertyChanged inpc)
            {
                PropertyChangedEventHandler h = OnTrackedPropertyChanged;
                inpc.PropertyChanged += h;
                _propSubs.Add((inpc, h));

                var dict = new Dictionary<string, object?>(StringComparer.Ordinal);
                foreach (var p in GetRecordableProps(obj.GetType()))
                {
                    try { dict[p.Name] = p.GetValue(obj); }
                    catch { dict[p.Name] = null; }
                }

                _snapshots[obj] = dict;
            }

            foreach (var cp in GetTrackedCollectionProps(obj.GetType()))
            {
                object? val;
                try { val = cp.GetValue(obj); }
                catch { continue; }
                if (val is INotifyCollectionChanged ncc && val is IEnumerable en)
                    TrackCollection(ncc, en);
                else if (val is IEnumerable en2)
                {
                    foreach (var item in en2)
                        TrackObject(item);
                }
            }
        }

        private void TrackCollection(INotifyCollectionChanged coll, IEnumerable items)
        {
            if (_disposed)
                return;
            if (!_collVisited.Add(coll))
                return;
            NotifyCollectionChangedEventHandler h = OnTrackedCollectionChanged;
            coll.CollectionChanged += h;
            _collSubs.Add((coll, h));
            foreach (var item in items)
                TrackObject(item);
        }

        private void UntrackCollection(INotifyCollectionChanged coll)
        {
            if (!_collVisited.Remove(coll))
                return;
            for (int i = _collSubs.Count - 1; i >= 0; i--)
            {
                if (ReferenceEquals(_collSubs[i].Coll, coll))
                {
                    try { coll.CollectionChanged -= _collSubs[i].Handler; } catch { /* yut */ }
                    _collSubs.RemoveAt(i);
                }
            }
        }

        // Dalı bırak (yapısal değişim kaydedilmez).
        private void UntrackBranch(object? obj)
        {
            if (obj is null)
                return;
            if (obj is NodeViewModel && !ReferenceEquals(obj, _root))
                return;

            // Koleksiyon dalıysa önce koleksiyonu çöz.
            if (obj is INotifyCollectionChanged ncc && obj is IEnumerable)
            {
                UntrackCollection(ncc);
            }

            if (!_visited.Remove(obj))
            {
                // Zaten nesne yoksa alt dalları yine dene (koleksiyon içeriği).
                if (obj is IEnumerable en0 && !(obj is string))
                {
                    foreach (var item in en0.Cast<object>().ToList())
                        UntrackBranch(item);
                }

                return;
            }

            if (obj is INotifyPropertyChanged inpc)
            {
                for (int i = _propSubs.Count - 1; i >= 0; i--)
                {
                    if (ReferenceEquals(_propSubs[i].Obj, inpc))
                    {
                        try { inpc.PropertyChanged -= _propSubs[i].Handler; } catch { /* yut */ }
                        _propSubs.RemoveAt(i);
                    }
                }
            }

            _snapshots.Remove(obj);
            RemovePendingFor(obj);

            // Alt koleksiyonları ve öğeleri çöz.
            foreach (var cp in GetTrackedCollectionProps(obj.GetType()))
            {
                object? val;
                try { val = cp.GetValue(obj); }
                catch { continue; }
                if (val is INotifyCollectionChanged childColl && val is IEnumerable childEn)
                {
                    var items = childEn.Cast<object>().ToList();
                    UntrackCollection(childColl);
                    foreach (var item in items)
                        UntrackBranch(item);
                }
            }
        }

        private void RemovePendingFor(object target)
        {
            if (!_index.TryGetValue(target, out _))
                return;
            _index.Remove(target);
            _ordered.RemoveAll(p => ReferenceEquals(p.Target, target));
            // Sıra dizinlerini tazele.
            for (int i = 0; i < _ordered.Count; i++)
            {
                var p = _ordered[i];
                if (!_index.TryGetValue(p.Target, out var inner))
                {
                    inner = new Dictionary<string, int>(StringComparer.Ordinal);
                    _index[p.Target] = inner;
                }

                inner[p.Prop.Name] = i;
            }
        }

        private void UntrackInternal()
        {
            foreach (var (o, h) in _propSubs)
            {
                try { o.PropertyChanged -= h; } catch { /* yut */ }
            }

            _propSubs.Clear();
            foreach (var (c, h) in _collSubs)
            {
                try { c.CollectionChanged -= h; } catch { /* yut */ }
            }

            _collSubs.Clear();
            _visited.Clear();
            _collVisited.Clear();
            _snapshots.Clear();
            _ordered.Clear();
            _index.Clear();
        }

        private void OnTrackedCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (_disposed || sender is null)
                return;
            // Yapısal değişim kaydedilmez, yalnız izleme güncellenir.
            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    if (e.NewItems != null)
                        foreach (var item in e.NewItems)
                            TrackObject(item);
                    break;
                case NotifyCollectionChangedAction.Remove:
                    if (e.OldItems != null)
                        foreach (var item in e.OldItems)
                            UntrackBranch(item);
                    break;
                case NotifyCollectionChangedAction.Replace:
                    if (e.OldItems != null)
                        foreach (var item in e.OldItems)
                            UntrackBranch(item);
                    if (e.NewItems != null)
                        foreach (var item in e.NewItems)
                            TrackObject(item);
                    break;
                case NotifyCollectionChangedAction.Reset:
                    if (sender is IEnumerable en)
                    {
                        foreach (var item in en)
                            TrackObject(item);
                    }

                    break;
                case NotifyCollectionChangedAction.Move:
                    break;
            }
        }

        private void OnTrackedPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (_disposed || sender is null)
                return;
            if (!_snapshots.TryGetValue(sender, out _))
                return;

            var name = e.PropertyName;
            if (string.IsNullOrEmpty(name))
            {
                // Toplu tazele: farkları tek tek işle.
                foreach (var p in GetRecordableProps(sender.GetType()))
                    ProcessPropChange(sender, p);
                return;
            }

            // Koleksiyon değişimi (ör. yeni liste) ise izlemeyi tazele.
            var collProps = GetTrackedCollectionProps(sender.GetType());
            for (int i = 0; i < collProps.Count; i++)
            {
                if (string.Equals(collProps[i].Name, name, StringComparison.Ordinal))
                {
                    object? val;
                    try { val = collProps[i].GetValue(sender); }
                    catch { return; }
                    if (val is INotifyCollectionChanged ncc && val is IEnumerable en)
                        TrackCollection(ncc, en);
                    return;
                }
            }

            PropertyInfo? prop = null;
            foreach (var p in GetRecordableProps(sender.GetType()))
            {
                if (string.Equals(p.Name, name, StringComparison.Ordinal))
                {
                    prop = p;
                    break;
                }
            }

            if (prop == null)
                return;
            ProcessPropChange(sender, prop);
        }

        private void ProcessPropChange(object sender, PropertyInfo prop)
        {
            object? current;
            try { current = prop.GetValue(sender); }
            catch { return; }
            if (!_snapshots.TryGetValue(sender, out var dict))
                return;
            dict.TryGetValue(prop.Name, out var oldSnap);
            if (Equals(oldSnap, current))
                return;

            if (!_isActive)
            {
                // Pasifken yalnız görüntüyü ilerlet.
                dict[prop.Name] = current;
                return;
            }

            if (_isExecuting())
            {
                // Geri alma sırsında yeni kayıt yok.
                dict[prop.Name] = current;
                if (_index.TryGetValue(sender, out var inner) && inner.TryGetValue(prop.Name, out var idx))
                    _ordered[idx].After = current;
                return;
            }

            dict[prop.Name] = current;
            if (_index.TryGetValue(sender, out var inner2) && inner2.TryGetValue(prop.Name, out var idx2))
            {
                _ordered[idx2].After = current;
                return;
            }

            var entry = new PendingEntry(sender, prop, oldSnap, current);
            _ordered.Add(entry);
            if (!_index.TryGetValue(sender, out var d))
            {
                d = new Dictionary<string, int>(StringComparer.Ordinal);
                _index[sender] = d;
            }

            d[prop.Name] = _ordered.Count - 1;
        }
    }

    // Görünüm bağı: odak kapısı + düğüm değişimi.
    public sealed class InspectorEditUndoRecorder : IDisposable
    {
        private Control? _root;
        private Func<NodeViewModel?>? _currentNode;
        private IUndoActionSink? _sink;
        private InspectorEditSession? _session;
        private NodeViewModel? _tracked;
        private Control? _activeInput;
        private EventHandler<GotFocusEventArgs>? _gotFocusHandler;
        private EventHandler<RoutedEventArgs>? _lostFocusHandler;
        private bool _disposed;

        public void Attach(Control inspectorRoot, Func<NodeViewModel?> currentNode, IUndoActionSink sink)
        {
            if (_disposed)
                return;
            Detach();
            _root = inspectorRoot;
            _currentNode = currentNode;
            _sink = sink;
            _session = new InspectorEditSession();
            try
            {
                _tracked = currentNode?.Invoke();
                if (_tracked != null)
                    _session.Track(_tracked);
            }
            catch { /* yut */ }

            _gotFocusHandler = OnGotFocus;
            _lostFocusHandler = OnLostFocus;
            _root.AddHandler(InputElement.GotFocusEvent, _gotFocusHandler, handledEventsToo: true);
            _root.AddHandler(InputElement.LostFocusEvent, _lostFocusHandler, handledEventsToo: true);
        }

        public void Detach()
        {
            if (_session != null && _sink != null)
            {
                try { _session.Commit(_sink); } catch { /* yut */ }
            }

            if (_root != null)
            {
                if (_gotFocusHandler != null)
                    try { _root.RemoveHandler(InputElement.GotFocusEvent, _gotFocusHandler); } catch { /* yut */ }
                if (_lostFocusHandler != null)
                    try { _root.RemoveHandler(InputElement.LostFocusEvent, _lostFocusHandler); } catch { /* yut */ }
            }

            _session?.Dispose();
            _session = null;
            _root = null;
            _currentNode = null;
            _sink = null;
            _tracked = null;
        }

        // Seçili düğüm değişince çağır: Commit + yeni düğümü Track.
        public void RefreshTrackedNode()
        {
            if (_session == null || _currentNode == null)
                return;
            if (_sink != null)
            {
                try { _session.Commit(_sink); } catch { /* yut */ }
            }

            NodeViewModel? next = null;
            try { next = _currentNode.Invoke(); } catch { /* yut */ }
            if (ReferenceEquals(next, _tracked))
                return;
            _session.Untrack();
            _tracked = next;
            if (_tracked != null)
            {
                try { _session.Track(_tracked); } catch { /* yut */ }
            }
        }

        public void Dispose()
        {
            if (_disposed)
                return;
            _disposed = true;
            Detach();
        }

        private void OnGotFocus(object? sender, GotFocusEventArgs e)
        {
            if (_session == null || _sink == null)
                return;
            Control? input = FindInputControl(e.Source as Visual);
            // Her giriş denetimi kendi geri alma adımıdır: başka alana geçmek önceki
            // oturumu kapatır. Düğme ve diğer odaklar da oturumu kapatır.
            if (input is null || !ReferenceEquals(input, _activeInput))
            {
                try { _session.Commit(_sink); } catch { /* yut */ }
            }
            _activeInput = input;
            if (input is not null)
                _session.Begin();
        }

        private void OnLostFocus(object? sender, RoutedEventArgs e)
        {
            if (_session == null || _root == null || _sink == null)
                return;
            var root = _root;
            var sess = _session;
            var sink = _sink;
            Dispatcher.UIThread.Post(() =>
            {
                if (!ReferenceEquals(sess, _session))
                    return;
                bool inside;
                try { inside = root.IsKeyboardFocusWithin; }
                catch { return; }
                if (!inside)
                {
                    try { sess.Commit(sink); } catch { /* yut */ }
                    _activeInput = null;
                }
            });
        }

        private Control? FindInputControl(Visual? focused)
        {
            for (Visual? cur = focused; cur != null; cur = cur.GetVisualParent())
            {
                if (cur is Control c && IsInputControl(c))
                    return c;
                if (ReferenceEquals(cur, _root))
                    break;
            }
            return null;
        }

        private static bool IsInputControl(Control c) =>
            c is TextBox or ComboBox or CheckBox or ToggleSwitch or Slider or RadioButton or NumericUpDown
                or RangedNumberControl or ColorPickerControl or AssetPickerControl;
    }
}
