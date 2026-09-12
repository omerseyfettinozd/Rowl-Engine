using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorCameraTransitionTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 11]: Camera & Transition Components Lifecycle & Native P/Invoke...");

        // 1. Component Registry Discovery
        var availableTypes = ComponentRegistry.AvailableTypes;
        if (!availableTypes.Contains("camera") || !availableTypes.Contains("transition"))
            throw new Exception("ComponentRegistry missing 'camera' or 'transition' types");

        var camComp = ComponentRegistry.Create("camera") as CameraComponentViewModel;
        if (camComp == null || camComp.TypeKey != "camera")
            throw new Exception("ComponentRegistry failed to create CameraComponentViewModel");

        var transComp = ComponentRegistry.Create("transition") as TransitionComponentViewModel;
        if (transComp == null || transComp.TypeKey != "transition")
            throw new Exception("ComponentRegistry failed to create TransitionComponentViewModel");

        // 2. Camera Serialization and Deserialization
        camComp.X = 1200.0;
        camComp.Y = 600.0;
        camComp.Zoom = 2.0;
        camComp.PanDuration = 1.5;
        camComp.ZoomDuration = 1.0;
        camComp.Easing = "smooth_step";
        camComp.ShakeIntensity = 25.0;
        camComp.ShakeDuration = 0.5;

        var camSerialized = camComp.Serialize();
        if ((double)camSerialized["x"] != 1200.0 || (double)camSerialized["zoom"] != 2.0 ||
            (double)camSerialized["shake_intensity"] != 25.0)
            throw new Exception("CameraComponentViewModel serialization mismatch");

        var camDeserialized = new CameraComponentViewModel();
        camDeserialized.Deserialize(new Dictionary<string, object?>
        {
            ["x"] = 1200.0,
            ["y"] = 600.0,
            ["zoom"] = 2.0,
            ["pan_duration"] = 1.5,
            ["zoom_duration"] = 1.0,
            ["easing"] = "smooth_step",
            ["shake_intensity"] = 25.0,
            ["shake_duration"] = 0.5
        });
        if (camDeserialized.X != 1200.0 || camDeserialized.Zoom != 2.0 || camDeserialized.ShakeIntensity != 25.0)
            throw new Exception("CameraComponentViewModel deserialization mismatch");

        // 3. Transition Serialization and Deserialization
        transComp.Kind = "fade_color";
        transComp.Duration = 1.25;
        transComp.ColorHex = "#10B981";

        var transSerialized = transComp.Serialize();
        if ((string)transSerialized["kind"] != "fade_color" || (double)transSerialized["duration"] != 1.25 ||
            (string)transSerialized["color"] != "#10B981")
            throw new Exception("TransitionComponentViewModel serialization mismatch");

        var transDeserialized = new TransitionComponentViewModel();
        transDeserialized.Deserialize(new Dictionary<string, object?>
        {
            ["kind"] = "fade_color",
            ["duration"] = 1.25,
            ["color"] = "#10B981"
        });
        if (transDeserialized.Kind != "fade_color" || transDeserialized.Duration != 1.25 || transDeserialized.ColorHex != "#10B981")
            throw new Exception("TransitionComponentViewModel deserialization mismatch");

        // 4. StoryGraphLoaderService Integration with Camera and Transition
        string cameraStoryJson = """
        {
          "format_version": 4,
          "start_node_id": 401,
          "nodes": [
            {
              "id": 401,
              "title": "CameraNode",
              "editor_x": 100,
              "editor_y": 100,
              "objects": [
                {
                  "id": "cam_obj",
                  "name": "Camera Controller",
                  "is_active": true,
                  "components": [
                    {
                      "type": "camera",
                      "id": "cam1",
                      "enabled": true,
                      "data": { "x": 1100, "y": 550, "zoom": 1.5, "pan_duration": 1.0 }
                    },
                    {
                      "type": "transition",
                      "id": "tr1",
                      "enabled": true,
                      "data": { "kind": "wipe_left", "duration": 0.8 }
                    }
                  ]
                }
              ],
              "next_nodes": []
            }
          ]
        }
        """;

        using (var doc = JsonDocument.Parse(cameraStoryJson))
        {
            var loadResult = StoryGraphLoaderService.Load(doc);
            if (!loadResult.Success || loadResult.Nodes.Count != 1)
                throw new Exception("StoryGraphLoaderService failed to load camera test graph");

            var loadedNode = loadResult.Nodes[0];
            var loadedCam = loadedNode.AllComponents.OfType<CameraComponentViewModel>().FirstOrDefault();
            if (loadedCam == null || loadedCam.X != 1100.0 || loadedCam.Zoom != 1.5 || loadedCam.PanDuration != 1.0)
                throw new Exception("StoryGraphLoaderService hydrated camera component properties mismatch");

            var loadedTrans = loadedNode.AllComponents.OfType<TransitionComponentViewModel>().FirstOrDefault();
            if (loadedTrans == null || loadedTrans.Kind != "wipe_left" || loadedTrans.Duration != 0.8)
                throw new Exception("StoryGraphLoaderService hydrated transition component properties mismatch");
        }

        // 5. NativeBridge P/Invoke for Camera Tweening
        IntPtr nativeHandle = NativeBridge.RowlEngine_Create();
        if (nativeHandle != IntPtr.Zero)
        {
            try
            {
                if (NativeBridge.RowlEngine_Init(nativeHandle, 1920, 1080, 0) == 1)
                {
                    NativeBridge.RowlEngine_SetCamera(nativeHandle, 960.0f, 540.0f, 1.0f);
                    NativeBridge.RowlEngine_CameraPanTo(nativeHandle, 1200.0f, 600.0f, 0.5f, 3);
                    NativeBridge.RowlEngine_CameraZoomTo(nativeHandle, 2.0f, 0.5f, 3);
                    if (NativeBridge.RowlEngine_IsCameraMoving(nativeHandle) != 1)
                        throw new Exception("NativeBridge RowlEngine_IsCameraMoving expected 1 during tween");

                    NativeBridge.RowlEngine_ResetCamera(nativeHandle);
                }
            }
            finally
            {
                NativeBridge.RowlEngine_Destroy(nativeHandle);
            }
        }

        Console.WriteLine("  ✅ [PASS] Camera and Transition component models, hydration, and NativeBridge P/Invoke verified");
    }
}
