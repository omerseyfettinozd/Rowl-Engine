using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor
{
    /// <summary>
    /// Faz 4 kilidi: Assets yeniden tasarımı (arama + tür filtresi + durum
    /// satırı, silme onayı, klasör-adı sorma, kayıp-grup bilgi satırı, tip
    /// etiketi). Headless-güvenli: pencere açılmaz, dialog'lar enjeksiyonla
    /// mock'lanır; async komutlar senkron bloklanır (test ipliğinde
    /// SynchronizationContext yok).
    /// </summary>
    internal static class EditorAssetsRedesignTests
    {
        public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
        {
            Console.WriteLine("\n[Test 36]: Assets redesign lock (search, filter, confirm, prompt, badges)...");
            var browser = mainVm.AssetBrowserViewModel;
            string assetsDir = Path.Combine(testProjectRoot, "Assets");

            // GetTypeLabel: filtre kategorileriyle aynı küme.
            if (AssetNodeViewModel.GetTypeLabel(true, "x") != "Klasör"
                || AssetNodeViewModel.GetTypeLabel(false, "a.png") != "Görsel"
                || AssetNodeViewModel.GetTypeLabel(false, "b.ogg") != "Ses"
                || AssetNodeViewModel.GetTypeLabel(false, "c.lua") != "Betik"
                || AssetNodeViewModel.GetTypeLabel(false, "d.rowlpkg") != "Paket"
                || AssetNodeViewModel.GetTypeLabel(false, "e.bin") != "Dosya")
                throw new Exception("TypeLabel mapping diverged from filter categories.");

            string alpha = Path.Combine(assetsDir, "f4_alpha.txt");
            string beta = Path.Combine(assetsDir, "f4_beta.txt");
            string folder = Path.Combine(assetsDir, "F4Klasor");
            string doomed = Path.Combine(assetsDir, "f4_sil.txt");
            File.WriteAllText(alpha, "a");
            File.WriteAllText(beta, "b");
            File.WriteAllText(doomed, "x");
            try
            {
                browser.SearchText = string.Empty;
                browser.SelectedTypeFilter = "Tümü";
                browser.RefreshAssets();
                if (!FindNode(browser, "f4_alpha.txt")!.TypeLabel.Equals("Betik"))
                    throw new Exception("Probe node did not carry its type label.");
                if (!browser.StatusText.Contains("öğe"))
                    throw new Exception($"Status line missing item count: '{browser.StatusText}'.");

                // Arama: daraltır + sonuç sayar; temizlenince geri gelir.
                // İki-fazlı tarama kilidi: filtre-dışı dosyalar kayıp
                // hayaletine dönüşmemeli (yalancı rozet regresyonu).
                browser.SearchText = "f4_alpha";
                if (FindNode(browser, "f4_beta.txt") != null || FindNode(browser, "f4_alpha.txt") == null)
                    throw new Exception("SearchText did not narrow the tree.");
                if (browser.MissingAssetPaths.Any(p => p.EndsWith("f4_beta.txt") || p.EndsWith("f4_sil.txt")))
                    throw new Exception("Filtered-out files were badged as missing.");
                if (browser.StatusText != "1 sonuç")
                    throw new Exception($"Search status expected '1 sonuç', got '{browser.StatusText}'.");
                browser.SearchText = string.Empty;
                if (FindNode(browser, "f4_beta.txt") == null)
                    throw new Exception("Clearing SearchText did not restore the tree.");

                // Tür filtresi: .txt Ses altında gizlenir.
                browser.SelectedTypeFilter = "Ses";
                if (FindNode(browser, "f4_alpha.txt") != null)
                    throw new Exception("Type filter did not hide non-matching files.");
                browser.SelectedTypeFilter = "Tümü";
                if (FindNode(browser, "f4_alpha.txt") == null)
                    throw new Exception("Resetting type filter did not restore files.");

                // Thumbnail erişimi headless'ta patlamamalı (desteklenmeyen tür → null).
                var txtNode = FindNode(browser, "f4_alpha.txt")!;
                if (txtNode.Thumbnail != null || txtNode.HasThumbnail)
                    throw new Exception("Non-image node must not produce a thumbnail.");

                // Silme onayı: ret → dosya yaşar + seçim korunur.
                var target = FindNode(browser, "f4_sil.txt")!;
                browser.SelectedNode = target;
                browser.ConfirmDeleteAsync = (_, _) => Task.FromResult(false);
                browser.DeleteAssetAsync().GetAwaiter().GetResult();
                if (!File.Exists(doomed) || browser.SelectedNode != target)
                    throw new Exception("Delete cancel did not preserve file and selection.");

                // Silme onayı: kabul → dosya gider + hayalet rozeti.
                browser.ConfirmDeleteAsync = (_, _) => Task.FromResult(true);
                browser.DeleteAssetAsync().GetAwaiter().GetResult();
                if (File.Exists(doomed))
                    throw new Exception("Confirmed delete did not remove the file.");
                if (browser.MissingAssetCount < 1)
                    throw new Exception("Confirmed delete did not raise a missing badge.");

                // Kayıp grup başlığı seçilemez.
                var group = browser.AssetTree.FirstOrDefault(n => n.RelativePath == "__missing__")
                    ?? throw new Exception("Missing group not found after delete.");
                if (!group.IsMissingGroup)
                    throw new Exception("Missing group did not flag IsMissingGroup.");
                browser.SelectedNode = group;
                if (browser.SelectedNode != null)
                    throw new Exception("Missing group header must not stay selected.");

                // Klasör adı sorma: enjeksiyon → belirtilen adla oluşturur.
                browser.SelectedNode = null;
                browser.PromptForFolderNameAsync = _ => Task.FromResult<string?>("F4Klasor");
                browser.CreateFolderAsync().GetAwaiter().GetResult();
                if (!Directory.Exists(folder))
                    throw new Exception("Prompted folder was not created.");

                // Vazgeçme (null) → klasör oluşturulmaz.
                browser.PromptForFolderNameAsync = _ => Task.FromResult<string?>(null);
                browser.CreateFolderAsync().GetAwaiter().GetResult();
                if (Directory.Exists(Path.Combine(assetsDir, "YeniKlasor")))
                    throw new Exception("Cancelled folder prompt created a directory.");

                // Rename bildirimi: boş ad sessizce yutulmaz (throw yok, düzen kapanır).
                var renameTarget = FindNode(browser, "f4_alpha.txt")!;
                renameTarget.StartRename();
                renameTarget.EditingName = "   ";
                renameTarget.CommitRenameCommand.Execute(null);
                if (renameTarget.IsEditing)
                    throw new Exception("Empty rename did not exit edit mode.");
            }
            finally
            {
                browser.ConfirmDeleteAsync = null;
                browser.PromptForFolderNameAsync = null;
                browser.SearchText = string.Empty;
                browser.SelectedTypeFilter = "Tümü";
                try { File.Delete(alpha); } catch { }
                try { File.Delete(beta); } catch { }
                try { File.WriteAllText(doomed, "x"); } catch { }
                try { Directory.Delete(folder, true); } catch { }
                browser.RefreshAssets();
            }
            Console.WriteLine("  [PASS] Assets search, filter, confirm, prompt and badges verified");
        }

        private static AssetNodeViewModel? FindNode(AssetBrowserViewModel browser, string fileName)
        {
            var stack = new System.Collections.Generic.Stack<AssetNodeViewModel>(browser.AssetTree);
            while (stack.Count > 0)
            {
                var node = stack.Pop();
                if (!node.IsDirectory && !node.IsMissingGroup && node.Name == fileName) return node;
                foreach (var child in node.Children) stack.Push(child);
            }
            return null;
        }
    }
}
