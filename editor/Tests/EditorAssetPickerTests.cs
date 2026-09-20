using System;
using System.IO;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor
{
    /// <summary>
    /// Faz 5 Dilim 6 kilidi: AssetPickerControl kararı — TÜM
    /// browse-butonları bu kontrol olur. El-yapımı
    /// TextBox + "Browse..." ikilisi bileşen görünümlerine dönemez;
    /// sürükle-bırak bölgesi + önizleme yerinde kalır (kontrol yalnızca
    /// yol düzenleyicisidir, bölgesi değil).
    /// </summary>
    internal static class EditorAssetPickerTests
    {
        public static void Run()
        {
            Console.WriteLine("\n[Test 40]: AssetPicker adoption lock (no hand-rolled Browse pairs)...");
            string repoRoot = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
            string compDir = Path.Combine(repoRoot, "editor", "Views", "Components");
            if (!Directory.Exists(compDir))
                throw new Exception($"Components dir not found: {compDir}");

            foreach (string file in Directory.GetFiles(compDir, "*.axaml"))
            {
                string text = File.ReadAllText(file);
                string name = Path.GetFileName(file);
                if (text.Contains("Browse...", StringComparison.Ordinal))
                    throw new Exception($"{name}: hand-rolled Browse button is back — use AssetPickerControl.");
                if (text.Contains("Drag &amp; Drop", StringComparison.Ordinal)
                    || text.Contains("Drag Image Here", StringComparison.Ordinal)
                    || text.Contains("Drag Sprite Here", StringComparison.Ordinal)
                    || text.Contains("Browse'a", StringComparison.Ordinal))
                    throw new Exception($"{name}: English drag/browse hint is back.");
            }

            // Dönüşen iki görünüm kontrolü doğru bağla kullanmalı.
            AssertPicker(compDir, "BackgroundComponentView.axaml", "Texture");
            AssertPicker(compDir, "CharacterComponentView.axaml", "Sprite");
            AssertPicker(compDir, "DialogueComponentView.axaml", "TypewriterSound");
            Console.WriteLine("  [PASS] All browse buttons go through AssetPickerControl");
        }

        private static void AssertPicker(string dir, string file, string pathProperty)
        {
            string text = File.ReadAllText(Path.Combine(dir, file));
            if (!text.Contains("AssetPickerControl", StringComparison.Ordinal)
                || !text.Contains($"AssetPath=\"{{Binding {pathProperty}",
                    StringComparison.Ordinal))
                throw new Exception($"{file}: AssetPickerControl binding for {pathProperty} lost.");
        }
    }
}
