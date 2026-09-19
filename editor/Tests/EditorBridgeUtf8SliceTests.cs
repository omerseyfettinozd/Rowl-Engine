using System;
using System.IO;
using System.Runtime.InteropServices;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

// B3a: A5-tur3 + B2a + B2b'nin eklediği 14 API'nin C# köprü girişleri
// (NativeBridge P/Invoke) gerçek-lib'e karşı kanıtlanır. Her Fact canlı
// handle'da caller-buffer sözleşmesini (NULL/0 size-query, undersized
// BufferTooSmall + cleared, exact-copy parity) ve davranışı doğrular;
// EngineHost'a dokunulmaz (sıfır-diff kuralı), call-site göçü B5'indir.
// D3: canlı native host VIDEO lease'i süreç-genelidir; paralel xUnit
// koleksiyonlarıyla çakışınca lease-affinity Init'i reddeder. Seri
// koleksiyonda koşar.
[Collection("StaticRootSequential")]
public sealed class EditorBridgeUtf8SliceTests
{
    private delegate NativeBridge.ResultCode Utf8Getter(
        IntPtr buffer, uint bufferSize, out uint requiredSize);

    private static string ReadUtf8(string name, Utf8Getter getter)
    {
        if (getter(IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
            required == 0)
            throw new Exception($"B3a {name}: UTF-8 size query failed");
        IntPtr buffer = Marshal.AllocHGlobal((int)required);
        try
        {
            if (getter(buffer, required, out uint repeated) != NativeBridge.ResultCode.Ok ||
                repeated != required)
                throw new Exception($"B3a {name}: UTF-8 exact-size copy failed");
            return Marshal.PtrToStringUTF8(buffer) ??
                throw new Exception($"B3a {name}: UTF-8 decode failed");
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static void CheckUndersized(string name, Utf8Getter getter)
    {
        if (getter(IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
            required < 2)
            throw new Exception($"B3a {name}: size query must report 2+ bytes");
        byte[] tiny = new byte[required - 1];
        for (int i = 0; i < tiny.Length; i++) tiny[i] = (byte)'x';
        IntPtr buffer = Marshal.AllocHGlobal(tiny.Length);
        try
        {
            Marshal.Copy(tiny, 0, buffer, tiny.Length);
            if (getter(buffer, (uint)tiny.Length, out uint repeated) !=
                    NativeBridge.ResultCode.BufferTooSmall ||
                repeated != required || Marshal.ReadByte(buffer) != 0)
                throw new Exception($"B3a {name}: undersized contract failed");
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static IntPtr CreateInitEngine()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        IntPtr handle = NativeBridge.RowlEngine_Create();
        if (handle == IntPtr.Zero)
            throw new Exception("B3a: failed to create native engine handle");
        if (NativeBridge.RowlEngine_Init(handle, 64, 64, 0) == 0)
        {
            NativeBridge.RowlEngine_Destroy(handle);
            throw new Exception("B3a: failed to init offscreen engine");
        }
        return handle;
    }

    private static void DestroyEngine(IntPtr handle)
    {
        NativeBridge.RowlEngine_Shutdown(handle);
        NativeBridge.RowlEngine_Destroy(handle);
    }

    [Fact]
    public void B3a_StateUtf8_RoundTripAndBufferContract()
    {
        IntPtr handle = CreateInitEngine();
        try
        {
            NativeBridge.RowlEngine_SetVariable(handle, "b3a_key", "b3a_değer-测试");
            string value = ReadUtf8("variable", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetVariableUtf8(handle, "b3a_key", b, s, out r));
            if (value != "b3a_değer-测试")
                throw new Exception($"B3a variable round-trip mismatch: [{value}]");

            string diagnostics = ReadUtf8("diagnostics", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8(handle, b, s, out r));
            if (diagnostics != "[]")
                throw new Exception($"B3a diagnostics mismatch: [{diagnostics}]");
            CheckUndersized("diagnostics", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8(handle, b, s, out r));

            string history = ReadUtf8("history", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetDialogueHistoryJsonUtf8(handle, b, s, out r));
            if (history != "[]")
                throw new Exception($"B3a history mismatch: [{history}]");
        }
        finally
        {
            DestroyEngine(handle);
        }
    }

    [Fact]
    public void B3a_LastResultUtf8_TripleAfterFailedLoad()
    {
        IntPtr handle = CreateInitEngine();
        string missing = Path.Combine(
            Path.GetTempPath(), $"RowlB3aMissing_{Guid.NewGuid():N}.json");
        try
        {
            NativeBridge.RowlEngine_LoadStoryGraph(handle, missing);
            string operation = ReadUtf8("last-result-operation",
                (IntPtr b, uint s, out uint r) =>
                    NativeBridge.RowlEngine_GetLastResultOperationUtf8(handle, b, s, out r));
            string message = ReadUtf8("last-result-message",
                (IntPtr b, uint s, out uint r) =>
                    NativeBridge.RowlEngine_GetLastResultMessageUtf8(handle, b, s, out r));
            string target = ReadUtf8("last-result-target",
                (IntPtr b, uint s, out uint r) =>
                    NativeBridge.RowlEngine_GetLastResultTargetUtf8(handle, b, s, out r));
            if (operation != "load_story_graph_path")
                throw new Exception($"B3a last-result operation mismatch: [{operation}]");
            if (message.Length == 0)
                throw new Exception("B3a last-result message must be non-empty after failed load");
            if (!target.Contains(Path.GetFileName(missing)))
                throw new Exception($"B3a last-result target mismatch: [{target}]");
        }
        finally
        {
            DestroyEngine(handle);
        }
    }

    [Fact]
    public void B3a_StoryUtf8_SpeakerDialogueAndGraphError()
    {
        IntPtr handle = CreateInitEngine();
        string missing = Path.Combine(
            Path.GetTempPath(), $"RowlB3aMissing_{Guid.NewGuid():N}.json");
        try
        {
            NativeBridge.RowlEngine_UpdateSceneFromJson(handle,
                "[{\"type\":\"speaker\",\"data\":{\"speaker\":\"B3a-Anlatıcı\"," +
                "\"dialogue\":\"B3a deneme cümlesi\"}}]");
            string speaker = ReadUtf8("speaker", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetSpeakerUtf8(handle, b, s, out r));
            string dialogue = ReadUtf8("dialogue", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetDialogueUtf8(handle, b, s, out r));
            if (speaker != "B3a-Anlatıcı" || dialogue != "B3a deneme cümlesi")
                throw new Exception($"B3a speaker/dialogue mismatch: [{speaker}] / [{dialogue}]");

            NativeBridge.RowlEngine_LoadStoryGraph(handle, missing);
            string graphError = ReadUtf8("story-graph-error",
                (IntPtr b, uint s, out uint r) =>
                    NativeBridge.RowlEngine_GetLastStoryGraphErrorUtf8(handle, b, s, out r));
            if (graphError.Length == 0)
                throw new Exception("B3a story-graph error must be non-empty after missing load");
        }
        finally
        {
            DestroyEngine(handle);
        }
    }

    [Fact]
    public void B3a_AudioUtf8_BlipSoundAndErrorChannel()
    {
        IntPtr handle = CreateInitEngine();
        try
        {
            NativeBridge.RowlEngine_SetDialogueVoiceBlip(
                handle, "voices/b3a.wav", 1.0f, 0.1f, 2, 0, 0);
            string blip = ReadUtf8("voice-blip-sound",
                (IntPtr b, uint s, out uint r) =>
                    NativeBridge.RowlEngine_GetDialogueVoiceBlipSoundUtf8(handle, b, s, out r));
            if (blip != "voices/b3a.wav")
                throw new Exception($"B3a voice-blip mismatch: [{blip}]");

            // Error channel is environment-dependent (empty when healthy);
            // the contract is Ok + exact required size on both reads.
            if (NativeBridge.RowlEngine_GetLastAudioErrorUtf8(
                    handle, IntPtr.Zero, 0, out uint first) != NativeBridge.ResultCode.Ok)
                throw new Exception("B3a audio-error size query failed");
            if (NativeBridge.RowlEngine_GetLastAudioErrorUtf8(
                    handle, IntPtr.Zero, 0, out uint second) != NativeBridge.ResultCode.Ok ||
                second != first)
                throw new Exception("B3a audio-error size query unstable");
        }
        finally
        {
            DestroyEngine(handle);
        }
    }

    [Fact]
    public void B3a_PauseMenuUtf8_ReportsJson()
    {
        IntPtr handle = CreateInitEngine();
        try
        {
            string menu = ReadUtf8("pause-menu", (IntPtr b, uint s, out uint r) =>
                NativeBridge.RowlEngine_GetPauseMenuJsonUtf8(handle, b, s, out r));
            if (!menu.Contains("\"open\""))
                throw new Exception($"B3a pause-menu JSON mismatch: [{menu}]");
        }
        finally
        {
            DestroyEngine(handle);
        }
    }

    [Fact]
    public void B3a_AudioCounters_StartAtZero()
    {
        IntPtr handle = CreateInitEngine();
        try
        {
            if (NativeBridge.RowlEngine_GetAudioDropCount(handle) != 0)
                throw new Exception("B3a fresh engine must report zero audio drops");
            if (NativeBridge.RowlEngine_GetSynthBlipCount(handle) != 0)
                throw new Exception("B3a fresh engine must report zero synth blips");
        }
        finally
        {
            DestroyEngine(handle);
        }
    }
}
