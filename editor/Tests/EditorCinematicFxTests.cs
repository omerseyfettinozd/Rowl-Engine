using System;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorCinematicFxTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 26]: Cinematic Camera Shake Presets & Screen Visual FX Pipeline...");

        // Step 26.1: CameraComponentViewModel Preset Selection & Auto-Population
        Console.WriteLine("    [Step 26.1]: CameraComponentViewModel Shake Preset Selection & Parameters...");
        var cam = new CameraComponentViewModel();
        cam.ShakePreset = "Earthquake";
        if (Math.Abs(cam.ShakeIntensity - 18.0) > 0.01 || Math.Abs(cam.ShakeDuration - 1.5) > 0.01 ||
            Math.Abs(cam.ShakeFrequency - 12.0) > 0.01 || Math.Abs(cam.ShakeDamping - 0.8) > 0.01 ||
            Math.Abs(cam.ShakeDirectionX - 1.0) > 0.01 || Math.Abs(cam.ShakeDirectionY - 0.2) > 0.01)
        {
            throw new Exception("Earthquake shake preset failed to auto-populate directional parameters");
        }

        cam.ShakePreset = "Explosion";
        if (Math.Abs(cam.ShakeIntensity - 35.0) > 0.01 || Math.Abs(cam.ShakeDamping - 2.5) > 0.01)
        {
            throw new Exception("Explosion shake preset failed to configure sharp blast damping");
        }

        cam.ShakePreset = "Heartbeat";
        if (Math.Abs(cam.ShakeDirectionX - 0.15) > 0.01 || Math.Abs(cam.ShakeDirectionY - 1.0) > 0.01)
        {
            throw new Exception("Heartbeat shake preset failed to configure vertical throb profile");
        }

        cam.ResetToCenter();
        if (cam.ShakePreset != "None" || cam.ShakeIntensity != 0.0 || cam.ShakeDuration != 0.0)
        {
            throw new Exception("CameraComponentViewModel.ResetToCenter did not reset shake parameters");
        }

        // Step 26.2: CameraComponentViewModel Serialization Round-Trip
        Console.WriteLine("    [Step 26.2]: CameraComponentViewModel Serialization Round-Trip...");
        cam.ShakePreset = "Subtle";
        cam.ShakeDamping = 1.2;
        cam.ShakeDirectionX = 0.6;
        cam.ShakeDirectionY = 0.6;
        var camDict = cam.Serialize();
        if (!camDict.ContainsKey("shake_preset") || (string)camDict["shake_preset"] != "subtle")
            throw new Exception("CameraComponentViewModel did not serialize shake_preset");

        var camDeser = new CameraComponentViewModel();
        camDeser.Deserialize(camDict.ToDictionary(k => k.Key, k => (object?)k.Value));
        if (camDeser.ShakePreset != "subtle" || Math.Abs(camDeser.ShakeDamping - 1.2) > 0.01 ||
            Math.Abs(camDeser.ShakeDirectionX - 0.6) > 0.01)
        {
            throw new Exception("CameraComponentViewModel failed to deserialize shake profile");
        }

        // Step 26.3: TransitionComponentViewModel Screen FX (Flash, Tint, Vignette) Serialization
        Console.WriteLine("    [Step 26.3]: TransitionComponentViewModel Screen FX Serialization...");
        var trans = new TransitionComponentViewModel
        {
            Kind = "crossfade",
            Duration = 0.8,
            FlashEnabled = true,
            FlashColorHex = "#FFCCAA",
            FlashDuration = 0.35,
            FlashIntensity = 0.9,
            TintEnabled = true,
            TintColorHex = "#0A183D",
            TintOpacity = 0.4,
            VignetteEnabled = true,
            VignetteIntensity = 0.7,
            VignetteRadius = 0.8,
            VignetteColorHex = "#110505"
        };
        var transDict = trans.Serialize();
        if (!transDict.ContainsKey("flash_enabled") || !transDict.ContainsKey("tint_enabled") || !transDict.ContainsKey("vignette_enabled"))
            throw new Exception("TransitionComponentViewModel did not serialize screen visual FX");

        var transDeser = new TransitionComponentViewModel();
        transDeser.Deserialize(transDict.ToDictionary(k => k.Key, k => (object?)k.Value));
        if (!transDeser.FlashEnabled || transDeser.FlashColorHex != "#FFCCAA" ||
            !transDeser.TintEnabled || Math.Abs(transDeser.TintOpacity - 0.4) > 0.01 ||
            !transDeser.VignetteEnabled || Math.Abs(transDeser.VignetteIntensity - 0.7) > 0.01)
        {
            throw new Exception("TransitionComponentViewModel failed to deserialize screen visual FX");
        }

        // Step 26.4: EngineHost Camera Shake Preset P/Invoke
        Console.WriteLine("    [Step 26.4]: EngineHost Camera Shake Preset P/Invoke Execution...");
        mainVm.EngineHost.ResetCamera();
        mainVm.EngineHost.TriggerCameraShakePreset("earthquake", 1.0f, 0.5f);
        if (!mainVm.EngineHost.IsCameraMoving())
            throw new Exception("EngineHost.IsCameraMoving returned false after TriggerCameraShakePreset");
        mainVm.EngineHost.Step(0.05f);
        float offsetX = mainVm.EngineHost.GetCameraShakeOffsetX();
        if (Math.Abs(offsetX) < 0.0001f)
            throw new Exception("Camera shake offset X was not computed during active earthquake shake");
        mainVm.EngineHost.ResetCamera();
        if (mainVm.EngineHost.IsCameraMoving() || mainVm.EngineHost.GetCameraShakeOffsetX() != 0.0f)
            throw new Exception("ResetCamera failed to zero camera shake");

        // Step 26.5: EngineHost Screen Flash Trigger & Lifecycle
        Console.WriteLine("    [Step 26.5]: EngineHost Screen Flash Trigger & Lifecycle...");
        mainVm.EngineHost.TriggerScreenFlashHex("#FFFFFF", 0.3f, 1.0f);
        if (!mainVm.EngineHost.IsScreenFlashActive())
            throw new Exception("EngineHost.IsScreenFlashActive returned false after TriggerScreenFlashHex");
        mainVm.EngineHost.Step(0.2f);
        mainVm.EngineHost.Step(0.2f);
        if (mainVm.EngineHost.IsScreenFlashActive())
            throw new Exception("EngineHost.IsScreenFlashActive returned true after flash duration expired");

        // Step 26.6: EngineHost Screen Tint Set, Query & Clear
        Console.WriteLine("    [Step 26.6]: EngineHost Screen Tint Set & Clear...");
        mainVm.EngineHost.SetScreenTintHex("#0A183D", 0.55f);
        if (Math.Abs(mainVm.EngineHost.GetScreenTintOpacity() - 0.55f) > 0.01f)
            throw new Exception("EngineHost.GetScreenTintOpacity mismatch");
        mainVm.EngineHost.Step(0.016f);
        mainVm.EngineHost.ClearScreenTint();
        if (mainVm.EngineHost.GetScreenTintOpacity() != 0.0f)
            throw new Exception("EngineHost.ClearScreenTint failed to clear tint opacity");

        // Step 26.7: EngineHost Vignette Post-Process
        Console.WriteLine("    [Step 26.7]: EngineHost Vignette Post-Process Activation...");
        mainVm.EngineHost.SetVignette(0.75f, 0.7f, "#000000");
        if (Math.Abs(mainVm.EngineHost.GetVignetteIntensity() - 0.75f) > 0.01f)
            throw new Exception("EngineHost.GetVignetteIntensity mismatch");
        mainVm.EngineHost.Step(0.016f);

        // Step 26.8: StoryGraphSerializer Active Story Round-Trip & Scene Component Ingestion
        Console.WriteLine("    [Step 26.8]: StoryGraphSerializer & JSON Ingestion of Shake & Screen FX...");
        var fxNode = new NodeViewModel(2601, "Visual FX Node", 200, 300, bare: true);
        var nodeCam = fxNode.AddComponent<CameraComponentViewModel>();
        nodeCam.ShakePreset = "Explosion";
        nodeCam.ShakeIntensity = 40.0;
        nodeCam.ShakeDuration = 0.6;
        var nodeTrans = fxNode.AddComponent<TransitionComponentViewModel>();
        nodeTrans.Kind = "crossfade";
        nodeTrans.FlashEnabled = true;
        nodeTrans.FlashColorHex = "#FFEEAA";
        nodeTrans.TintEnabled = true;
        nodeTrans.TintColorHex = "#200A3D";
        nodeTrans.TintOpacity = 0.45;
        nodeTrans.VignetteEnabled = true;
        nodeTrans.VignetteIntensity = 0.8;

        string fxStoryJson = StoryGraphSerializer.SerializeActiveStory(fxNode);
        if (!fxStoryJson.Contains("\"shake_preset\": \"explosion\"") && !fxStoryJson.Contains("\"shake_preset\":\"explosion\""))
            throw new Exception("SerializeActiveStory did not serialize shake_preset");
        if (!fxStoryJson.Contains("\"flash_enabled\": true") && !fxStoryJson.Contains("\"flash_enabled\":true"))
            throw new Exception("SerializeActiveStory did not serialize flash_enabled");
        if (!fxStoryJson.Contains("\"tint_enabled\": true") && !fxStoryJson.Contains("\"tint_enabled\":true"))
            throw new Exception("SerializeActiveStory did not serialize tint_enabled");
        if (!fxStoryJson.Contains("\"vignette_enabled\": true") && !fxStoryJson.Contains("\"vignette_enabled\":true"))
            throw new Exception("SerializeActiveStory did not serialize vignette_enabled");

        mainVm.EngineHost.UpdateSceneFromComponents(fxStoryJson);
        if (!mainVm.EngineHost.IsCameraMoving())
            throw new Exception("EngineHost did not activate camera shake from serialized active story JSON");
        if (!mainVm.EngineHost.IsScreenFlashActive())
            throw new Exception("EngineHost did not activate screen flash from serialized active story JSON");
        if (Math.Abs(mainVm.EngineHost.GetScreenTintOpacity() - 0.45f) > 0.01f)
            throw new Exception("EngineHost did not set tint opacity from serialized active story JSON");
        if (Math.Abs(mainVm.EngineHost.GetVignetteIntensity() - 0.8f) > 0.01f)
            throw new Exception("EngineHost did not set vignette intensity from serialized active story JSON");

        mainVm.EngineHost.Step(0.016f);
        mainVm.EngineHost.ClearScreenTint();
        mainVm.EngineHost.SetVignette(0.0f);
        mainVm.EngineHost.ResetCamera();

        Console.WriteLine("  ✅ [PASS] Cinematic Camera Shake Presets & Screen Visual FX Pipeline verified");
    }
}
