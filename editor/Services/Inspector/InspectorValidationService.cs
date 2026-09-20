using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.IO;
using System.Linq;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services.Inspector
{
    /// <summary>
    /// Faz 4 Dilim 4 — reactive field-level validation. Attaches to the live
    /// node/connection collections and recomputes the affected node(s) on
    /// every relevant property change (typing in the Inspector re-evaluates
    /// that node immediately; cross-node rules keep incremental indexes).
    /// Issues are <see cref="ProjectValidationIssue"/> records — the same
    /// type the Issues panel shows — so the Dilim 5 linter can merge inline
    /// and batch results without conversion. Field keys come from
    /// <see cref="InspectorFieldDescriptor.Keys"/> for badge binding.
    /// <para>
    /// Severity mirrors <c>ProjectValidationService</c>: malformed/duplicate
    /// content ids, dangling option targets and unresolvable asset paths are
    /// errors; empty speaker/text/title, isolated nodes and terminal notes
    /// are warnings. Unlike the batch validator, inline checks run on every
    /// node regardless of start-node reachability (authoring strictness).
    /// </para>
    /// </summary>
    public sealed class InspectorValidationService
    {
        private static readonly string[] AssetKeys =
            { "texture", "sprite", "bgm_track", "sfx_track", "path", "typewriter_sound", "custom_box_texture" };

        /// <summary>
        /// Component types whose Serialize() can emit an <see cref="AssetKeys"/>
        /// entry (verified against every component VM). Other types skip the
        /// serialize pass entirely — their rules run on typed properties.
        /// </summary>
        private static readonly HashSet<string> AssetCarrierTypes = new(StringComparer.Ordinal)
            { "background", "character", "audio", "dialogue", "script" };

        private static readonly Dictionary<(string component, string key), string> AssetFieldKeys =
            new()
            {
                [("background", "texture")] = InspectorFieldDescriptor.Keys.BackgroundTexture,
                [("character", "sprite")] = InspectorFieldDescriptor.Keys.CharacterSprite,
                [("audio", "bgm_track")] = InspectorFieldDescriptor.Keys.AudioBgmTrack,
                [("audio", "sfx_track")] = InspectorFieldDescriptor.Keys.AudioSfxTrack,
                [("dialogue", "typewriter_sound")] = InspectorFieldDescriptor.Keys.DialogueBlipSound,
                [("character", "voice_blip_sound")] = InspectorFieldDescriptor.Keys.CharacterVoiceBlip,
                [("script", "path")] = InspectorFieldDescriptor.Keys.ScriptPath,
            };

        private sealed record Entry(
            ProjectValidationIssue Issue, string FieldKey, ulong NodeId, string? DedupKey = null);

        private readonly List<Entry> _entries = new();
        private readonly Dictionary<NodeComponentViewModel, NodeViewModel> _componentOwner = new();
        private readonly Dictionary<ChoiceOptionViewModel, NodeViewModel> _optionOwner = new();
        private readonly Dictionary<ChoiceComponentViewModel, NodeViewModel> _choiceOwner = new();
        private readonly Dictionary<FrameObjectViewModel, NodeViewModel> _frameOwner = new();
        private readonly Dictionary<ulong, NodeViewModel> _nodeById = new();
        private readonly Dictionary<string, HashSet<NodeViewModel>> _contentIndex =
            new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<NodeViewModel, HashSet<string>> _nodeContentIds = new();
        private readonly Dictionary<string, List<Entry>> _dupEntriesById =
            new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<ulong, int> _inDegree = new();
        private readonly Dictionary<ulong, int> _outDegree = new();
        private readonly HashSet<string> _exactPaths = new(StringComparer.Ordinal);
        private readonly HashSet<string> _insensitivePaths = new(StringComparer.OrdinalIgnoreCase);

        private ObservableCollection<NodeViewModel>? _nodes;
        private ObservableCollection<ConnectionViewModel>? _connections;
        private Func<string>? _assetsPathProvider;

        private readonly ObservableCollection<ProjectValidationIssue> _issues = new();

        /// <summary>
        /// All live inline issues (suitable for the Issues panel). Flushed
        /// on access, like every other read on this service.
        /// </summary>
        public ObservableCollection<ProjectValidationIssue> Issues
        {
            get
            {
                Flush();
                return _issues;
            }
        }

        /// <summary>Raised after any issue-set mutation (badge refresh).</summary>
        public event EventHandler? Changed;

        public bool HasErrors
        {
            get
            {
                Flush();
                return _entries.Any(e => e.Issue.IsError);
            }
        }

        public int IssueCount
        {
            get
            {
                Flush();
                return _entries.Count;
            }
        }

        /// <summary>Attaches live collections; runs a full initial pass.</summary>
        public void Attach(
            ObservableCollection<NodeViewModel> nodes,
            ObservableCollection<ConnectionViewModel>? connections = null,
            Func<string>? assetsPathProvider = null)
        {
            Detach();
            _nodes = nodes ?? throw new ArgumentNullException(nameof(nodes));
            _connections = connections;
            _assetsPathProvider = assetsPathProvider;
            _nodes.CollectionChanged += OnNodesChanged;
            foreach (var node in _nodes)
                HookNode(node);
            if (_connections is not null)
                _connections.CollectionChanged += OnConnectionsChanged;
            RefreshAssets();
            MarkAllDirty();
            Flush();
        }

        public void Detach()
        {
            if (_nodes is not null)
            {
                _nodes.CollectionChanged -= OnNodesChanged;
                foreach (var node in _nodes.ToList())
                    UnhookNode(node);
            }
            if (_connections is not null)
                _connections.CollectionChanged -= OnConnectionsChanged;
            _nodes = null;
            _connections = null;
            ClearAllEntries();
            _contentIndex.Clear();
            _inDegree.Clear();
            _outDegree.Clear();
            _componentOwner.Clear();
            _optionOwner.Clear();
            _choiceOwner.Clear();
            _frameOwner.Clear();
            _nodeById.Clear();
        }

        /// <summary>Re-scans the Assets tree for existence checks.</summary>
        public void RefreshAssets()
        {
            _exactPaths.Clear();
            _insensitivePaths.Clear();
            MarkAllDirty();
            string? root = _assetsPathProvider?.Invoke();
            if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
                return;
            try
            {
                string full = Path.GetFullPath(root);
                foreach (string file in Directory.EnumerateFiles(full, "*", SearchOption.AllDirectories))
                {
                    string rel = Path.GetRelativePath(full, file).Replace('\\', '/');
                    _exactPaths.Add(rel);
                    _insensitivePaths.Add(rel);
                }
            }
            catch (Exception)
            {
                // Fail-soft: stale index keeps resolving what it knows; the
                // batch validator reports scan failures authoritatively.
            }
        }

        /// <summary>Full recompute (load, connection edits, asset refresh).</summary>
        public void ValidateAll()
        {
            if (_nodes is null)
                return;
            ClearAllEntries();
            _contentIndex.Clear();
            _nodeContentIds.Clear();
            _dupEntriesById.Clear();
            RebuildNodeIndex();
            RebuildDegrees();
            MarkAllDirty();
            Flush();
        }

        /// <summary>Issues for one node (badges + inspector section).</summary>
        public IReadOnlyList<ProjectValidationIssue> IssuesFor(ulong nodeId)
        {
            Flush();
            return _entries.Where(e => e.NodeId == nodeId).Select(e => e.Issue).ToList();
        }

        /// <summary>Issues for one node field (badge binding).</summary>
        public IReadOnlyList<ProjectValidationIssue> IssuesFor(ulong nodeId, string fieldKey)
        {
            Flush();
            return _entries
                .Where(e => e.NodeId == nodeId &&
                    string.Equals(e.FieldKey, fieldKey, StringComparison.Ordinal))
                .Select(e => e.Issue)
                .ToList();
        }

        // Lazy dirty tracking (Dilim 2 search-index pattern)
        //
        // Collection/property events only MARK nodes dirty (O(1)); the
        // expensive per-node pass runs on the next read (badge query,
        // Issues, HasErrors) or explicit ValidateAll. Bulk loads of
        // thousands of nodes/edges therefore stay interactive while
        // keystroke edits still re-evaluate before the badge paints.

        private readonly HashSet<NodeViewModel> _dirty = new();

        private void MarkDirty(NodeViewModel? node)
        {
            if (node is not null)
                _dirty.Add(node);
        }

        private void MarkAllDirty()
        {
            if (_nodes is null)
                return;
            foreach (var node in _nodes)
                _dirty.Add(node);
        }

        /// <summary>Validates every dirty node still in the collection.</summary>
        public void Flush()
        {
            if (_nodes is null || _dirty.Count == 0)
                return;
            var pending = _dirty.ToList();
            _dirty.Clear();
            foreach (var node in pending)
            {
                if (!_nodes.Contains(node))
                    continue;
                RemoveNodeEntries(node, keepDuplicates: true);
                try
                {
                    RevalidateNodeBody(node);
                }
                catch (Exception)
                {
                    // Fail-soft per node: one hostile component must never
                    // wedge the whole flush (entries for it stay cleared).
                }
            }
            if (pending.Count > 0)
                Changed?.Invoke(this, EventArgs.Empty);
        }

        // Incremental core

        /// <summary>
        /// Immediate per-node pass (flush path only). Callers must have
        /// removed the node's entries first; duplicate flags refresh for
        /// the ids this node touched.
        /// </summary>
        private void RevalidateNodeBody(NodeViewModel node)
        {
            // Node-level rules.
            if (string.IsNullOrWhiteSpace(node.Title))
                Add(node, InspectorFieldDescriptor.Keys.NodeTitle, false,
                    $"Düğüm #{node.Id} başlığı boş; tuvalde ve aramada bulunması zorlaşır.");
            int degree = DegreeOf(node.Id);
            if (_nodes is not null && _nodes.Count > 1 && degree == 0)
                Add(node, InspectorFieldDescriptor.Keys.NodePorts, false,
                    $"Düğüm #{node.Id} bağlantısız; hikâye akışına kabloyla bağlayın.");

            foreach (var dialogue in node.AllComponents.OfType<DialogueComponentViewModel>())
            {
                if (!dialogue.IsEnabled)
                    continue;
                if (string.IsNullOrWhiteSpace(dialogue.DialogueText))
                    Add(node, InspectorFieldDescriptor.Keys.DialogueText, false,
                        $"Düğüm #{node.Id} diyalog metni boş.");
                else if (string.IsNullOrWhiteSpace(dialogue.Speaker))
                    Add(node, InspectorFieldDescriptor.Keys.DialogueSpeaker, false,
                        $"Düğüm #{node.Id} konuşmacısı boş.");
                string raw = dialogue.ContentId ?? string.Empty;
                if (string.IsNullOrWhiteSpace(raw))
                    continue;
                if (ContentIdService.Normalize(raw) is null)
                    Add(node, InspectorFieldDescriptor.Keys.DialogueContentId, true,
                        $"Düğüm #{node.Id} content_id '{raw}' geçersiz; UUID biçimi (8-4-4-4-12) bekleniyor.");
            }

            foreach (var choice in node.AllComponents.OfType<ChoiceComponentViewModel>())
            {
                if (!choice.IsEnabled)
                    continue;
                foreach (var option in choice.Options)
                {
                    if (!option.IsEnabled)
                        continue;
                    if (string.IsNullOrWhiteSpace(option.Text))
                        Add(node, InspectorFieldDescriptor.Keys.ChoiceOptionText, false,
                            $"Düğüm #{node.Id} seçeneğinin düğme metni boş.");
                    if (option.TargetNodeId == 0)
                        Add(node, InspectorFieldDescriptor.Keys.ChoiceOptionTarget, true,
                            $"Düğüm #{node.Id} seçeneği '{option.Text}' hedef düğüme bağlı değil (hedef 0).");
                }
            }

            // Asset rules mirror the batch validator: enabled components,
            // known asset data keys, blank = optional silence. Serialize is
            // skipped for types that can never emit an asset key.
            foreach (var component in node.AllComponents.Where(c =>
                c.IsEnabled && AssetCarrierTypes.Contains(c.TypeKey)))
            {
                Dictionary<string, object> data;
                try
                {
                    data = component.Serialize();
                }
                catch (Exception)
                {
                    continue;
                }
                foreach (var pair in data)
                {
                    if (!AssetKeys.Contains(pair.Key) || pair.Value is not string asset)
                        continue;
                    if (string.IsNullOrWhiteSpace(asset))
                        continue;
                    string? message = InspectorAssetRules.CheckSingleReference(
                        asset, _exactPaths, _insensitivePaths,
                        _assetsPathProvider?.Invoke());
                    if (message is not null)
                        Add(node, AssetFieldKey(component.TypeKey, pair.Key), true,
                            $"Düğüm #{node.Id}: {message} ({component.DisplayName}).");
                }
            }

            // Incremental duplicate tracking: only ids whose owner set
            // changed (old ∪ new for this node) get their flags recomputed.
            var affected = UpdateContentIndex(node);
            if (affected.Count > 0)
                RefreshDuplicateIds(affected);
        }

        private static string AssetFieldKey(string componentType, string dataKey) =>
            AssetFieldKeys.TryGetValue((componentType, dataKey), out string? field)
                ? field
                : $"{componentType}.{dataKey}";

        /// <summary>
        /// Diffs the node's indexed content ids against its stored set,
        /// updates the reverse index and returns the ids whose owner set
        /// changed (empty = duplicate flags provably unaffected).
        /// </summary>
        private HashSet<string> UpdateContentIndex(NodeViewModel node)
        {
            var current = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var dialogue in node.AllComponents.OfType<DialogueComponentViewModel>())
            {
                if (!dialogue.IsEnabled)
                    continue;
                string? normalized = ContentIdService.Normalize(dialogue.ContentId ?? string.Empty);
                if (normalized is not null)
                    current.Add(normalized);
            }
            _nodeContentIds.TryGetValue(node, out var previous);
            previous ??= new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            // Affected = ids whose owner set changed for this node.
            var affected = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string dropped in previous.Where(id => !current.Contains(id)))
            {
                if (_contentIndex.TryGetValue(dropped, out var set))
                {
                    set.Remove(node);
                    if (set.Count == 0)
                        _contentIndex.Remove(dropped);
                }
                affected.Add(dropped);
            }
            foreach (string added in current.Where(id => !previous.Contains(id)))
            {
                if (!_contentIndex.TryGetValue(added, out var set))
                {
                    set = new HashSet<NodeViewModel>();
                    _contentIndex[added] = set;
                }
                set.Add(node);
                affected.Add(added);
            }
            _nodeContentIds[node] = current;
            return affected;
        }

        private void RefreshDuplicateIds(IEnumerable<string> ids)
        {
            foreach (string normalized in ids)
            {
                if (_dupEntriesById.TryGetValue(normalized, out var stale))
                {
                    foreach (var entry in stale.ToList())
                        RemoveEntryAt(_entries.IndexOf(entry));
                    _dupEntriesById.Remove(normalized);
                }
                if (!_contentIndex.TryGetValue(normalized, out var owners) || owners.Count < 2)
                    continue;
                ulong first = owners.Min(n => n.Id);
                var fresh = new List<Entry>();
                foreach (var owner in owners.Where(n => n.Id != first).OrderBy(n => n.Id))
                {
                    var issue = new ProjectValidationIssue(true,
                        $"Düğüm #{owner.Id} mükerrer content_id '{normalized}' taşıyor (ilk kullanım düğüm #{first}); her diyalog tekil kimlik ister.",
                        owner.Id);
                    var entry = new Entry(
                        issue, InspectorFieldDescriptor.Keys.DialogueContentId, owner.Id, normalized);
                    InsertEntry(entry);
                    fresh.Add(entry);
                }
                if (fresh.Count > 0)
                    _dupEntriesById[normalized] = fresh;
            }
        }

        private void RebuildDegrees()
        {
            _inDegree.Clear();
            _outDegree.Clear();
            if (_nodes is not null)
                foreach (var node in _nodes)
                {
                    _inDegree[node.Id] = 0;
                    _outDegree[node.Id] = 0;
                }
            if (_connections is not null)
                foreach (var connection in _connections)
                {
                    if (connection.SourceNode is null || connection.TargetNode is null)
                        continue;
                    if (_outDegree.ContainsKey(connection.SourceNode.Id))
                        _outDegree[connection.SourceNode.Id]++;
                    if (_inDegree.ContainsKey(connection.TargetNode.Id))
                        _inDegree[connection.TargetNode.Id]++;
                }
        }

        private int DegreeOf(ulong nodeId) =>
            (_inDegree.TryGetValue(nodeId, out int incoming) ? incoming : 0) +
            (_outDegree.TryGetValue(nodeId, out int outgoing) ? outgoing : 0);

        /// <summary>First-wins id map (duplicate ids never throw here).</summary>
        private void RebuildNodeIndex()
        {
            _nodeById.Clear();
            if (_nodes is null)
                return;
            foreach (var node in _nodes)
                _nodeById.TryAdd(node.Id, node);
        }

        private bool TryGetNode(ulong nodeId, out NodeViewModel node) =>
            _nodeById.TryGetValue(nodeId, out node!);

        // Entry bookkeeping

        private void Add(NodeViewModel node, string fieldKey, bool isError, string message)
        {
            var issue = new ProjectValidationIssue(isError, message, node.Id);
            InsertEntry(new Entry(issue, fieldKey, node.Id));
        }

        private void InsertEntry(Entry entry)
        {
            _entries.Add(entry);
            _issues.Add(entry.Issue);
            Changed?.Invoke(this, EventArgs.Empty);
        }

        private void RemoveNodeEntries(NodeViewModel node, bool keepDuplicates)
        {
            for (int i = _entries.Count - 1; i >= 0; i--)
            {
                if (_entries[i].NodeId != node.Id)
                    continue;
                if (keepDuplicates &&
                    _entries[i].FieldKey == InspectorFieldDescriptor.Keys.DialogueContentId &&
                    _entries[i].Issue.Message.Contains("mükerrer", StringComparison.Ordinal))
                    continue;
                RemoveEntryAt(i);
            }
        }

        private void RemoveEntryAt(int index)
        {
            var entry = _entries[index];
            _entries.RemoveAt(index);
            _issues.Remove(entry.Issue);
            if (entry.DedupKey is not null &&
                _dupEntriesById.TryGetValue(entry.DedupKey, out var bucket))
            {
                bucket.Remove(entry);
                if (bucket.Count == 0)
                    _dupEntriesById.Remove(entry.DedupKey);
            }
            Changed?.Invoke(this, EventArgs.Empty);
        }

        private void ClearAllEntries()
        {
            if (_entries.Count == 0 && _issues.Count == 0)
                return;
            _entries.Clear();
            _issues.Clear();
            Changed?.Invoke(this, EventArgs.Empty);
        }

        // Subscriptions (leak-safe: removals unhook via OldItems)

        private void HookNode(NodeViewModel node)
        {
            node.PropertyChanged += OnNodePropertyChanged;
            node.Objects.CollectionChanged += OnFrameObjectsChanged;
            foreach (var frame in node.Objects)
                HookFrame(node, frame);
        }

        private void UnhookNode(NodeViewModel node)
        {
            node.PropertyChanged -= OnNodePropertyChanged;
            node.Objects.CollectionChanged -= OnFrameObjectsChanged;
            foreach (var frame in node.Objects.ToList())
                UnhookFrame(frame);
        }

        private void HookFrame(NodeViewModel node, FrameObjectViewModel frame)
        {
            _frameOwner[frame] = node;
            frame.PropertyChanged += OnFramePropertyChanged;
            frame.Components.CollectionChanged += OnFrameComponentsChanged;
            foreach (var component in frame.Components)
                HookComponent(node, component);
        }

        private void UnhookFrame(FrameObjectViewModel frame)
        {
            frame.PropertyChanged -= OnFramePropertyChanged;
            frame.Components.CollectionChanged -= OnFrameComponentsChanged;
            foreach (var component in frame.Components.ToList())
                UnhookComponent(component);
            _frameOwner.Remove(frame);
        }

        private void HookComponent(NodeViewModel node, NodeComponentViewModel component)
        {
            _componentOwner[component] = node;
            component.PropertyChanged += OnComponentPropertyChanged;
            if (component is ChoiceComponentViewModel choice)
            {
                _choiceOwner[choice] = node;
                choice.Options.CollectionChanged += OnChoiceOptionsChanged;
                foreach (var option in choice.Options)
                    HookOption(node, option);
            }
        }

        private void UnhookComponent(NodeComponentViewModel component)
        {
            component.PropertyChanged -= OnComponentPropertyChanged;
            _componentOwner.Remove(component);
            if (component is ChoiceComponentViewModel choice)
            {
                choice.Options.CollectionChanged -= OnChoiceOptionsChanged;
                foreach (var option in choice.Options.ToList())
                    UnhookOption(option);
                _choiceOwner.Remove(choice);
            }
        }

        private void HookOption(NodeViewModel node, ChoiceOptionViewModel option)
        {
            _optionOwner[option] = node;
            option.PropertyChanged += OnOptionPropertyChanged;
        }

        private void UnhookOption(ChoiceOptionViewModel option)
        {
            option.PropertyChanged -= OnOptionPropertyChanged;
            _optionOwner.Remove(option);
        }

        private void OnChoiceOptionsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (sender is not ObservableCollection<ChoiceOptionViewModel>)
                return;
            NodeViewModel? owner = null;
            foreach (var (choice, node) in _choiceOwner)
            {
                if (ReferenceEquals(choice.Options, sender))
                {
                    owner = node;
                    break;
                }
            }
            if (owner is null)
                return;
            if (e.OldItems is not null)
                foreach (ChoiceOptionViewModel option in e.OldItems)
                    UnhookOption(option);
            if (e.NewItems is not null)
                foreach (ChoiceOptionViewModel option in e.NewItems)
                    HookOption(owner, option);
            MarkDirty(owner);
        }

        private void OnOptionPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (sender is ChoiceOptionViewModel option &&
                _optionOwner.TryGetValue(option, out var node))
                MarkDirty(node);
        }

        private void OnNodesChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (_nodes is null)
                return;
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                foreach (var known in _componentOwner.Keys.ToList())
                    UnhookComponent(known);
                foreach (var option in _optionOwner.Keys.ToList())
                    UnhookOption(option);
                foreach (var frame in _frameOwner.Keys.ToList())
                    UnhookFrame(frame);
                foreach (var node in _nodes)
                    HookNode(node);
                ValidateAll();
                return;
            }
            if (e.OldItems is not null)
            {
                var removedIds = new HashSet<ulong>();
                var affectedContent = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (NodeViewModel node in e.OldItems)
                {
                    UnhookNode(node);
                    RemoveNodeEntries(node, keepDuplicates: false);
                    if (_nodeContentIds.TryGetValue(node, out var stored))
                    {
                        foreach (string id in stored)
                        {
                            affectedContent.Add(id);
                            if (_contentIndex.TryGetValue(id, out var set))
                            {
                                set.Remove(node);
                                if (set.Count == 0)
                                    _contentIndex.Remove(id);
                            }
                        }
                        _nodeContentIds.Remove(node);
                    }
                    removedIds.Add(node.Id);
                    _inDegree.Remove(node.Id);
                    _outDegree.Remove(node.Id);
                    // Re-point the id map when a same-id sibling survives.
                    _nodeById.Remove(node.Id);
                    foreach (var sibling in _nodes)
                    {
                        if (sibling.Id == node.Id)
                        {
                            _nodeById.TryAdd(sibling.Id, sibling);
                            break;
                        }
                    }
                }
                // Only former neighbors change degree: collect them in one
                // edge scan instead of revalidating the whole graph.
                var neighbors = new HashSet<ulong>();
                if (_connections is not null)
                    foreach (var connection in _connections)
                    {
                        if (connection.SourceNode is null || connection.TargetNode is null)
                            continue;
                        ulong from = connection.SourceNode.Id, to = connection.TargetNode.Id;
                        if (removedIds.Contains(from) && !removedIds.Contains(to))
                            neighbors.Add(to);
                        else if (removedIds.Contains(to) && !removedIds.Contains(from))
                            neighbors.Add(from);
                    }
                RebuildDegrees();
                foreach (ulong neighbor in neighbors)
                    if (TryGetNode(neighbor, out var node))
                        MarkDirty(node);
                // A removed first-owner clears or re-points duplicate flags.
                if (affectedContent.Count > 0)
                    RefreshDuplicateIds(affectedContent);
                return;
            }
            if (e.NewItems is not null)
                foreach (NodeViewModel node in e.NewItems)
                {
                    HookNode(node);
                    _nodeById.TryAdd(node.Id, node);
                    // Absent-guard: edges may predate the node (degrees
                    // already bumped by the connection path); never reset.
                    if (!_inDegree.ContainsKey(node.Id))
                        _inDegree[node.Id] = 0;
                    if (!_outDegree.ContainsKey(node.Id))
                        _outDegree[node.Id] = 0;
                    MarkDirty(node);
                }
        }

        private void OnConnectionsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (_nodes is null)
                return;
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                RebuildDegrees();
                MarkAllDirty();
                return;
            }
            // Endpoint marking only: wires change nothing but degrees, and
            // the flush recomputes the two ends. Bulk cable loads stay O(1)
            // per wire with zero Serialize calls.
            if (e.OldItems is not null)
                foreach (ConnectionViewModel connection in e.OldItems)
                    CollectEndpoints(connection, decrement: true);
            if (e.NewItems is not null)
                foreach (ConnectionViewModel connection in e.NewItems)
                    CollectEndpoints(connection, decrement: false);
        }

        private void CollectEndpoints(ConnectionViewModel connection, bool decrement)
        {
            foreach (var end in new[] { connection.SourceNode, connection.TargetNode })
            {
                if (end is null)
                    continue;
                bool isSource = ReferenceEquals(end, connection.SourceNode);
                var degrees = isSource ? _outDegree : _inDegree;
                if (!degrees.ContainsKey(end.Id))
                    degrees[end.Id] = 0;
                degrees[end.Id] = Math.Max(0, degrees[end.Id] + (decrement ? -1 : 1));
                MarkDirty(end);
            }
        }

        private void OnFrameObjectsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (_nodes is null)
                return;
            foreach (var node in _nodes.ToList())
            {
                if (!ReferenceEquals(node.Objects, sender))
                    continue;
                if (e.OldItems is not null)
                    foreach (FrameObjectViewModel frame in e.OldItems)
                        UnhookFrame(frame);
                if (e.NewItems is not null)
                    foreach (FrameObjectViewModel frame in e.NewItems)
                        HookFrame(node, frame);
                if (e.Action == NotifyCollectionChangedAction.Reset)
                {
                    foreach (var known in _frameOwner.Keys.ToList())
                        UnhookFrame(known);
                    foreach (var frame in node.Objects)
                        HookFrame(node, frame);
                }
                MarkDirty(node);
                return;
            }
        }

        private void OnFrameComponentsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (sender is not System.Collections.ObjectModel.ObservableCollection<NodeComponentViewModel> components)
                return;
            NodeViewModel? owner = null;
            foreach (var (frame, node) in _frameOwner)
            {
                if (ReferenceEquals(frame.Components, components))
                {
                    owner = node;
                    break;
                }
            }
            if (owner is null)
                return;
            if (e.OldItems is not null)
                foreach (NodeComponentViewModel component in e.OldItems)
                    UnhookComponent(component);
            if (e.NewItems is not null)
                foreach (NodeComponentViewModel component in e.NewItems)
                    HookComponent(owner, component);
            MarkDirty(owner);
        }

        private void OnFramePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            // IsActive toggles change the validated set (disabled skipped).
            if (sender is FrameObjectViewModel frame &&
                _frameOwner.TryGetValue(frame, out var node))
                MarkDirty(node);
        }

        private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (sender is NodeViewModel node && _nodes is not null && _nodes.Contains(node))
                MarkDirty(node);
        }

        private void OnComponentPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (sender is NodeComponentViewModel component &&
                _componentOwner.TryGetValue(component, out var node))
                MarkDirty(node);
        }
    }
}
