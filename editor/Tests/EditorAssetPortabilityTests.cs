using System;
using System.IO;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorAssetPortabilityTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 5]: Asset Auto-Copy & Project Portability (External Image Import)...");
        string tempExternalFile = Path.Combine(
            Path.GetTempPath(), "test_external_character_sprite.png");
        File.WriteAllBytes(
            tempExternalFile,
            new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A });

        string importedName = mainVm.ImportImageFileToProject(tempExternalFile);
        if (importedName != "test_external_character_sprite.png")
            throw new Exception($"ImportImageFileToProject returned unexpected name: {importedName}");

        string expectedDestination = Path.Combine(
            MainWindowViewModel.AssetsPath,
            "images",
            "test_external_character_sprite.png");
        if (!File.Exists(expectedDestination))
        {
            throw new Exception(
                $"Imported file was not found at expected project path: {expectedDestination}");
        }

        try { File.Delete(tempExternalFile); } catch { }
        try { File.Delete(expectedDestination); } catch { }
        Console.WriteLine(
            "  ✅ [PASS] External file automatically copied into project Assets/images/ " +
            "and linked via relative filename");
    }
}
