using System;
using System.IO;

namespace RowlEngine.Editor.Models
{
    public class ProjectInfo
    {
        public string Id { get; set; } = Guid.NewGuid().ToString("N");
        public string Name { get; set; } = "";
        public string Path { get; set; } = "";
        public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
        public DateTime LastOpenedAt { get; set; } = DateTime.UtcNow;
        public string? CoverPath { get; set; }

        public bool HasCover => !string.IsNullOrWhiteSpace(CoverPath) && File.Exists(CoverAbsolutePath);

        public string CoverAbsolutePath
        {
            get
            {
                if (string.IsNullOrWhiteSpace(CoverPath)) return "";
                if (System.IO.Path.IsPathRooted(CoverPath)) return CoverPath;
                return System.IO.Path.Combine(Path, CoverPath);
            }
        }
    }
}
