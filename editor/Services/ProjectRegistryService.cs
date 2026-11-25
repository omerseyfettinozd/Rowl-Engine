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
        private readonly string? _sampleProjectsRoot;
        private List<ProjectInfo> _projects = new();
        private HashSet<string> _removedPaths = new(StringComparer.OrdinalIgnoreCase);

        public IReadOnlyList<ProjectInfo> Projects => _projects;

        public ProjectRegistryService() : this(GetRegistryPath(), DefaultSampleProjectsRoot()) { }

        /// <summary>
        /// Faz 8 C1: kayıt yolu verilen örnek (testler) kullanıcının ev dizinindeki örnek
        /// projeleri kendiliğinden içe aktarmaz; aksi hâlde sonuç makineye bağlı olurdu.
        /// </summary>
        public ProjectRegistryService(string registryPath, string? sampleProjectsRoot = null)
        {
            _registryPath = registryPath;
            _sampleProjectsRoot = sampleProjectsRoot;
            Load();
        }

        private static string DefaultSampleProjectsRoot() =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Belgeler", "Rowl Engine Project");

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

        private string GetRemovedPathsFilePath()
        {
            string? dir = Path.GetDirectoryName(_registryPath);
            return string.IsNullOrEmpty(dir) ? "projects_removed.json" : Path.Combine(dir, "projects_removed.json");
        }

        private void LoadRemovedPaths()
        {
            try
            {
                string path = GetRemovedPathsFilePath();
                if (File.Exists(path))
                {
                    string json = File.ReadAllText(path);
                    var items = JsonSerializer.Deserialize<List<string>>(json);
                    if (items != null)
                    {
                        _removedPaths = new HashSet<string>(items, StringComparer.OrdinalIgnoreCase);
                    }
                }
            }
            catch { }
        }

        private void SaveRemovedPaths()
        {
            try
            {
                string path = GetRemovedPathsFilePath();
                string? dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                {
                    Directory.CreateDirectory(dir);
                }
                string json = JsonSerializer.Serialize(_removedPaths.ToList(), new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(path, json);
            }
            catch { }
        }

        public void Load()
        {
            LoadRemovedPaths();
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

                // Filter out non-existent directories & deduplicate by ID and Path
                var valid = new List<ProjectInfo>();
                foreach (var p in _projects.Where(p => Directory.Exists(p.Path)).OrderByDescending(p => p.LastOpenedAt))
                {
                    string full = Path.GetFullPath(p.Path);
                    if (!valid.Any(v => v.Id == p.Id || Path.GetFullPath(v.Path).Equals(full, StringComparison.OrdinalIgnoreCase)))
                    {
                        valid.Add(p);
                    }
                }
                _projects = valid;

                TryAutoImportLegacyProject();
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
            if (!string.IsNullOrWhiteSpace(info.Path))
            {
                _removedPaths.Remove(info.Path);
                try { _removedPaths.Remove(Path.GetFullPath(info.Path)); } catch { }
                SaveRemovedPaths();
            }

            _projects.RemoveAll(p => p.Id == info.Id || p.Path.Equals(info.Path, StringComparison.OrdinalIgnoreCase));
            _projects.Insert(0, info);
            Save();
        }

        public bool Remove(string id)
        {
            var target = _projects.FirstOrDefault(p => p.Id == id);
            if (target != null && !string.IsNullOrWhiteSpace(target.Path))
            {
                _removedPaths.Add(target.Path);
                try { _removedPaths.Add(Path.GetFullPath(target.Path)); } catch { }
                SaveRemovedPaths();
            }

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

        private static string ResolveProjectName(string directory)
        {
            try
            {
                string projFile = Path.Combine(directory, "project.rowlproj");
                if (File.Exists(projFile))
                {
                    using var doc = JsonDocument.Parse(File.ReadAllText(projFile));
                    if (doc.RootElement.TryGetProperty("name", out var elem))
                    {
                        string? n = elem.GetString();
                        if (!string.IsNullOrWhiteSpace(n))
                            return n;
                    }
                }
            }
            catch { }
            return Path.GetFileName(directory);
        }

        private static string ResolveProjectId(string directory)
        {
            try
            {
                string projFile = Path.Combine(directory, "project.rowlproj");
                if (File.Exists(projFile))
                {
                    using var doc = JsonDocument.Parse(File.ReadAllText(projFile));
                    if (doc.RootElement.TryGetProperty("project_uuid", out var elem))
                    {
                        string? u = elem.GetString();
                        if (!string.IsNullOrWhiteSpace(u) && Guid.TryParse(u, out var g))
                            return g.ToString("N");
                    }
                }
            }
            catch { }
            return Guid.NewGuid().ToString("N");
        }

        private static string? ResolveProjectCover(string directory)
        {
            try
            {
                var direct = Directory.GetFiles(directory, "cover*.*", SearchOption.TopDirectoryOnly)
                    .FirstOrDefault(f => f.EndsWith(".png", StringComparison.OrdinalIgnoreCase) ||
                                         f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase) ||
                                         f.EndsWith(".jpeg", StringComparison.OrdinalIgnoreCase));
                if (direct != null)
                    return Path.GetFileName(direct);

                string imagesDir = Path.Combine(directory, "Assets", "images");
                if (Directory.Exists(imagesDir))
                {
                    string[] preferred = { "aksam_isigi.jpg", "okuyan_kiz.jpg", "gun_batimi.jpg", "atolye.jpg" };
                    foreach (var pref in preferred)
                    {
                        string candidate = Path.Combine(imagesDir, pref);
                        if (File.Exists(candidate))
                            return Path.Combine("Assets", "images", pref);
                    }
                }
            }
            catch { }
            return null;
        }

        private void TryAutoImportLegacyProject()
        {
            if (string.IsNullOrWhiteSpace(_sampleProjectsRoot) || !Directory.Exists(_sampleProjectsRoot))
                return;

            bool changed = false;
            try
            {
                var found = new List<string>();
                foreach (var sub in Directory.GetDirectories(_sampleProjectsRoot, "*", SearchOption.AllDirectories)
                             .OrderBy(d => d.Length))
                {
                    if (found.Any(r => sub.StartsWith(r + Path.DirectorySeparatorChar, StringComparison.Ordinal)))
                        continue;

                    if (File.Exists(Path.Combine(sub, "project.rowlproj")) ||
                        File.Exists(Path.Combine(sub, "full_story_graph.json")) ||
                        File.Exists(Path.Combine(sub, "Assets", "full_story_graph.json")))
                    {
                        found.Add(sub);

                        string fullSub = Path.GetFullPath(sub);
                        string id = ResolveProjectId(sub);

                        bool alreadyInProjects = _projects.Any(p =>
                        {
                            if (p.Id == id) return true;
                            try { return Path.GetFullPath(p.Path).Equals(fullSub, StringComparison.OrdinalIgnoreCase); }
                            catch { return p.Path.Equals(sub, StringComparison.OrdinalIgnoreCase); }
                        });

                        if (alreadyInProjects)
                            continue;

                        bool wasRemoved = _removedPaths.Any(r =>
                        {
                            try { return Path.GetFullPath(r).Equals(fullSub, StringComparison.OrdinalIgnoreCase); }
                            catch { return r.Equals(sub, StringComparison.OrdinalIgnoreCase); }
                        });

                        if (wasRemoved)
                            continue;

                        string name = ResolveProjectName(sub);
                        string? cover = ResolveProjectCover(sub);

                        _projects.Add(new ProjectInfo
                        {
                            Id = id,
                            Name = name,
                            Path = sub,
                            CreatedAt = DateTime.UtcNow,
                            LastOpenedAt = DateTime.UtcNow,
                            CoverPath = cover
                        });
                        changed = true;
                    }
                }
            }
            catch { }

            if (changed)
            {
                Save();
            }
        }
    }
}
