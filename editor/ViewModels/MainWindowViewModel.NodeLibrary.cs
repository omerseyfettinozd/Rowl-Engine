using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.NodeLibrary;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>Faz 7 D2 — düğüm kütüphanesinin ana pencere tarafı (şablonu sahneye ekleme).</summary>
    public partial class MainWindowViewModel
    {
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
