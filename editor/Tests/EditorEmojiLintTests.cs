using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests
{
    /// <summary>
    /// Faz 1 kilidi: editör ürün kodunda emoji/sembol kalmadığını doğrular.
    /// Kural: metin-etiket öncelikli; durum anlamı renklerle taşınır.
    /// </summary>
    public sealed class EditorEmojiLintTests
    {
        // Kaldırılan BMP sembolleri (yorum-içi ok "→" ve noktalama hariç).
        private static readonly string[] BannedSymbols = new[]
        {
            "⚠", "⛔", "↩", "↪", "↻", "↺", "▲", "▼", "▶", "⏹", "⏸",
            "●", "⋯", "＋", "⤴", "›", "✦", "✕", "◧", "◀"
        };

        [Fact]
        public void ProductCode_ContainsNoEmojiOrBannedSymbols()
        {
            string repoRoot = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
            string editorDir = Path.Combine(repoRoot, "editor");
            var offenders = new List<string>();

            foreach (string sub in new[] { "Views", "ViewModels", "Controls", "Services", "Models" })
            {
                string dir = Path.Combine(editorDir, sub);
                if (!Directory.Exists(dir))
                    continue;
                foreach (string file in Directory.GetFiles(dir, "*.axaml", SearchOption.AllDirectories))
                    Scan(file, offenders);
                foreach (string file in Directory.GetFiles(dir, "*.cs", SearchOption.AllDirectories))
                    Scan(file, offenders);
            }

            Assert.True(offenders.Count == 0,
                "Emoji/sembol bulundu:\n" + string.Join("\n", offenders));
        }

        private static void Scan(string file, List<string> offenders)
        {
            string text = File.ReadAllText(file, Encoding.UTF8);
            for (int i = 0; i < text.Length; i++)
            {
                if (char.IsSurrogate(text[i]))
                {
                    offenders.Add($"{file}: vekil-çift (emoji) konumu {i}");
                    return;
                }
            }
            foreach (string banned in BannedSymbols)
            {
                if (text.Contains(banned, StringComparison.Ordinal))
                    offenders.Add($"{file}: yasaklı sembol '{banned}'");
            }
        }
    }
}
