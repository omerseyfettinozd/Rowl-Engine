using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Nodes;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services.NodeLibrary;

/// <summary>
/// Faz 7 D2 — düğüm ↔ kütüphane şablonu. Bileşenler kayıt dosyasıyla aynı biçimde
/// tutulur (<c>type</c> / <c>enabled</c> / <c>data</c> = <see cref="NodeComponentViewModel.Serialize"/>)
/// ve yükleyicinin kendi yolundan (<see cref="StoryGraphComponentHydrator"/>) geri kurulur;
/// böylece şablon için ikinci bir serileştirme yolu yoktur.
/// </summary>
public static class NodeTemplateConverter
{
    /// <summary>Düğümün başlığı, objeleri (ad, görünürlük) ve bileşen değerleri. Bağlantılar dahil değil.</summary>
    public static NodeTemplate Capture(NodeViewModel node)
    {
        var objects = new List<TemplateObject>(node.Objects.Count);
        foreach (FrameObjectViewModel obj in node.Objects)
        {
            var components = new List<TemplateComponent>(obj.Components.Count);
            foreach (NodeComponentViewModel component in obj.Components)
            {
                JsonObject values = JsonSerializer.SerializeToNode(component.Serialize()) as JsonObject ?? new JsonObject();
                components.Add(new TemplateComponent(component.TypeKey, values, component.IsEnabled));
            }
            objects.Add(new TemplateObject(obj.Name, obj.IsActive, components));
        }
        return new NodeTemplate(node.Title, objects);
    }

    /// <summary>Sahneye eklenen kopyanın sonucu.</summary>
    public sealed record InstantiateResult(NodeViewModel Node, IReadOnlyList<string> SkippedComponentTypes);

    /// <summary>
    /// Şablondan yeni bir düğüm kurar: yeni kimlik, yeni bileşen kimlikleri ve yeni diyalog
    /// <c>content_id</c>'leri (çoğaltma kuralı: kopya yeni içeriktir). Bağlantı hedefleri şablona
    /// kaydedilirken zaten boşaltılmıştır (<see cref="NodeLibraryOperations.Reusable"/>).
    /// Bu sürümün tanımadığı bileşen türleri atlanır ve bildirilir.
    /// </summary>
    public static InstantiateResult Instantiate(NodeTemplate template, ulong id, double x, double y)
    {
        var node = new NodeViewModel(id, template.Title, x, y, bare: true);
        var skipped = new List<string>();
        foreach (TemplateObject source in template.Objects)
        {
            FrameObjectViewModel obj = node.CreateObject(source.Name);
            obj.IsActive = source.IsActive;
            foreach (TemplateComponent component in source.Components)
            {
                var json = new JsonObject
                {
                    ["type"] = component.TypeKey,
                    ["enabled"] = component.Enabled,
                    ["data"] = component.Values.DeepClone(),
                };
                using JsonDocument document = JsonDocument.Parse(json.ToJsonString());
                if (!StoryGraphComponentHydrator.TryCreate(document.RootElement, out NodeComponentViewModel? created, out _)
                    || created is null)
                {
                    skipped.Add(component.TypeKey);
                    continue;
                }
                if (created is DialogueComponentViewModel dialogue)
                    dialogue.ContentId = ContentIdService.NewContentId();
                obj.AddComponent(created);
            }
        }
        return new InstantiateResult(node, skipped);
    }

    /// <summary>
    /// Kütüphane kaydının kaynak anahtarı: proje kimliği (yoksa proje kökü) + düğüm kimliği.
    /// Denetçi'deki yıldız ve "Kaydı güncelle" bu anahtarla kaydı bulur.
    /// </summary>
    public static string SourceKey(string projectKey, ulong nodeId) => $"{projectKey}:{nodeId}";
}
