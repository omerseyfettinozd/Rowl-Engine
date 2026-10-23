using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.NodeLibrary;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>Faz 7 — düğüm kütüphanesi (D2: şablonu sahneye ekleme) ve Lua editörü (D5) panellerinin ana pencere tarafı.</summary>
    public partial class MainWindowViewModel
    {
        private LuaEditorViewModel? _luaEditor;
        private NodeLibraryViewModel? _nodeLibrary;

        /// <summary>Faz 7 D3 — düğüm kütüphanesi paneli ve Denetçi bölümü (ortak durum); ilk erişimde yüklenir.</summary>
        public NodeLibraryViewModel NodeLibrary => _nodeLibrary ??= new NodeLibraryViewModel(this);

        /// <summary>Faz 7 D5 — Lua editörü paneli (projenin Assets altındaki .lua dosyaları); ilk erişimde kurulur.</summary>
        public LuaEditorViewModel LuaEditor => _luaEditor ??= new LuaEditorViewModel(this);

        /// <summary>Kullanıcı düzeyi kütüphane dosyası: editör ayar profiliyle aynı dizin.</summary>
        public string NodeLibraryPath { get; } = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "RowlEngine", NodeLibraryStore.FileName);

        /// <summary>
        /// Kütüphane kayıtlarının proje anahtarı: projenin kalıcı UUID'si, yoksa proje kökü.
        /// Aynı düğüm kimliği başka projede başka düğümdür; yıldız durumu bununla ayrılır.
        /// </summary>
        public string ProjectLibraryKey =>
            ProjectIdentityService.TryReadStoredUuid(Path.Combine(ProjectRoot, "project.rowlproj"), out _)
            ?? ProjectRoot;

        /// <summary>Kütüphanede seçili düğümün kaynak anahtarı (<c>null</c> = seçim yok).</summary>
        public string? SelectedNodeLibraryKey =>
            SelectedNode is { } node ? NodeTemplateConverter.SourceKey(ProjectLibraryKey, node.Id) : null;

        /// <summary>
        /// Şablondan yeni bir düğümü görünür alanın ortasına ekler, seçer ve geri alınabilir kaydeder
        /// (<see cref="AddNode"/> ile aynı yol). Tanınmayan bileşen türleri günlüğe yazılır.
        /// </summary>
        public NodeViewModel AddNodeFromTemplate(NodeTemplate template)
        {
            ulong nextId = Nodes.Count > 0 ? Nodes.Max(n => n.Id) + 1 : 101;
            var (spawnX, spawnY) = StoryGraphCanvasService.CalculateSpawnPosition(Nodes.Count, PanX, PanY, ZoomScale);
            var result = NodeTemplateConverter.Instantiate(template, nextId, spawnX, spawnY);
            NodeViewModel node = result.Node;

            node.PropertyChanged += OnNodePropertyChanged;
            Nodes.Add(node);
            SelectedNode = node;
            UndoRedoService.Instance.RecordAction(new AddNodeUndoAction(this, node));
            UpdateStartNodeState();
            ScheduleSave();
            AppendLog($"Added node #{nextId} from library template '{template.Title}'");
            if (result.SkippedComponentTypes.Count > 0)
                AppendLog($"[Library] Skipped unknown component type(s): {string.Join(", ", result.SkippedComponentTypes)}");
            return node;
        }
    }
}
