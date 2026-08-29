using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Models;

namespace RowlEngine.Editor.Services
{
    public class ProjectRegistryService
    {
        private readonly string _registryPath;
        private List<ProjectInfo> _projects = new();

        public IReadOnlyList<ProjectInfo> Projects => _projects;

        public ProjectRegistryService() : this(GetRegistryPath()) { }

        public ProjectRegistryService(string registryPath)
        {
            _registryPath = registryPath;
            Load();
        }

        public static string GetRegistryPath()
        {
            string baseDir;
            if (OperatingSystem.IsWindows())
            {
                baseDir = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            }
            else if (OperatingSystem.IsMacOS())
            {
                string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                baseDir = Path.Combine(home, "Library", "Application Support");
            }
            else // Linux & other
            {
                string? xdg = Environment.GetEnvironmentVariable("XDG_CONFIG_HOME");
                if (!string.IsNullOrWhiteSpace(xdg))
                    baseDir = xdg;
                else
                    baseDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config");
            }

            string dir = Path.Combine(baseDir, "RowlEngine");
            if (!Directory.Exists(dir))
            {
                try { Directory.CreateDirectory(dir); } catch { }
            }
            return Path.Combine(dir, "projects.json");
        }

        public void Load()
        {
            try
            {
                if (!File.Exists(_registryPath))
                {
                    _projects = new List<ProjectInfo>();
                    TryAutoImportLegacyProject();
                    return;
                }

                string json = File.ReadAllText(_registryPath);
                var items = JsonSerializer.Deserialize<List<ProjectInfo>>(json);
                _projects = items ?? new List<ProjectInfo>();

                // Filter out non-existent directories
                _projects = _projects.Where(p => Directory.Exists(p.Path)).OrderByDescending(p => p.LastOpenedAt).ToList();

                if (_projects.Count == 0)
                {
                    TryAutoImportLegacyProject();
                }
            }
            catch
            {
                _projects = new List<ProjectInfo>();
                TryAutoImportLegacyProject();
            }
        }

        public void Save()
        {
            try
            {
                string? dir = Path.GetDirectoryName(_registryPath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }

                var options = new JsonSerializerOptions { WriteIndented = true };
                string json = JsonSerializer.Serialize(_projects, options);
                File.WriteAllText(_registryPath, json);
            }
            catch { }
        }

        public void Add(ProjectInfo info)
        {
            _projects.RemoveAll(p => p.Id == info.Id || p.Path.Equals(info.Path, StringComparison.OrdinalIgnoreCase));
            _projects.Insert(0, info);
            Save();
        }

        public bool Remove(string id)
        {
            int count = _projects.RemoveAll(p => p.Id == id);
            if (count > 0)
            {
                Save();
                return true;
            }
            return false;
        }

        public void Touch(string id)
        {
            var p = _projects.FirstOrDefault(x => x.Id == id);
            if (p != null)
            {
                p.LastOpenedAt = DateTime.UtcNow;
                _projects = _projects.OrderByDescending(x => x.LastOpenedAt).ToList();
                Save();
            }
        }

        public bool Rename(string id, string newName)
        {
            var p = _projects.FirstOrDefault(x => x.Id == id);
            if (p == null) return false;

            if (_projects.Any(x => x.Id != id && x.Name.Equals(newName, StringComparison.OrdinalIgnoreCase)))
                return false;

            p.Name = newName;
            Save();
            return true;
        }

        public void UpdateCover(string id, string coverPath)
        {
            var p = _projects.FirstOrDefault(x => x.Id == id);
            if (p != null)
            {
                p.CoverPath = coverPath;
                Save();
            }
        }

        private void TryAutoImportLegacyProject()
        {
            // Auto import sample projects if found in Documents or Projects directory
            string userDocs = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Belgeler", "Rowl Engine Project");
            if (Directory.Exists(userDocs))
            {
                try
                {
                    foreach (var sub in Directory.GetDirectories(userDocs, "*", SearchOption.AllDirectories))
                    {
                        if (File.Exists(Path.Combine(sub, "project.rowlproj")) ||
                            File.Exists(Path.Combine(sub, "full_story_graph.json")) ||
                            File.Exists(Path.Combine(sub, "Assets", "full_story_graph.json")))
                        {
                            string name = Path.GetFileName(sub);
                            if (!_projects.Any(p => p.Path == sub))
                            {
                                _projects.Add(new ProjectInfo
                                {
                                    Id = Guid.NewGuid().ToString("N"),
                                    Name = name,
                                    Path = sub,
                                    CreatedAt = DateTime.UtcNow,
                                    LastOpenedAt = DateTime.UtcNow
                                });
                            }
                        }
                    }
                    if (_projects.Count > 0)
                    {
                        Save();
                    }
                }
                catch { }
            }
        }
    }
}