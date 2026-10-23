#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace RowlEngine.Editor.Services.Scripting;

/// <summary>
/// Lua düzenleme panelinin arkasındaki düzenleme modeli.
/// Dosyalar projenin Assets klasöründe gerçek .lua dosyaları olarak tutulur.
/// </summary>
public sealed class LuaScriptWorkspace
{
    /// <summary>Tek dosya için üst sınır (1 MiB). Büyük dosyalar listede atlanır.</summary>
    public const int MaxFileBytes = 1024 * 1024;

    /// <summary>View Tab için girinti (prototip: Tab = 2 boşluk).</summary>
    public static string IndentOnTab => "  ";

    private static readonly Regex FileNameRule = new(@"^[\w-]+\.lua$", RegexOptions.CultureInvariant);

    private readonly List<string> _files = new();
    private readonly List<string> _skipped = new();
    private readonly List<ScriptBuffer> _open = new();

    /// <summary>Assets kökünün tam yolu.</summary>
    public string AssetsRoot { get; }

    /// <summary>Diskteki .lua dosyaları (gizli dizinler ve büyük dosyalar hariç) + kaybolmuş açık tamponlar.</summary>
    public IReadOnlyList<string> Files => _files.AsReadOnly();

    /// <summary>Boyut sınırını aşan dosyalar.</summary>
    public IReadOnlyList<string> SkippedFiles => _skipped.AsReadOnly();

    /// <summary>Açılış sırasıyla açık tamponlar.</summary>
    public IReadOnlyList<ScriptBuffer> OpenBuffers => _open.AsReadOnly();

    /// <summary>Etkin tamponun yolu (yoksa null).</summary>
    public string? ActivePath { get; private set; }

    public LuaScriptWorkspace(string assetsRoot)
    {
        if (string.IsNullOrWhiteSpace(assetsRoot))
            throw new ArgumentException("Assets kökü boş olamaz.", nameof(assetsRoot));
        AssetsRoot = Path.GetFullPath(assetsRoot);
        Refresh();
    }

    /// <summary>Tek dosyalık düzenleme tamponu.</summary>
    public sealed class ScriptBuffer
    {
        public string RelativePath { get; }

        private string _text;
        public string Text
        {
            get => _text;
            set
            {
                if (value is null)
                    throw new ArgumentNullException(nameof(value));
                if (string.Equals(_text, value, StringComparison.Ordinal))
                    return;
                _text = value;
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }

        public string SavedText { get; private set; }

        public bool IsDirty => !string.Equals(_text, SavedText, StringComparison.Ordinal);

        public bool IsMissingOnDisk { get; private set; }

        public event EventHandler? Changed;

        internal ScriptBuffer(string relativePath, string text, string savedText, bool missingOnDisk)
        {
            RelativePath = relativePath;
            _text = text;
            SavedText = savedText;
            IsMissingOnDisk = missingOnDisk;
        }

        internal void MarkSaved()
        {
            SavedText = _text;
            IsMissingOnDisk = false;
        }

        internal void SetMissing(bool missing)
        {
            IsMissingOnDisk = missing;
        }

        internal void ApplyReload(string diskContent)
        {
            SavedText = diskContent;
            IsMissingOnDisk = false;
            if (!string.Equals(_text, diskContent, StringComparison.Ordinal))
            {
                _text = diskContent;
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    /// <summary>Dizini yeniden tarar. Açık tamponlar korunur.</summary>
    public void Refresh()
    {
        var diskFiles = new List<string>();
        var skipped = new List<string>();

        if (Directory.Exists(AssetsRoot))
        {
            foreach (string file in Directory.EnumerateFiles(AssetsRoot, "*.lua", SearchOption.AllDirectories))
            {
                string rel;
                try
                {
                    rel = Path.GetRelativePath(AssetsRoot, file).Replace('\\', '/');
                }
                catch
                {
                    continue;
                }

                string[] segs = rel.Split('/');
                bool hidden = false;
                for (int i = 0; i < segs.Length - 1; i++)
                {
                    if (segs[i].Length > 0 && segs[i][0] == '.')
                    {
                        hidden = true;
                        break;
                    }
                }
                if (hidden)
                    continue;

                long length;
                try
                {
                    length = new FileInfo(file).Length;
                }
                catch
                {
                    continue;
                }

                if (length > MaxFileBytes)
                    skipped.Add(rel);
                else
                    diskFiles.Add(rel);
            }
        }

        SortOrdinalIgnoreCase(diskFiles);
        SortOrdinalIgnoreCase(skipped);

        _files.Clear();
        _files.AddRange(diskFiles);
        _skipped.Clear();
        _skipped.AddRange(skipped);

        bool addedMissing = false;
        foreach (ScriptBuffer buf in _open)
        {
            bool exists;
            try
            {
                exists = File.Exists(GetFullPath(buf.RelativePath));
            }
            catch
            {
                exists = false;
            }

            buf.SetMissing(!exists);
            if (!exists)
            {
                bool found = false;
                foreach (string f in _files)
                {
                    if (string.Equals(f, buf.RelativePath, StringComparison.OrdinalIgnoreCase))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    _files.Add(buf.RelativePath);
                    addedMissing = true;
                }
            }
        }

        if (addedMissing)
            SortOrdinalIgnoreCase(_files);
    }

    /// <summary>Dosyayı açar veya zaten açıksa döndürür.</summary>
    public ScriptBuffer Open(string relPath)
    {
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer? existing = FindOpen(norm);
        if (existing is not null)
            return existing;

        string full = GetFullPath(norm);
        if (!File.Exists(full))
            throw new FileNotFoundException($"Lua dosyası bulunamadı: {norm}", full);

        string content = ReadAndNormalize(full);
        var buf = new ScriptBuffer(norm, content, content, false);
        _open.Add(buf);
        return buf;
    }

    /// <summary>Tamponu etkin yapar (gerekirse açar).</summary>
    public void Activate(string relPath)
    {
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer buf = FindOpen(norm) ?? Open(norm);
        ActivePath = buf.RelativePath;
    }

    /// <summary>Tamponu diske atomik yazar.</summary>
    public void Save(string relPath)
    {
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer buf = FindOpen(norm)
            ?? throw new InvalidOperationException($"Buffer açık değil: {norm}");
        string full = GetFullPath(norm);
        ProjectFileSystem.WriteAllTextAtomically(full, buf.Text);
        buf.MarkSaved();
        Refresh();
    }

    /// <summary>Kirli tamponların hepsini kaydeder, sayıyı döndürür.</summary>
    public int SaveAll()
    {
        int count = 0;
        foreach (ScriptBuffer buf in _open.ToList())
        {
            if (!buf.IsDirty)
                continue;
            string full = GetFullPath(buf.RelativePath);
            ProjectFileSystem.WriteAllTextAtomically(full, buf.Text);
            buf.MarkSaved();
            count++;
        }

        if (count > 0)
            Refresh();
        return count;
    }

    /// <summary>İlk boş scripts/script-N.lua dosyasını oluşturur, açar ve etkin yapar.</summary>
    public string CreateNew(string initialContent)
    {
        if (initialContent is null)
            throw new ArgumentNullException(nameof(initialContent));

        string normalized = initialContent.Replace("\r\n", "\n");
        if (normalized.Length > 0 && normalized[0] == '\uFEFF')
            normalized = normalized.Substring(1);

        for (int n = 1; ; n++)
        {
            string candidate = $"scripts/script-{n}.lua";
            string full = GetFullPath(candidate);
            bool exists;
            try
            {
                exists = File.Exists(full);
            }
            catch
            {
                continue;
            }
            if (exists)
                continue;
            if (FindOpen(candidate) is not null)
                continue;

            ProjectFileSystem.WriteAllTextAtomically(full, normalized);
            Refresh();
            Open(candidate);
            Activate(candidate);
            return candidate;
        }
    }

    /// <summary>Tamponu kapatır. Kirli ve vazgeçilmemişse false döndürür.</summary>
    public bool Close(string relPath, bool discardChanges)
    {
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer? buf = FindOpen(norm);
        if (buf is null)
            return false;
        if (buf.IsDirty && !discardChanges)
            return false;

        _open.Remove(buf);
        if (string.Equals(ActivePath, norm, StringComparison.OrdinalIgnoreCase))
            ActivePath = _open.Count > 0 ? _open[_open.Count - 1].RelativePath : null;

        Refresh();
        return true;
    }

    /// <summary>Tampon metninde geçen sorgu sayısı (çakışmasız, ordinal).</summary>
    public int CountMatches(string relPath, string query)
    {
        if (string.IsNullOrEmpty(query))
            return 0;
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer buf = FindOpen(norm)
            ?? throw new InvalidOperationException($"Buffer açık değil: {norm}");
        string text = buf.Text;
        int count = 0;
        int start = 0;
        while (start <= text.Length - query.Length)
        {
            int idx = text.IndexOf(query, start, StringComparison.Ordinal);
            if (idx < 0)
                break;
            count++;
            start = idx + query.Length;
        }
        return count;
    }

    /// <summary>Disk içeriği SavedText'ten farklıysa true (başka araç değiştirmiş).</summary>
    public bool HasExternalChange(string relPath)
    {
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer buf = FindOpen(norm)
            ?? throw new InvalidOperationException($"Buffer açık değil: {norm}");
        string full = GetFullPath(norm);
        if (!File.Exists(full))
            return true;
        string disk = ReadAndNormalize(full);
        return !string.Equals(disk, buf.SavedText, StringComparison.Ordinal);
    }

    /// <summary>Diskten yeniden yükler (kirliyken geçersiz).</summary>
    public void Reload(string relPath)
    {
        string norm = NormalizeAndValidate(relPath);
        ScriptBuffer buf = FindOpen(norm)
            ?? throw new InvalidOperationException($"Buffer açık değil: {norm}");
        if (buf.IsDirty)
            throw new InvalidOperationException($"Kaydedilmemiş değişiklik var: {norm}");
        string full = GetFullPath(norm);
        if (!File.Exists(full))
            throw new FileNotFoundException($"Lua dosyası bulunamadı: {norm}", full);
        string disk = ReadAndNormalize(full);
        buf.ApplyReload(disk);
        Refresh();
    }

    /// <summary>1-tabanlı satır/sütun (prototip "Satır N · Sütun M").</summary>
    public static (int Line, int Column) CaretPosition(string text, int caretIndex)
    {
        if (text is null)
            throw new ArgumentNullException(nameof(text));
        if (caretIndex < 0)
            caretIndex = 0;
        if (caretIndex > text.Length)
            caretIndex = text.Length;

        int line = 1;
        int lastNewline = -1;
        for (int i = 0; i < caretIndex; i++)
        {
            if (text[i] == '\n')
            {
                line++;
                lastNewline = i;
            }
        }

        return (line, caretIndex - lastNewline);
    }

    private ScriptBuffer? FindOpen(string normalized)
    {
        foreach (ScriptBuffer buf in _open)
        {
            if (string.Equals(buf.RelativePath, normalized, StringComparison.OrdinalIgnoreCase))
                return buf;
        }
        return null;
    }

    private string GetFullPath(string normalizedRel)
    {
        return Path.GetFullPath(Path.Combine(AssetsRoot, normalizedRel.Replace('/', Path.DirectorySeparatorChar)));
    }

    private static string ReadAndNormalize(string fullPath)
    {
        string content = File.ReadAllText(fullPath, Encoding.UTF8);
        if (content.Length > 0 && content[0] == '\uFEFF')
            content = content.Substring(1);
        if (content.Contains("\r\n", StringComparison.Ordinal))
            content = content.Replace("\r\n", "\n", StringComparison.Ordinal);
        return content;
    }

    private string NormalizeAndValidate(string relPath)
    {
        if (relPath is null)
            throw new ArgumentNullException(nameof(relPath));

        string p = relPath.Replace('\\', '/').Trim();
        if (p.Length == 0)
            throw new ArgumentException("Script yolu boş olamaz.", nameof(relPath));
        if (p.StartsWith("/", StringComparison.Ordinal))
            throw new ArgumentException($"Kök yol kullanılamaz: {relPath}", nameof(relPath));
        if (p.Length >= 2 && char.IsLetter(p[0]) && p[1] == ':')
            throw new ArgumentException($"Kök yol kullanılamaz: {relPath}", nameof(relPath));
        if (Path.IsPathRooted(p))
            throw new ArgumentException($"Kök yol kullanılamaz: {relPath}", nameof(relPath));

        string[] parts = p.Split('/');
        foreach (string part in parts)
        {
            if (part.Length == 0)
                throw new ArgumentException($"Geçersiz script yolu: {relPath}", nameof(relPath));
            if (part == "..")
                throw new ArgumentException($"Üst dizine çıkılamaz: {relPath}", nameof(relPath));
            if (part == ".")
                throw new ArgumentException($"Geçersiz script yolu: {relPath}", nameof(relPath));
        }

        string fileName = parts[parts.Length - 1];
        if (!fileName.EndsWith(".lua", StringComparison.Ordinal))
            throw new ArgumentException($"Yalnızca .lua dosyaları: {relPath}", nameof(relPath));
        if (!FileNameRule.IsMatch(fileName))
            throw new ArgumentException($"Geçersiz dosya adı: {relPath}", nameof(relPath));

        string full = GetFullPath(p);
        if (!ProjectFileSystem.IsSameOrDescendant(full, AssetsRoot))
            throw new ArgumentException($"Assets dışına çıkılamaz: {relPath}", nameof(relPath));

        return p;
    }

    private static void SortOrdinalIgnoreCase(List<string> list)
    {
        list.Sort((a, b) =>
        {
            int c = string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
            return c != 0 ? c : string.Compare(a, b, StringComparison.Ordinal);
        });
    }
}
