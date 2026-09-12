using System;
using System.IO;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorProjectLifecycleTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 18]: EditorProjectLifecycleCoordinator & EditorVisualAssetPickerService Isolation...");

        // Step 18.1: ResolveUnsavedChangesAsync State Transitions
        Console.WriteLine("    [Step 18.1]: ResolveUnsavedChangesAsync State Transitions...");
        bool isDirty = false;
        bool saved = false;

        // 1. Not dirty -> true without invoking dialog
        bool cleanResult = EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("cancel"),
            () => saved = true,
            () => isDirty,
            d => isDirty = d).GetAwaiter().GetResult();
        if (!cleanResult || saved)
            throw new Exception("Clean state should resolve immediately without saving.");

        // 2. Dirty + Cancel -> false, remains dirty
        isDirty = true;
        bool cancelResult = EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("cancel"),
            () => saved = true,
            () => isDirty,
            d => isDirty = d).GetAwaiter().GetResult();
        if (cancelResult || !isDirty || saved)
            throw new Exception("Cancel should abort resolution and leave project dirty.");

        // 3. Dirty + Discard -> true, clears dirty without save
        isDirty = true;
        bool discardResult = EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("discard"),
            () => saved = true,
            () => isDirty,
            d => isDirty = d).GetAwaiter().GetResult();
        if (!discardResult || isDirty || saved)
            throw new Exception("Discard should clear dirty flag and return true without saving.");

        // 4. Dirty + Save -> saves and clears dirty
        isDirty = true;
        saved = false;
        bool saveResult = EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
            () => Task.FromResult<string?>("save"),
            () => { saved = true; isDirty = false; },
            () => isDirty,
            d => isDirty = d).GetAwaiter().GetResult();
        if (!saveResult || isDirty || !saved)
            throw new Exception("Save choice should invoke save callback and mark clean.");

        // Step 18.2: ConfirmDeleteSaveSlotAsync Verification
        Console.WriteLine("    [Step 18.2]: ConfirmDeleteSaveSlotAsync Verification...");
        bool confirmed = EditorProjectLifecycleCoordinator.ConfirmDeleteSaveSlotAsync(
            () => Task.FromResult<bool?>(true)).GetAwaiter().GetResult();
        bool denied = EditorProjectLifecycleCoordinator.ConfirmDeleteSaveSlotAsync(
            () => Task.FromResult<bool?>(false)).GetAwaiter().GetResult();
        bool dismissed = EditorProjectLifecycleCoordinator.ConfirmDeleteSaveSlotAsync(
            () => Task.FromResult<bool?>(null)).GetAwaiter().GetResult();

        if (!confirmed || denied || dismissed)
            throw new Exception("ConfirmDeleteSaveSlotAsync did not evaluate dialog result properly.");

        // Step 18.3: GenerateSaveAsTargetDirectory & ExecuteSaveAs
        Console.WriteLine("    [Step 18.3]: GenerateSaveAsTargetDirectory & ExecuteSaveAs...");
        var testTime = new DateTime(2026, 9, 11, 15, 45, 0);
        string generatedDir = EditorProjectLifecycleCoordinator.GenerateSaveAsTargetDirectory("/tmp/parent", testTime);
        string expectedDir = Path.Combine("/tmp/parent", "RowlProject_2026-09-11_15-45");
        if (generatedDir != expectedDir)
            throw new Exception($"GenerateSaveAsTargetDirectory generated unexpected path: {generatedDir}");

        string saveAsTargetDir = Path.Combine(Path.GetTempPath(), $"RowlTestSaveAs_{Guid.NewGuid():N}");
        try
        {
            bool saveHookCalled = false;
            var coordSaveAsResult = EditorProjectLifecycleCoordinator.ExecuteSaveAs(
                testProjectRoot,
                saveAsTargetDir,
                mainVm.Nodes.Count,
                mainVm.GetStartNode()?.Id ?? 101,
                () => saveHookCalled = true);

            if (!coordSaveAsResult.Succeeded || !saveHookCalled)
                throw new Exception("ExecuteSaveAs failed or did not flush source project before copying.");

            if (!File.Exists(Path.Combine(saveAsTargetDir, "project.rowlproj")) ||
                !Directory.Exists(Path.Combine(saveAsTargetDir, "Assets")))
                throw new Exception("ExecuteSaveAs did not copy Assets and project descriptor.");
        }
        finally
        {
            try { Directory.Delete(saveAsTargetDir, true); } catch { }
        }

        // Step 18.4: ExecuteOpenProject Resolution & Rollback Isolation
        Console.WriteLine("    [Step 18.4]: ExecuteOpenProject Resolution & Rollback Isolation...");
        var invalidOpen = EditorProjectLifecycleCoordinator.ExecuteOpenProject(
            "/nonexistent_dir_12345",
            testProjectRoot,
            testProjectRoot,
            () => { },
            (r, p) => { },
            _ => { },
            () => true,
            () => { },
            () => { });

        if (invalidOpen.Succeeded)
            throw new Exception("ExecuteOpenProject should fail on non-existent directory.");

        bool engineMounted = false;
        bool graphLoaded = false;
        var validOpen = EditorProjectLifecycleCoordinator.ExecuteOpenProject(
            testProjectRoot,
            testProjectRoot,
            testProjectRoot,
            () => { },
            (r, p) => { },
            _ => engineMounted = true,
            () => { graphLoaded = true; return true; },
            () => { },
            () => { });

        if (!validOpen.Succeeded || !engineMounted || !graphLoaded)
            throw new Exception("ExecuteOpenProject failed on valid project root.");

        // Step 18.5: EditorVisualAssetPickerService Component Assignment
        Console.WriteLine("    [Step 18.5]: EditorVisualAssetPickerService Component Assignment...");
        string testImagesFolder = EditorVisualAssetPickerService.EnsureAssetsImagesFolder(Path.Combine(testProjectRoot, "Assets"));
        if (!Directory.Exists(testImagesFolder))
            throw new Exception("EnsureAssetsImagesFolder did not create images folder.");

        var charComponent = new CharacterComponentViewModel();
        bool charAssigned = EditorVisualAssetPickerService.ApplyImageAssetToComponent(charComponent, "hero_test.png");
        if (!charAssigned || charComponent.Sprite != "hero_test.png")
            throw new Exception("ApplyImageAssetToComponent failed on CharacterComponentViewModel.");

        var bgComponent = new BackgroundComponentViewModel();
        bool bgAssigned = EditorVisualAssetPickerService.ApplyImageAssetToComponent(bgComponent, "scenery_test.png");
        if (!bgAssigned || bgComponent.Texture != "scenery_test.png")
            throw new Exception("ApplyImageAssetToComponent failed on BackgroundComponentViewModel.");

        var dialogueComponent = new DialogueComponentViewModel();
        bool unsupportedAssigned = EditorVisualAssetPickerService.ApplyImageAssetToComponent(dialogueComponent, "ignored.png");
        if (unsupportedAssigned)
            throw new Exception("ApplyImageAssetToComponent should return false for unsupported component type.");

        Console.WriteLine("  ✅ [PASS] EditorProjectLifecycleCoordinator & EditorVisualAssetPickerService Isolation verified");
    }
}
