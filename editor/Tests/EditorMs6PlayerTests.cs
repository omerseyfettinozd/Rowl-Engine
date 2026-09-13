using System;
using System.IO;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor;

// MS-6 regression gate (headless suite, Test 32): the editor preview drives a
// non-playing engine, so a mid-typing line must complete on the first advance
// request and move on only on the second — for both the keyboard path
// (AdvanceNode, like MainWindow Space/Enter) and the pointer composition
// (PointerDown + AdvanceNode, like EnginePreviewControl clicks).
internal static class EditorMs6PlayerTests
{
    public static void Run()
    {
        Console.WriteLine("\n📌 [Test 32]: MS-6 preview click-to-complete (editor headless)...");

        string graphPath = Path.Combine(Path.GetTempPath(), $"RowlMs6Preview_{Guid.NewGuid():N}.json");
        File.WriteAllText(graphPath,
            "{\"format_version\":4,\"start_node_id\":101,\"nodes\":[" +
            "{\"id\":101,\"speaker\":\"Evelyn\",\"dialogue\":\"Typed.\",\"components\":[" +
            "{\"type\":\"dialogue\",\"id\":\"d_ms6\",\"enabled\":true,\"data\":{" +
            "\"speaker\":\"Evelyn\"," +
            "\"dialogue\":\"This line is long enough that a short step leaves it mid-typing.\"," +
            "\"typewriter_enabled\":true,\"text_speed\":30}}]," +
            "\"next_nodes\":[{\"id\":102}]}," +
            "{\"id\":102,\"speaker\":\"Evelyn\",\"dialogue\":\"Second.\"}]}");

        IntPtr handle = NativeBridge.RowlEngine_Create();
        if (handle == IntPtr.Zero)
            throw new Exception("MS-6: failed to create native engine handle");
        try
        {
            if (NativeBridge.RowlEngine_Init(handle, 960, 540, 0) == 0)
                throw new Exception("MS-6: failed to init offscreen engine");

            NativeBridge.RowlEngine_LoadStoryGraph(handle, graphPath);
            NativeBridge.RowlEngine_Step(handle, 0.0f);
            if (NativeBridge.RowlEngine_GetCurrentNodeId(handle) != 101)
                throw new Exception("MS-6: fixture did not present node 101");

            // Paused presentation reports a static frame (MS-4 gate intact).
            if (NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 1)
                throw new Exception("MS-6: paused typewriter presentation must report a static frame");

            NativeBridge.RowlEngine_Step(handle, 0.2f);
            NativeBridge.RowlEngine_AdvanceNode(handle, 0);
            if (NativeBridge.RowlEngine_GetCurrentNodeId(handle) != 101)
                throw new Exception("MS-6: mid-typing AdvanceNode skipped instead of completing");
            NativeBridge.RowlEngine_AdvanceNode(handle, 0);
            if (NativeBridge.RowlEngine_GetCurrentNodeId(handle) != 102)
                throw new Exception("MS-6: completed line did not advance");

            // Pointer composition parity (EnginePreviewControl pattern).
            NativeBridge.RowlEngine_LoadStoryGraph(handle, graphPath);
            NativeBridge.RowlEngine_Step(handle, 0.0f);
            NativeBridge.RowlEngine_Step(handle, 0.2f);
            ClickEmptyCanvas(handle);
            if (NativeBridge.RowlEngine_GetCurrentNodeId(handle) != 101)
                throw new Exception("MS-6: mid-typing preview click skipped instead of completing");
            ClickEmptyCanvas(handle);
            if (NativeBridge.RowlEngine_GetCurrentNodeId(handle) != 102)
                throw new Exception("MS-6: completed line did not advance on second click");
        }
        finally
        {
            NativeBridge.RowlEngine_Shutdown(handle);
            NativeBridge.RowlEngine_Destroy(handle);
            try { File.Delete(graphPath); } catch { }
        }

        Console.WriteLine("  ✅ [PASS] MS-6 preview click-to-complete verified");
    }

    private static void ClickEmptyCanvas(IntPtr handle)
    {
        // 960x540 offscreen maps physical (480,50) to virtual (960,100):
        // empty canvas in this linear graph (no choice buttons, no bezel).
        if (NativeBridge.RowlEngine_PointerDown(handle, 480.0f, 50.0f) == 0)
            NativeBridge.RowlEngine_AdvanceNode(handle, 0);
    }
}
