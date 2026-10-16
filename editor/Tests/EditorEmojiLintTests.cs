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
        // Sabit tema-önizleme swatch'ları (SettingsDialog: her temanın gerçek
        // paletini gösterir; token olamaz, bilerek hardcoded).
        // Diğer girdiler OYUN-İÇERİĞİ verisidir (proje dosyasına kaydedilen
        // varsayılanlar/presetler/renk-etiketleri, editör kromu değil):
        // Dialogue/Choice/Transition component presetleri, grup rengi
        // varsayılanı, Transition örnek giriş metinleri, node renk-etiket
        // paleti. SettingsViewModel girdisi TEMA TANIMININ KENDİSİDİR
        // (palet kaynağı; bypass değil). ThemeFallbackColors ise tek
        // güvenlik-ağı noktasıdır.
        private static readonly Dictionary<string, string[]> AllowedHex = new()
        {
            ["SettingsDialog.axaml"] = new[] { "#111113", "#19191C", "#85858B", "#F2EFE6" },
            ["ThemeFallbackColors.cs"] = new[] { "#131315", "#29292D", "#66666D", "#85858B", "#9DB58F", "#A9A3C9", "#D9B47A", "#E3A296", "#F2EFE6" },
            ["DialogueComponentViewModel.cs"] = new[] { "#F1F5F9", "#38BDF8", "#0F0F1A", "#00F0FF", "#0A0A12", "#334155", "#000000", "#F59E0B", "#1E1E2E", "#EC4899" },
            ["ChoiceComponentViewModel.cs"] = new[] { "#111827", "#374151", "#6B7280", "#F5E6C8", "#E8C98E", "#241A12", "#7C5C3B", "#450A0A", "#B91C1C", "#FEE2E2", "#EF4444", "#1E293B", "#0EA5E9", "#FFFFFF", "#38BDF8", "#475569" },
            ["CanvasGroupViewModel.cs"] = new[] { "#3B82F6" },
            ["TransitionComponentView.axaml"] = new[] { "#000000", "#FFFFFF", "#0A183D" },
            ["TransitionComponentViewModel.cs"] = new[] { "#000000", "#FFFFFF", "#0A183D" },
            ["NodeColorTags.cs"] = new[] { "#EF4444", "#F59E0B", "#EAB308", "#22C55E", "#3B82F6", "#A855F7", "#EC4899", "#6B7280", "#64748B", "#38BDF8" },
            ["SettingsViewModel.cs"] = new[] { "#000000", "#070709", "#0A0A0B", "#0B0B0D", "#0F0F12", "#111113", "#111114", "#131315", "#18181B", "#19191C", "#19191D", "#1A1A1E", "#1E1E22", "#202023", "#222226", "#26262B", "#29292D", "#2B2A36", "#2C332C", "#3A3124", "#3E2B26", "#4A4A52", "#66666D", "#74736C", "#847F73", "#85858B", "#9DB58F", "#9E988A", "#A9A3C9", "#B9B3A3", "#D4CBB6", "#D9B47A", "#DAD7CF", "#E2DBC9", "#E3A296", "#EDE8DC", "#F2EFE6", "#FFFFFF",
                /* Faz 6 görsel uyum token'ları */ "#999991", "#A5A49E", "#2E2E32", "#171719", "#ADABA4", "#ACA89D",
                /* Faz 7 Lua renklendirmesi */ "#727E78", "#BFA8DA", "#C5CDA9", "#C9B089",
                /* Faz 8 A1 tuval nokta ızgarası */ "#303034" },
        };
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
            string[] lines = File.ReadAllLines(file, Encoding.UTF8);
            string name = Path.GetFileName(file);
            for (int li = 0; li < lines.Length; li++)
            {
                string line = lines[li];
                string trimmed = line.TrimStart();
                bool isComment = trimmed.StartsWith("//", StringComparison.Ordinal) ||
                                 trimmed.StartsWith("<!--", StringComparison.Ordinal) ||
                                 trimmed.StartsWith("*", StringComparison.Ordinal);
                for (int i = 0; i < line.Length; i++)
                {
                    if (char.IsSurrogate(line[i]))
                    {
                        offenders.Add($"{file}:{li + 1}: vekil-çift (emoji)");
                        break;
                    }
                }
                foreach (string banned in BannedSymbols)
                {
                    if (line.Contains(banned, StringComparison.Ordinal))
                        offenders.Add($"{file}:{li + 1}: yasaklı sembol '{banned}'");
                }
                if (isComment)
                    continue; // yorum-içi tarihsel hex serbest
                foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(
                    line, "#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?\\b"))
                {
                    if (m.Value.Length == 9)
                        continue; // BoxShadow alpha (#AARRGGBB) izni
                    if (IsInsideBoxShadow(line, m.Index))
                        continue; // yükselti gölgesi her zaman nötr siyahtır
                    if (AllowedHex.TryGetValue(name, out var allowed) &&
                        Array.IndexOf(allowed, m.Value) >= 0)
                        continue; // swatch/içerik-verisi/güvenlik-ağı izni
                    offenders.Add($"{file}:{li + 1}: hardcoded hex '{m.Value}'");
                }
            }
        }
        private static bool IsInsideBoxShadow(string line, int index)
        {
            const string marker = "BoxShadow=\"";
            int start = line.LastIndexOf(marker, index, System.StringComparison.Ordinal);
            if (start < 0)
                return false;
            int end = line.IndexOf('"', start + marker.Length);
            return end < 0 || index < end;
        }
    }
}
