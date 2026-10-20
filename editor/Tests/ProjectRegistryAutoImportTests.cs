using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Gerçek ekran denemesi bulgusu (2026-10-20): ilk açılışta örnek proje taraması bir
/// projenin Assets/json klasörünü ("full_story_graph.json" içerdiği için) ayrı bir
/// "json" projesi olarak listeliyordu. Bulunan projenin alt klasörleri proje sayılmaz.
/// </summary>
public sealed class ProjectRegistryAutoImportTests
{
    [Fact]
    public void AutoImport_DoesNotListFoldersInsideAProject()
    {
        string tmp = Path.Combine(Path.GetTempPath(), "RowlRegistry_" + Guid.NewGuid().ToString("N"));
        try
        {
            string samples = Path.Combine(tmp, "samples");
            string proj = Path.Combine(samples, "Test 1");
            Directory.CreateDirectory(Path.Combine(proj, "Assets", "json", ".recovery"));
            File.WriteAllText(Path.Combine(proj, "project.rowlproj"), "{}");
            File.WriteAllText(Path.Combine(proj, "full_story_graph.json"), "{}");
            File.WriteAllText(Path.Combine(proj, "Assets", "json", "full_story_graph.json"), "{}");
            // Proje dosyası olmayan eski tip proje de bulunmaya devam eder.
            string legacy = Path.Combine(samples, "Eski");
            Directory.CreateDirectory(Path.Combine(legacy, "Assets"));
            File.WriteAllText(Path.Combine(legacy, "Assets", "full_story_graph.json"), "{}");

            var registry = new ProjectRegistryService(Path.Combine(tmp, "cfg", "projects.json"), samples);

            Assert.Equal(new[] { "Eski", "Test 1" }, registry.Projects.Select(p => p.Name).OrderBy(n => n, StringComparer.Ordinal));
        }
        finally
        {
            try { Directory.Delete(tmp, recursive: true); } catch { }
        }
    }
}
