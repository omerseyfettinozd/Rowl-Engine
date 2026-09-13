using System;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor;

/// <summary>
/// MS-4 dirty-frame &amp; idle-diet regression gate. Pins the pure copy-gate
/// truth table (no native handle needed) and the native static query through
/// managed P/Invoke on a bare offscreen engine: settled → static (copy can be
/// skipped), transition/shake → active, dead handle → not-static.
/// </summary>
internal static class EditorFrameSkippingTests
{
    public static void Run()
    {
        Console.WriteLine("\n📌 [Test 30]: MS-4 dirty-frame copy gate & idle-diet counters...");

        // Pure gate: active always copies; a freshly settled frame copies
        // once (latch); a still frame on the same step skips; a step advance
        // always copies.
        if (!EngineHost.ShouldCopyFrame(frameActive: true, lastTickFrameActive: true, stepId: 7, lastCopiedStepId: 7))
            throw new Exception("MS-4: active frame must copy");
        if (!EngineHost.ShouldCopyFrame(frameActive: true, lastTickFrameActive: false, stepId: 7, lastCopiedStepId: 7))
            throw new Exception("MS-4: reactivated frame must copy");
        if (!EngineHost.ShouldCopyFrame(frameActive: false, lastTickFrameActive: true, stepId: 7, lastCopiedStepId: 7))
            throw new Exception("MS-4: just-settled frame must copy once");
        if (EngineHost.ShouldCopyFrame(frameActive: false, lastTickFrameActive: false, stepId: 7, lastCopiedStepId: 7))
            throw new Exception("MS-4: still frame on the same step must skip");
        if (!EngineHost.ShouldCopyFrame(frameActive: false, lastTickFrameActive: false, stepId: 8, lastCopiedStepId: 7))
            throw new Exception("MS-4: advanced step must copy even when still");

        // Native query through the managed bridge. An empty component scene
        // leaves no dialogue, script, or entity behind, so the frame is
        // provably still regardless of any autoloaded story graph.
        if (NativeBridge.RowlEngine_IsPreviewFrameStatic(IntPtr.Zero) != 0)
            throw new Exception("MS-4: dead handle must report not-static");

        IntPtr handle = NativeBridge.RowlEngine_Create();
        if (handle == IntPtr.Zero)
            throw new Exception("MS-4: failed to create native engine handle");
        try
        {
            if (NativeBridge.RowlEngine_Init(handle, 960, 540, 0) == 0)
                throw new Exception("MS-4: failed to init offscreen engine");

            NativeBridge.RowlEngine_UpdateSceneFromJson(handle, "[]");
            NativeBridge.RowlEngine_Step(handle, 0.0f);
            if (NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 1)
                throw new Exception("MS-4: settled frame must report static");

            NativeBridge.RowlEngine_StartTransition(handle, "crossfade", 0.4f, null);
            if (NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 0)
                throw new Exception("MS-4: active transition must report not-static");
            NativeBridge.RowlEngine_Step(handle, 0.25f);
            NativeBridge.RowlEngine_Step(handle, 0.25f);
            if (NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 1)
                throw new Exception("MS-4: completed transition must settle back to static");

            NativeBridge.RowlEngine_TriggerCameraShake(handle, 15.0f, 0.5f);
            if (NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 0)
                throw new Exception("MS-4: camera shake must report not-static");
            NativeBridge.RowlEngine_Step(handle, 0.25f);
            NativeBridge.RowlEngine_Step(handle, 0.25f);
            NativeBridge.RowlEngine_Step(handle, 0.25f);
            if (NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 1)
                throw new Exception("MS-4: completed shake must settle back to static");
        }
        finally
        {
            NativeBridge.RowlEngine_Shutdown(handle);
            NativeBridge.RowlEngine_Destroy(handle);
        }

        Console.WriteLine("  ✅ [PASS] MS-4 dirty-frame gate truth table and native static query verified");
    }
}
