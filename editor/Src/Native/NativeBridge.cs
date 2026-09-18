/**
 * NativeBridge.cs
 *
 * P/Invoke declarations for the native RowlEngineCore shared library.
 *
 * All string parameters and return values are explicitly marshaled as UTF-8
 * to support full international character sets (Turkish, Japanese, etc.).
 */

using System;
using System.Runtime.InteropServices;

namespace RowlEngine.Editor.Native
{
    internal static class NativeBridge
    {
        private const string Lib = "RowlEngineCore";

        internal enum ResultCode
        {
            Ok = 0,
            InvalidHandle = 1,
            InvalidArgument = 2,
            FileNotFound = 3,
            FileTooLarge = 4,
            ParseError = 5,
            ValidationError = 6,
            IoError = 7,
            ScriptSyntaxError = 8,
            ScriptRuntimeError = 9,
            AudioDecodeError = 10,
            StateError = 11,
            BufferTooSmall = 12,
            Unsupported = 13,
            UnknownError = 99,
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ApiVersion
        {
            internal uint Major;
            internal uint Minor;
            internal uint Patch;
        }

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetApiVersion(out ApiVersion version);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetCapabilities(out ulong capabilities);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetSaveDirectoryUtf8(
            IntPtr handle, IntPtr buffer, uint bufferSize, out uint requiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetProfileDirectoryUtf8(
            IntPtr handle, IntPtr buffer, uint bufferSize, out uint requiredSize);

        // ── Lifecycle ────────────────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_Create();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_Destroy(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_Init(
            IntPtr handle,
            uint virtualWidth,
            uint virtualHeight,
            int vsync);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_Step(IntPtr handle, float deltaTime);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_Shutdown(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsRunning(IntPtr handle);

        // ── Native window embedding ──────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetExternalWindowHandle(
            IntPtr handle,
            IntPtr nativeWindowHandle,
            uint width,
            uint height);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ResizeViewport(
            IntPtr handle,
            uint newWidth,
            uint newHeight);

        // ── Offscreen Framebuffer & Playback Control ──────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetPixelBuffer(
            IntPtr handle,
            out uint outW,
            out uint outH);

        // MS-0 pitch contract: outPitch is the surface row stride in bytes,
        // always >= outW*4 on success. Null-handle fallback zeroes all outs.
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetPixelBufferEx(
            IntPtr handle,
            out uint outW,
            out uint outH,
            out uint outPitch);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetTextureCacheBudgetBytes(
            IntPtr handle,
            ulong bytes);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetTextureCacheBudgetBytes(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetTextureCacheEvictionCount(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double RowlEngine_GetLastFrameTextureLoadMilliseconds(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double RowlEngine_GetLastFrameNonTextureRenderMilliseconds(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double RowlEngine_GetLastFrameTextRasterizationMilliseconds(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern double RowlEngine_GetLastFrameRendererFlushMilliseconds(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetPlayState(
            IntPtr handle,
            int isPlaying);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetPaused(
            IntPtr handle,
            int paused);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsPaused(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint RowlEngine_GetChoiceCount(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetChoiceLabelAtUtf8(
            IntPtr handle,
            uint index,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetChoiceOptionIdAtUtf8(
            IntPtr handle,
            uint index,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetSaveSlotMetadataJson(
            IntPtr handle,
            int slotIndex,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ResetToStartNode(
            IntPtr handle);

        // ── Scene / story control ────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_UpdateScene(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string speaker,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string dialogue,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string background,
            float bgX,   float bgY,   float bgW,   float bgH,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string character,
            float charX, float charY, float charW, float charH,
            float dlgX,  float dlgY,  float dlgW,  float dlgH);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_UpdateSceneEx(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string speaker,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string dialogue,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string background,
            float bgX,   float bgY,   float bgW,   float bgH,   float bgRot,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string character,
            float charX, float charY, float charW, float charH, float charRot,
            float dlgX,  float dlgY,  float dlgW,  float dlgH);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_LoadStoryGraph(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonPath);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_LoadStoryGraphFromVfs(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string vfsPath);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastStoryGraphError(IntPtr handle);

        // ── Graph vNext chapter queries (CAPABILITY_GRAPH_VNEXT) ─────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetCurrentChapterIdUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetChapterCount(
            IntPtr handle,
            out uint outCount);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetChapterIdAtUtf8(
            IntPtr handle,
            uint index,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        // ── Faz 3 Dilim 1 runtime localization (CAPABILITY_LOCALIZATION) ────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_SetLocale(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string locale);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLocale(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetSupportedLocalesJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        // ── Faz 3 Dilim 2 rich-text markup (CAPABILITY_RICH_TEXT_MARKUP) ──
        //
        // Handle-free pure helpers: no engine instance, no thread affinity.
        // Fail-closed: null markup, oversized input (>256 KiB) and bad
        // caller buffers report InvalidArgument; undersized buffers report
        // BufferTooSmall with the required size.

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_ParseMarkup(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? markupUtf8,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_StripMarkup(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? markupUtf8,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        // ── Faz 3 Dilim 3 text shaping (CAPABILITY_TEXT_SHAPING) ────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_ShapeMarkup(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string markupUtf8,
            IntPtr fontData,
            uint fontDataSize,
            float fontSize,
            float maxWidth,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? languageUtf8,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        // ── Faz 2 player-loop read tracking (CAPABILITY_PLAYER_LOOP) ────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetActiveDialogueContentIdsJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetProjectDirectory(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string projectRoot);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetBgmTransitionDefaults(IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string transition, float durationSeconds);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_AdvanceNode(
            IntPtr handle,
            uint choiceIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_SelectChoice(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string optionId);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_PointerDown(IntPtr handle, float x, float y);

        /// <summary>
        /// Updates the scene from a JSON string containing component data.
        /// This is the component-based alternative to RowlEngine_UpdateScene.
        /// </summary>
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_UpdateSceneFromJson(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string componentsJson);

        // ── State queries ────────────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetSpeaker(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogue(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetCurrentNodeId(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetBackgroundRotation(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetBackgroundParallax(IntPtr handle, float parallaxX, float parallaxY);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetBackgroundParallaxX(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetBackgroundParallaxY(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetBackgroundOpacity(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetCharacterRotation(IntPtr handle);

        // ── Audio Control ───────────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_PlayAudio(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath,
            int channelType,
            int filterType);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_StopBgm(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetBgmVolume(IntPtr handle, float volume);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetMasterVolume(IntPtr handle, float volume);

        // B4 — simetrik tamamlama (C API'de zaten var, c_api_audio.cpp:114;
        // additive, ABI-etkisiz). Guard round-trip testleri için.
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetMasterVolume(IntPtr handle);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetVoiceVolume(IntPtr handle, float volume);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetSfxVolume(IntPtr handle, float volume);

        // ── Faz 5 Dilim 1 audio streaming (CAPABILITY_AUDIO_STREAMING) ────
        //
        // IsStreaming reports the live BGM routing decision (1 = stream,
        // 0 = memory / unknown / no-BGM / dead handle). GetStreamInfoJson
        // follows the caller-buffer contract (NULL/0 size query,
        // BUFFER_TOO_SMALL + required size, INVALID_HANDLE on dead
        // handles); the JSON schema is mode/duration_seconds/
        // threshold_seconds/threshold_bytes/buffered_seconds/reason/
        // channel/asset. Volume setters clamp to [0,1] and ignore
        // non-finite input; dead-handle getters return 0.0f.

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsStreaming(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetStreamInfoJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetBgmVolume(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetAmbienceVolume(IntPtr handle, float volume);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetAmbienceVolume(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetUiVolume(IntPtr handle, float volume);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetUiVolume(IntPtr handle);

        // ── Faz 5 Dilim 2 mixer + SFX polyphony + fade curves + ambience
        // beds + pump observability (CAPABILITY_AUDIO_MIXER_POLYPHONY) ────
        //
        // 1:1 Cdecl mirror of the 15 additive entries documented in
        // engine/include/rowl/c_api.h (capability 16384). FadeCurve: 0 =
        // Linear (default), 1 = EqualPower; setters ignore any other value.
        // Pool depth clamps to [1,16] (default 8). Ambience beds: 0 = BedA
        // (legacy), 1 = BedB; invalid bed is fail-closed. Crossfade
        // duration <= 0 (or non-finite) is an instant switch. JSON getters
        // follow the caller-buffer contract; pump stats are observability
        // only (no fail gate).

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetFadeCurve(IntPtr handle, int curve);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetFadeCurve(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetSfxPoolDepth(IntPtr handle, int depth);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetSfxPoolDepth(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetSfxActiveVoices(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetSfxActivePaths(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_PlayAmbienceBed(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath,
            int bed);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_StopAmbienceBed(IntPtr handle, int bed);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetAmbienceBedVolume(IntPtr handle, int bed, float volume);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetAmbienceBedVolume(IntPtr handle, int bed);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsAmbienceBedPlaying(IntPtr handle, int bed);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_CrossfadeAmbienceTo(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath,
            float durationSeconds,
            int curve);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsAmbienceCrossfadeActive(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetBgmPumpStatsJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetBgmPumpAvgMicroseconds(IntPtr handle);

        // ── Faz 5 Dilim 3 layered character slots + expression presets
        // (CAPABILITY_CHARACTER_LAYERS = 32768) ────
        //
        // 1:1 Cdecl mirror of the 11 additive entries documented in
        // engine/include/rowl/c_api.h. Slots: "body" < "face" < "outfit" <
        // "accessory" (fixed draw order, case-sensitive). Empty asset
        // clears the slot; opacity clamps to [0,1] (non-finite rejected);
        // nonzero visible = shown. Expression apply is atomic server-side
        // (any broken slot leaves every slot untouched + diagnosis via
        // GetLastCharacterErrorUtf8). String inputs are bounded (256 KiB
        // carrier); JSON outputs follow the caller-buffer contract (NULL/0
        // size query, undersized buffer clears + BufferTooSmall).

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_SetCharacterSlotAsset(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string slotName,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetCharacterSlotAssetUtf8(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string slotName,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_SetCharacterSlotOpacity(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string slotName,
            float opacity);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetCharacterSlotOpacity(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string slotName,
            out float outOpacity);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_SetCharacterSlotVisible(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string slotName,
            int visible);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsCharacterSlotVisible(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string slotName);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_RegisterCharacterPreset(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string presetName,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string expressionJsonUtf8);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_ApplyCharacterExpression(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string presetName);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetCharacterPresetListJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetCharacterDrawListJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLastCharacterErrorUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        // ── Faz 5 Dilim 4 budgeted asset prefetch + chapter-windowed loading
        // (CAPABILITY_PREFETCH_CHAPTERS = 65536) ────
        //
        // 1:1 Cdecl mirror of the 9 additive entries documented in
        // engine/include/rowl/c_api.h. Prefetch: active + next scene assets,
        // byte-budgeted (0 = 32 MiB default, clamped to 128 MiB) +
        // time-budgeted synchronous pump (<= 0/non-finite = ~4 ms, clamped to
        // 50 ms, no threads). Chapters: index (editor chapter_index.json
        // schema) + per-chapter files (editor LoadChapterFile schema); only
        // active +-1 stay resident. String inputs are bounded (256 KiB
        // carrier; 16 MiB for chapter files); JSON outputs follow the
        // caller-buffer contract (NULL/0 size query, undersized buffer clears
        // + BufferTooSmall).

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_LoadChapterIndexJson(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string indexJsonUtf8);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_AppendChapterFileJson(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string chapterJsonUtf8);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_LoadChapter(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string chapterIdUtf8);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_UnloadChapter(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string chapterIdUtf8);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLoadedChaptersJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsChapterBoundaryNode(
            IntPtr handle,
            ulong nodeId);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_PrefetchChapterAssets(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? chapterIdUtf8,
            ulong budgetBytes);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_PumpPrefetch(
            IntPtr handle,
            float maxMilliseconds);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetPrefetchProgressJson(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetTextSpeedMultiplier(IntPtr handle, float multiplier);        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetTextScale(IntPtr handle, float scale);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetHighContrast(IntPtr handle, int enabled);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetReducedMotion(IntPtr handle, int enabled);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetTextScale(IntPtr handle);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsHighContrast(IntPtr handle);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsReducedMotion(IntPtr handle);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetAutoAdvanceDelayOffset(IntPtr handle, float seconds);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_TriggerVoiceDucking(IntPtr handle, int isVoiceActive);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsBgmPlaying(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsVoicePlaying(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetActiveDspFilter(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastAudioError(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsAudioDeviceAvailable(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsAudioOutputSuspended(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetAudioChannelPeak(IntPtr handle, int channelType, int channelIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetAudioChannelRms(IntPtr handle, int channelType, int channelIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_GetAudioSpectrum(IntPtr handle, [Out] float[] outBands, int bandCount);

        // ── Typewriter Voice Blips & Audio Effects (Milestone 25) ─────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_PlayVoiceBlip(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? soundPath,
            float pitch,
            float volume,
            int channelType);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetDialogueVoiceBlip(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? soundPath,
            float basePitch,
            float pitchVariance,
            int cadence,
            int skipPunctuation,
            int channelType);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogueVoiceBlipSound(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetDialogueVoiceBlipPitch(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetDialogueVoiceBlipVariance(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetDialogueVoiceBlipCadence(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetDialogueVoiceBlipSkipPunctuation(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetDialogueVoiceBlipChannel(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetDialogueVoiceBlipVolume(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetDialogueVoiceBlipVolume(IntPtr handle, float volume);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint RowlEngine_GetVoiceBlipCount(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ResetVoiceBlipCount(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetScriptRuntimeDiagnosticsJson(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogueHistoryJson(IntPtr handle);

        // ── Save / Load Slots & History Rewind ────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_SaveGameSlot(IntPtr handle, int slotIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_SaveGameSlotResult(IntPtr handle, int slotIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_LoadGameSlot(IntPtr handle, int slotIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_LoadGameSlotResult(IntPtr handle, int slotIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_HasSaveSlot(IntPtr handle, int slotIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_DeleteSaveSlot(IntPtr handle, int slotIndex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_Rewind(IntPtr handle, uint steps);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetCurrentStepId(IntPtr handle);

        // ── Scripting & Variable Evaluation ───────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetVariable(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetVariable(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_EvaluateCondition(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string conditionExpr);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_ExecuteScript(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string scriptCode);

        // ── Structured Runtime Results & Diagnostics ──────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_GetLastResultCode(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastResultOperation(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastResultMessage(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastResultTarget(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ClearLastResult(IntPtr handle);

        // ── Camera & Transition Controls ─────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_StartTransition(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string kind,
            float durationSeconds,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? colorHex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsTransitionActive(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsPreviewFrameStatic(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetCamera(
            IntPtr handle,
            float x,
            float y,
            float zoom);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_TriggerCameraShake(
            IntPtr handle,
            float intensity,
            float durationSeconds);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ResetCamera(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_CameraPanTo(
            IntPtr handle,
            float targetX,
            float targetY,
            float durationSeconds,
            int easingType);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_CameraZoomTo(
            IntPtr handle,
            float targetZoom,
            float durationSeconds,
            int easingType);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsCameraMoving(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_TriggerCameraShakePreset(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string presetName,
            float intensityMultiplier,
            float durationSeconds);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_TriggerCameraShakeProfile(
            IntPtr handle,
            float intensity,
            float durationSeconds,
            float frequency,
            float damping,
            float dirX,
            float dirY);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetCameraShakeOffsetX(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetCameraShakeOffsetY(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_TriggerScreenFlash(
            IntPtr handle,
            byte r,
            byte g,
            byte b,
            float durationSeconds,
            float intensity);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_TriggerScreenFlashHex(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string colorHex,
            float durationSeconds,
            float intensity);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsScreenFlashActive(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetScreenTint(
            IntPtr handle,
            byte r,
            byte g,
            byte b,
            float opacity);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetScreenTintHex(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string colorHex,
            float opacity);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ClearScreenTint(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetScreenTintOpacity(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetVignette(
            IntPtr handle,
            float intensity,
            float radius,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? colorHex);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern float RowlEngine_GetVignetteIntensity(IntPtr handle);

        // ── Faz 5 Dilim 5 asset provenance (CAPABILITY_ASSET_PROVENANCE = 131072) ──
        //
        // 1:1 Cdecl mirror of the additive entry documented in
        // engine/include/rowl/c_api.h:
        // RowlEngine_GetAssetProvenanceJson(handle, assetPathUtf8, buffer,
        // bufferSize, outRequiredSize). Caller-buffer contract (NULL/0 size
        // query, undersized buffer clears + BufferTooSmall, missing sidecar
        // reports FileNotFound). Callers go through AssetProvenanceService,
        // which falls back to the on-disk <output>.rowlconv.json sidecar when
        // the export is absent.

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetAssetProvenanceJson(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPathUtf8,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        // ── B3a caller-buffer P/Invoke girişleri (14 adet) ──
        //
        // A5-tur3 + B2a + B2b'nin eklediği API'lerin 1:1 Cdecl aynaları
        // (engine/include/rowl/c_api.h). Ödünç-imzalı WithLength varyantları
        // durur; call-site göçü B5'indir — bu blok yalnızca eksik girişleri
        // kapatır (köprü %100'e). Sözleşme: NULL/0 size-query, undersized
        // clears + BufferTooSmall. Sayac okumaları değere-göre-dönüşlüdür
        // (ölü-handle'da 0; fail-closed skalerler).

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetDialogueHistoryJsonUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetVariableUtf8(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLastResultOperationUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLastResultMessageUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLastResultTargetUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetPauseMenuJsonUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLastStoryGraphErrorUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetSpeakerUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetDialogueUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetLastAudioErrorUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ResultCode RowlEngine_GetDialogueVoiceBlipSoundUtf8(
            IntPtr handle,
            IntPtr buffer,
            uint bufferSize,
            out uint outRequiredSize);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint RowlEngine_GetSynthBlipCount(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetAudioDropCount(IntPtr handle);

        // ── Helpers ──────────────────────────────────────────────────────────

        /// <summary>Converts a native C UTF-8 string pointer to a managed string safely.</summary>
        internal static string PtrToString(IntPtr ptr)
            => ptr == IntPtr.Zero ? string.Empty
                                  : Marshal.PtrToStringUTF8(ptr) ?? string.Empty;

        /// <summary>
        /// Length-aware variant for the MS-0 `...WithLength` getters. Copies exactly
        /// byteLength bytes, so the result never depends on NUL scanning.
        /// </summary>
        internal static string PtrToString(IntPtr ptr, uint byteLength)
            => ptr == IntPtr.Zero || byteLength == 0 ? string.Empty
                : Marshal.PtrToStringUTF8(ptr, (int)byteLength) ?? string.Empty;

        // ── MS-0 length-reporting string getters (see lifetime contract in c_api.h) ──

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetSpeakerWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogueWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastStoryGraphErrorWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastAudioErrorWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogueVoiceBlipSoundWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetScriptRuntimeDiagnosticsJsonWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogueHistoryJsonWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetVariableWithLength(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
            out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastResultOperationWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastResultMessageWithLength(IntPtr handle, out uint outLen);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetLastResultTargetWithLength(IntPtr handle, out uint outLen);
    }
}
