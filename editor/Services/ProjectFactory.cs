using System;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Models;

namespace RowlEngine.Editor.Services
{
    public static class ProjectFactory
    {
        public static (bool Success, string? Error, ProjectInfo? Info) CreateNewProject(string name, string parentFolder)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(name))
                    return (false, "Proje adı boş olamaz.", null);

                if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
                    return (false, "Proje adı geçersiz karakterler içeriyor.", null);

                if (!Directory.Exists(parentFolder))
                {
                    try { Directory.CreateDirectory(parentFolder); }
                    catch (Exception ex) { return (false, $"Hedef dizin oluşturulamadı: {ex.Message}", null); }
                }

                string projectDir = Path.Combine(parentFolder, name);
                if (Directory.Exists(projectDir))
                    return (false, "Bu isimde bir proje klasörü zaten mevcut.", null);

                Directory.CreateDirectory(projectDir);

                // Alt klasörler
                string assetsDir = Path.Combine(projectDir, "Assets");
                string imagesDir = Path.Combine(assetsDir, "images");
                string jsonDir = Path.Combine(assetsDir, "json");
                string packagesDir = Path.Combine(assetsDir, "packages");

                Directory.CreateDirectory(assetsDir);
                Directory.CreateDirectory(imagesDir);
                Directory.CreateDirectory(jsonDir);
                Directory.CreateDirectory(packagesDir);

                File.WriteAllText(Path.Combine(imagesDir, ".gitkeep"), "");
                File.WriteAllText(Path.Combine(packagesDir, ".gitkeep"), "");

                // Standart başlangıç hikaye grafiği
                string starterGraph = @"{
  ""project_name"": """ + name + @""",
  ""version"": ""1.0.0"",
  ""start_node_id"": 101,
  ""nodes"": [
    {
      ""id"": 101,
      ""title"": ""Giriş Sahnesi"",
      ""speaker"": ""Narrator"",
      ""dialogue"": ""Rowl Engine dünyasına hoş geldiniz! Burası hikayenizin başlangıcı."",
      ""background"": """",
      ""character_sprite"": """",
      ""character_x"": 0.0,
      ""character_y"": 0.0,
      ""character_width"": 360.0,
      ""character_height"": 540.0,
      ""character_scale"": 1.0,
      ""dialogue_box_x"": 80.0,
      ""dialogue_box_y"": 860.0,
      ""dialogue_box_width"": 1760.0,
      ""dialogue_box_height"": 180.0,
      ""dialogue_box_scale"": 1.0,
      ""dsp_filter"": ""Normal"",
      ""x"": 100.0,
      ""y"": 100.0
    }
  ],
  ""connections"": []
}";

                string starterActive = @"{
  ""node_id"": 101,
  ""scene_id"": ""scene_101"",
  ""audio_dsp"": ""Normal"",
  ""components"": [
    {
      ""type"": ""DialogueComponent"",
      ""speaker"": ""Narrator"",
      ""text"": ""Rowl Engine dünyasına hoş geldiniz! Burası hikayenizin başlangıcı."",
      ""box_rect"": { ""x"": 80, ""y"": 860, ""w"": 1760, ""h"": 180 }
    }
  ]
}";

                File.WriteAllText(Path.Combine(projectDir, "full_story_graph.json"), starterGraph);
                File.WriteAllText(Path.Combine(projectDir, "active_story.json"), starterActive);
                File.WriteAllText(Path.Combine(jsonDir, "full_story_graph.json"), starterGraph);
                File.WriteAllText(Path.Combine(jsonDir, "active_story.json"), starterActive);

                // project.rowlproj
                var manifest = new
                {
                    name = name,
                    version = "1.0.0",
                    engineVersion = "1.0.0",
                    createdAt = DateTime.UtcNow.ToString("o"),
                    savedAt = DateTime.UtcNow.ToString("o"),
                    nodeCount = 1,
                    startNodeId = 101
                };
                var opts = new JsonSerializerOptions { WriteIndented = true };
                File.WriteAllText(Path.Combine(projectDir, "project.rowlproj"), JsonSerializer.Serialize(manifest, opts));

                var info = new ProjectInfo
                {
                    Id = Guid.NewGuid().ToString("N"),
                    Name = name,
                    Path = projectDir,
                    CreatedAt = DateTime.UtcNow,
                    LastOpenedAt = DateTime.UtcNow
                };

                return (true, null, info);
            }
            catch (Exception ex)
            {
                return (false, $"Proje oluşturma hatası: {ex.Message}", null);
            }
        }
    }
}
