/**
 * EngineHost.cs
 *
 * High-level manager for the embedded C++ Engine lifetime.
 *
 * Responsibilities:
 *   - Owns the editor-only offscreen engine on a dedicated worker thread
 *   - Queues the Avalonia DispatcherTimer tick onto that owner thread (~60 FPS)
 *   - Copies the offscreen RGBA32 framebuffer into an Avalonia WriteableBitmap
 *   - Controls Play / Stop playback state and story resets
 */

using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using System;
using System.Buffers;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Collections.Generic;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Native
{
    public sealed class EngineHost : INotifyPropertyChanged, IDisposable
    {
        public event PropertyChangedEventHandler? PropertyChanged;
        public event Action? FrameUpdated;

        private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        // ── State ────────────────────────────────────────────────────────────

        private OffscreenRuntimeWorker? _runtime;
        private DispatcherTimer? _tickTimer;
        private DateTime _lastTick = DateTime.UtcNow;
        private string? _lastPreviewComponentsJson;

        /// <summary>True when Play mode is active (game loop running).</summary>
        public bool IsPlaying { get; private set; } = false;

        private WriteableBitmap? _renderTargetBitmap;
        public WriteableBitmap? RenderTargetBitmap
        {
            get => _renderTargetBitmap;
            private set
            {
                if (_renderTargetBitmap != value)
                {
                    _renderTargetBitmap = value;
                    OnPropertyChanged();
                }
            }
        }

        /// <summary>True while the engine is initialised and not requesting quit.</summary>
        public bool IsRunning => InvokeNative(
            handle => NativeBridge.RowlEngine_IsRunning(handle) != 0, false);

        /// <summary>True after a successful Initialize() call.</summary>
        public bool IsInitialized => _runtime?.IsAvailable == true;

        /// <summary>
        /// Raw native engine pointer retained for compatibility. Calls that use
        /// this pointer must still be dispatched through the offscreen worker.
        /// </summary>
        public IntPtr Handle => _runtime?.Handle ?? IntPtr.Zero;

        internal int RuntimeWorkerThreadId => _runtime?.ManagedThreadId ?? 0;

        // B5 — adapter'ın K-trio worker-wrapper'larına erişimi (TAŞINMA
        // KURALI: marshal API worker'da yaşar; host'a davranış eklenmez,
        // yalnızca erişim verilir — şişirme yasağına aykırı değil).
        internal OffscreenRuntimeWorker? Runtime => _runtime;

        // W8-f — Initialize başarısızlık detayı: üç sessiz-false dalı da
        // LastError'u doldurur, başarılı init boşaltır. Throw yok, imza
        // bool korunur. Yalnızca gözlemlenebilirliktir (dönüş semantiği
        // aynı). EmbeddedRuntimeBootstrap satır 134-159 emsaldir.
        internal string LastError { get; private set; } = string.Empty;

        // W8-f — test seam'i: varsayılan null iken gerçek worker kurulur;
        // testler deterministik başarısızlık dallarını (fırlatan fabrika,
        // handlesiz worker) bu fabrikayla enjekte eder. Üretim akışı
        // değişmez.
        internal Func<OffscreenRuntimeWorker>? RuntimeFactory { get; set; }

        private T InvokeNative<T>(Func<IntPtr, T> command, T fallback)
        {
            OffscreenRuntimeWorker? runtime = _runtime;
            return runtime?.IsAvailable == true ? runtime.Invoke(command) : fallback;
        }

        private void InvokeNative(Action<IntPtr> command)
        {
            OffscreenRuntimeWorker? runtime = _runtime;
            if (runtime?.IsAvailable == true)
                runtime.Invoke(command);
        }

        public ulong TextureCacheBudgetBytes => InvokeNative(
            NativeBridge.RowlEngine_GetTextureCacheBudgetBytes, 0UL);

        public ulong TextureCacheEvictionCount => InvokeNative(
            NativeBridge.RowlEngine_GetTextureCacheEvictionCount, 0UL);

        /// <summary>Texture decode and upload work from the latest rendered frame.</summary>
        public double LastFrameTextureLoadMilliseconds => InvokeNative(
            NativeBridge.RowlEngine_GetLastFrameTextureLoadMilliseconds, 0.0);

        /// <summary>Non-texture renderer work from the latest rendered frame.</summary>
        public double LastFrameNonTextureRenderMilliseconds => InvokeNative(
            NativeBridge.RowlEngine_GetLastFrameNonTextureRenderMilliseconds, 0.0);

        /// <summary>TrueType rasterization work from the latest rendered frame.</summary>
        public double LastFrameTextRasterizationMilliseconds => InvokeNative(
            NativeBridge.RowlEngine_GetLastFrameTextRasterizationMilliseconds, 0.0);

        /// <summary>SDL command flush work from the latest rendered frame.</summary>
        public double LastFrameRendererFlushMilliseconds => InvokeNative(
            NativeBridge.RowlEngine_GetLastFrameRendererFlushMilliseconds, 0.0);

        public IReadOnlyList<ScriptRuntimeDiagnostic> ScriptRuntimeDiagnostics { get; private set; }
            = Array.Empty<ScriptRuntimeDiagnostic>();

        public IReadOnlyList<DialogueHistoryEntry> DialogueHistory { get; private set; }
            = Array.Empty<DialogueHistoryEntry>();

        /// <summary>Managed/native work split for the most recent editor preview delivery.</summary>
        public double LastSceneUpdateMilliseconds { get; private set; }
        public double LastPreviewStepMilliseconds { get; private set; }
        public double LastPixelBufferCopyMilliseconds { get; private set; }

        /// <summary>
        /// Sets the decoded texture-cache ceiling for this runtime. Use a
        /// device-profile budget after Initialize; the native layer clamps
        /// values below 1 MiB and evicts least-recently-used textures safely.
        /// </summary>
        public void SetTextureCacheBudgetBytes(ulong bytes)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetTextureCacheBudgetBytes(handle, bytes));
        }

        // ── Initialisation ───────────────────────────────────────────────────

        /// <summary>
        /// Creates and initialises the engine in offscreen framebuffer mode.
        /// </summary>
        public bool Initialize(uint width = 1920, uint height = 1080, bool vsync = true)
        {
            if (IsInitialized)
                return true; // Already initialised

            try
            {
                _runtime = RuntimeFactory?.Invoke() ?? new OffscreenRuntimeWorker();
            }
            catch (InvalidOperationException ex)
            {
                _runtime = null;
                LastError = $"Engine init failed: offscreen runtime worker could not start ({ex.Message}).";
                return false;
            }
            if (!IsInitialized)
            {
                LastError = "Engine init failed: offscreen runtime worker started without a usable native handle.";
                Dispose();
                return false;
            }
            _lastPreviewComponentsJson = null;

            // Kanal Init ÖNCESİ temizlenir (SetProjectDirectoryChecked
            // emsali): sıfır-dışı her kod bu Init'e aittir, stale damga
            // taze sanılmaz. Anlık görüntü Dispose ÖNCESİ alınır.
            InvokeNative(handle => NativeBridge.RowlEngine_ClearLastResult(handle));

            int result = InvokeNative(handle => NativeBridge.RowlEngine_Init(
                handle, width, height, vsync ? 1 : 0), 0);

            if (result == 0)
            {
                // W8-g G5 Init-0 sentinel: kanal bos-sentinel (0/""/"")
                // donerse bunu neden gibi yazma — bos kanali adlandir.
                if (TryGetLastEngineResult(out int code, out string operation, out string message)
                    && (code != 0 || !string.IsNullOrEmpty(operation) || !string.IsNullOrEmpty(message)))
                    LastError = $"Engine init failed: RowlEngine_Init returned 0 (code {code}, operation '{operation}', message '{message}').";
                else
                    LastError = "Engine init failed: RowlEngine_Init returned 0 (last-result channel empty — no native cause captured).";
                Dispose();
                return false;
            }

            // Initial static frame render
            InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
            UpdatePixelBuffer();

            StartTickTimer();
            LastError = string.Empty;
            return true;
        }

        /// <summary>
        /// Backward compatibility method for view components.
        /// W8-g G5: bogus-nonzero fail-open kapandi — Zero offscreen
        /// fallback'a duser (D01 legacy pin); nonzero fail-closed'dur
        /// (false + LastError, worker yok). Gercek gomme-yolu
        /// EmbeddedRuntimeBootstrap'tadir; bu imzanin uretim cagirani yok.
        /// </summary>
        public bool InitializeEmbedded(IntPtr nativeWindowHandle, uint width, uint height, bool vsync = true)
        {
            if (nativeWindowHandle != IntPtr.Zero)
            {
                LastError = "Embedded init failed: nonzero native window handle is not claimed by this host (no offscreen fallback; embed via EmbeddedRuntimeBootstrap).";
                return false;
            }
            return Initialize(width, height, vsync);
        }

        // ── Tick loop ────────────────────────────────────────────────────────

        private void StartTickTimer()
        {
            _lastTick = DateTime.UtcNow;
            _lastIdleUpkeep = _lastTick;
            _tickTimer = new DispatcherTimer(DispatcherPriority.Render)
            {
                Interval = TimeSpan.FromMilliseconds(16) // ~60 FPS
            };
            _tickTimer.Tick += OnTick;
            _tickTimer.Start();
        }

        public float MasterPeakL { get; private set; }
        public float MasterPeakR { get; private set; }
        public float MasterRmsL { get; private set; }
        public float MasterRmsR { get; private set; }

        public event Action<float, float, float, float>? AudioTelemetryPolled;

        private ulong _lastDialogueStepId = ulong.MaxValue;
        private bool _dialogueHistoryDirty = true;
        private float _telemetryAccumulator;

        // ── MS-4 dirty-frame & idle-diet state ──────────────────────────────
        // Pause upkeep cadence: device events, audio suspension and quit
        // polling stay alive while the 60 Hz Step(0)+render loop sleeps.
        private const double IdleUpkeepIntervalMilliseconds = 500.0;
        private DateTime _lastIdleUpkeep = DateTime.UtcNow;
        private ulong _lastCopiedStepId = ulong.MaxValue;
        private bool _lastTickFrameActive = true;

        /// <summary>Pixel copies skipped because the frame was provably static.</summary>
        public ulong SkippedPixelBufferCopies { get; private set; }
        /// <summary>Pixel-buffer copy attempts that threw (see debug log).</summary>
        public ulong PixelBufferCopyErrorCount { get; private set; }
        /// <summary>Script-diagnostics JSON parses that failed (see debug log).</summary>
        public ulong DiagnosticsParseErrorCount { get; private set; }
        /// <summary>Dialogue-history JSON parses that failed (see debug log).</summary>
        public ulong DialogueHistoryParseErrorCount { get; private set; }
        /// <summary>Active content-id JSON parses that failed (see debug log).</summary>
        public ulong ActiveContentIdsParseErrorCount { get; private set; }
        /// <summary>Slot metadata JSON parses that failed (see debug log).</summary>
        public ulong SlotMetadataParseErrorCount { get; private set; }
        /// <summary>Dispatcher-tick faults swallowed by the last-resort guard (see debug log).</summary>
        public ulong TickErrorCount { get; private set; }

        /// <summary>
        /// Pure copy-gate decision behind the dirty-frame optimization, kept
        /// static so the headless suite can pin its truth table without a
        /// native handle. Copies when the frame is active, when activity just
        /// ended (settle the final frame), or when the story step advanced.
        /// </summary>
        internal static bool ShouldCopyFrame(
            bool frameActive, bool lastTickFrameActive, ulong stepId, ulong lastCopiedStepId)
        {
            if (frameActive) return true;
            if (lastTickFrameActive) return true;
            return stepId != lastCopiedStepId;
        }

        /// <summary>MS-4 native dirty-frame query (true = copy can be skipped).</summary>
        public bool IsPreviewFrameStatic()
            => InvokeNative(handle => NativeBridge.RowlEngine_IsPreviewFrameStatic(handle) != 0, false);

        /// <summary>
        /// Marks the cached dialogue history stale. Called by state-changing
        /// operations (advance, choice, load, reset) so the next tick re-pulls.
        /// </summary>
        public void InvalidateDialogueHistory() => _dialogueHistoryDirty = true;

        private void OnTick(object? sender, EventArgs e)
        {
            if (!IsInitialized) return;

            try
            {
                TickCore();
            }
            catch (Exception ex)
            {
                // Son kale: 60 Hz dispatcher threadi asla fırlatmamalı.
                TickErrorCount++;
                Debug.WriteLine($"EngineHost tick failed ({TickErrorCount}): {ex.Message}");
            }
        }

        private void TickCore()
        {
            var now = DateTime.UtcNow;
            float dt = (float)(now - _lastTick).TotalSeconds;
            _lastTick = now;

            // Clamp: prevent spiral of death after sleep/pause
            if (dt > 0.25f) dt = 0.25f;
            if (dt < 0.0f)  dt = 0.0f;

            if (IsPlaying)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, dt));
                RefreshDialogueHistoryIfStale();
                CopyPixelBufferIfDirty();
            }
            else if ((now - _lastIdleUpkeep).TotalMilliseconds >= IdleUpkeepIntervalMilliseconds)
            {
                // MS-4 idle diet: the paused 60 Hz Step(0)+render loop is
                // asleep. A 2 Hz upkeep keeps SDL quit polling, audio-device
                // hotplug and suspension alive; the refresh below is a safety
                // net for setters that mutate without an explicit copy.
                // Mutating entry points (scene/story/choice/viewport) already
                // copy synchronously, so interaction latency is unchanged.
                double idleSeconds = (now - _lastIdleUpkeep).TotalSeconds;
                _lastIdleUpkeep = now;
                // #160: the upkeep drives the visual clock with the real idle
                // delta (clamped like the play loop) so in-flight camera
                // tweens progress instead of stalling on Step(0). Paused story
                // simulation stays frozen (engine.cpp paused branch), so this
                // is story-safe.
                float upkeepDt = (float)Math.Clamp(idleSeconds, 0.0, 0.25);
                // Pre-clear so the read below belongs to this upkeep; Step
                // never stamps success, so non-zero is always fresh evidence
                // (e.g. the #160 stall StateError).
                InvokeNative(handle => NativeBridge.RowlEngine_ClearLastResult(handle));
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, upkeepDt));
                if (TryGetLastEngineResult(out int upkeepCode, out string upkeepOp, out string upkeepMsg)
                    && upkeepCode != 0)
                    Debug.WriteLine($"EngineHost idle upkeep last-result {upkeepCode} ({upkeepOp}): {upkeepMsg}");
                UpdatePixelBuffer();
            }

            // MS-2: audio meters at 10 Hz, not per-frame. Four P/Invokes per
            // tick at 60 FPS was pure overhead for a ~100 ms human display.
            _telemetryAccumulator += dt;
            if (_telemetryAccumulator >= 0.1f)
            {
                _telemetryAccumulator = 0.0f;
                PollAudioTelemetry();
            }
        }

        /// <summary>
        /// Pulls dialogue history only when the runtime step advanced or an
        /// explicit invalidation happened. Skips the per-tick JSON
        /// deserialize that used to run 60×/second on an unchanged backlog.
        /// </summary>
        private void RefreshDialogueHistoryIfStale()
        {
            ulong stepId = InvokeNative(NativeBridge.RowlEngine_GetCurrentStepId, 0UL);
            if (!_dialogueHistoryDirty && stepId == _lastDialogueStepId)
                return;
            _lastDialogueStepId = stepId;
            _dialogueHistoryDirty = false;
            RefreshDialogueHistory();
        }

        /// <summary>
        /// MS-4 dirty-frame gate: copies the ~8.3 MB frame only when the
        /// native static query, the activity latch, or the story step says the
        /// presented pixels may be stale. Gameplay stepping is untouched — only
        /// the copy is gated, so a conservative native "not static" can only
        /// cost a copy, never a frozen frame.
        /// </summary>
        private void CopyPixelBufferIfDirty()
        {
            ulong stepId = InvokeNative(NativeBridge.RowlEngine_GetCurrentStepId, 0UL);
            bool frameActive = !IsPreviewFrameStatic();
            bool shouldCopy = ShouldCopyFrame(frameActive, _lastTickFrameActive, stepId, _lastCopiedStepId);
            _lastTickFrameActive = frameActive;
            if (!shouldCopy)
            {
                SkippedPixelBufferCopies++;
                return;
            }
            UpdatePixelBuffer();
            _lastCopiedStepId = stepId;
        }

        private void PollAudioTelemetry()
        {
            if (!IsInitialized) return;
            (MasterPeakL, MasterPeakR, MasterRmsL, MasterRmsR) = InvokeNative(handle => (
                NativeBridge.RowlEngine_GetAudioChannelPeak(handle, 3, 0),
                NativeBridge.RowlEngine_GetAudioChannelPeak(handle, 3, 1),
                NativeBridge.RowlEngine_GetAudioChannelRms(handle, 3, 0),
                NativeBridge.RowlEngine_GetAudioChannelRms(handle, 3, 1)),
                (0.0f, 0.0f, 0.0f, 0.0f));

            AudioTelemetryPolled?.Invoke(MasterPeakL, MasterPeakR, MasterRmsL, MasterRmsR);
        }

        /// <summary>Frames skipped because the native pitch was unusable (see MS-0 contract).</summary>
        public ulong PixelBufferPitchMismatchCount { get; private set; }

        private void UpdatePixelBuffer()
        {
            if (!IsInitialized) return;

            var stopwatch = Stopwatch.StartNew();
            byte[]? rentedPixels = null;
            try
            {
                var snapshot = InvokeNative(handle =>
                {
                    IntPtr pixelPtr = NativeBridge.RowlEngine_GetPixelBufferEx(
                        handle, out uint width, out uint height, out uint pitch);
                    if (pixelPtr == IntPtr.Zero || width == 0 || height == 0)
                        return (Pixels: (byte[]?)null, Width: 0u, Height: 0u, PitchMismatch: false);

                    uint tightRowBytes = checked(width * 4);
                    if (pitch < tightRowBytes)
                        return (Pixels: (byte[]?)null, Width: width, Height: height, PitchMismatch: true);

                    int rowLength = checked((int)tightRowBytes);
                    byte[] pixels = ArrayPool<byte>.Shared.Rent(checked(rowLength * (int)height));
                    rentedPixels = pixels;
                    for (uint y = 0; y < height; y++)
                    {
                        IntPtr row = IntPtr.Add(pixelPtr, checked((int)(y * pitch)));
                        Marshal.Copy(row, pixels, checked((int)y * rowLength), rowLength);
                    }
                    return (Pixels: (byte[]?)pixels, Width: width, Height: height, PitchMismatch: false);
                }, (Pixels: (byte[]?)null, Width: 0u, Height: 0u, PitchMismatch: false));

                if (snapshot.PitchMismatch)
                {
                    PixelBufferPitchMismatchCount++;
                    return;
                }

                if (snapshot.Pixels != null)
                {
                    int width = checked((int)snapshot.Width);
                    int height = checked((int)snapshot.Height);

                    if (RenderTargetBitmap == null ||
                        RenderTargetBitmap.PixelSize.Width != width ||
                        RenderTargetBitmap.PixelSize.Height != height)
                    {
                        var previousBitmap = RenderTargetBitmap;
                        RenderTargetBitmap = new WriteableBitmap(
                            new PixelSize(width, height),
                            new Vector(96, 96),
                            PixelFormat.Rgba8888,
                            AlphaFormat.Opaque);
                        previousBitmap?.Dispose();
                    }

                    using (var buf = RenderTargetBitmap.Lock())
                    {
                        int tightRowBytes = checked(width * 4);
                        int copyRowBytes = Math.Min(tightRowBytes, buf.RowBytes);
                        for (int y = 0; y < height; y++)
                        {
                            Marshal.Copy(
                                snapshot.Pixels,
                                y * tightRowBytes,
                                IntPtr.Add(buf.Address, y * buf.RowBytes),
                                copyRowBytes);
                        }
                    }
                    OnPropertyChanged(nameof(RenderTargetBitmap));
                    FrameUpdated?.Invoke();
                }
            }
            catch (Exception ex)
            {
                // MS-4: counted instead of silent — headless runs without an
                // Avalonia render interface land here; real failures stay
                // visible in the debug log with a running total.
                PixelBufferCopyErrorCount++;
                Debug.WriteLine($"EngineHost pixel buffer copy failed ({PixelBufferCopyErrorCount}): {ex.Message}");
            }
            finally
            {
                if (rentedPixels != null)
                    ArrayPool<byte>.Shared.Return(rentedPixels);
                stopwatch.Stop();
                LastPixelBufferCopyMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
            }
        }

        // ── Playback & Engine State Control ──────────────────────────────────

        /// <summary>Sets the C++ engine playback state (true = Play, false = Stop/Pause).</summary>
        public void SetPlayState(bool isPlaying)
        {
            if (!IsInitialized) return;
            if (IsPlaying == isPlaying) return;
            IsPlaying = isPlaying;
            _lastTick = DateTime.UtcNow;
            _lastIdleUpkeep = _lastTick;

            InvokeNative(handle => NativeBridge.RowlEngine_SetPlayState(handle, isPlaying ? 1 : 0));
            if (!isPlaying)
            {
                // Render static frame when stopping
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
            OnPropertyChanged(nameof(IsPlaying));
        }

        /// <summary>Freezes (or resumes) story simulation for player pause menus.</summary>
        public void SetPaused(bool paused)
        {
            if (IsInitialized)
                InvokeNative(handle => NativeBridge.RowlEngine_SetPaused(handle, paused ? 1 : 0));
        }

        /// <summary>Reports the native pause flag (false when uninitialized).</summary>
        public bool IsPaused()
            => InvokeNative(NativeBridge.RowlEngine_IsPaused, 0) != 0;

        /// <summary>Resets the C++ engine story state back to the starting node.</summary>
        public void ResetToStartNode()
        {
            if (IsInitialized)
            {
                _lastPreviewComponentsJson = null;
                InvokeNative(handle =>
                {
                    NativeBridge.RowlEngine_ResetToStartNode(handle);
                    NativeBridge.RowlEngine_Step(handle, 0.0f);
                });
                InvalidateDialogueHistory();
                UpdatePixelBuffer();
            }
        }

        /// <summary>Advances engine simulation and rendering by the specified delta time.</summary>
        public void Step(float dt = 0.0f)
        {
            if (!IsInitialized) return;
            InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, dt));
            UpdatePixelBuffer();
        }

        // ── Scene / story control ────────────────────────────────────────────

        /// <summary>
        /// Pushes a complete visual novel scene update to the engine.
        /// The in-process call is serialized on the offscreen owner thread.
        /// </summary>
        public void UpdateScene(
            string speaker,   string dialogue,  string background,
            float  bgX,       float  bgY,       float  bgW,       float  bgH,
            string character,
            float  charX,     float  charY,     float  charW,     float  charH,
            float  dlgX,      float  dlgY,      float  dlgW,      float  dlgH)
        {
            if (!IsInitialized) return;

            InvokeNative(handle => NativeBridge.RowlEngine_UpdateScene(
                handle, speaker ?? "", dialogue ?? "", background ?? "",
                bgX, bgY, bgW, bgH, character ?? "",
                charX, charY, charW, charH, dlgX, dlgY, dlgW, dlgH));

            if (!IsPlaying)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
        }

        /// <summary>
        /// Extended legacy scene update with rotation angles for background and character.
        /// </summary>
        public void UpdateSceneEx(
            string speaker,   string dialogue,  string background,
            float  bgX,       float  bgY,       float  bgW,       float  bgH,       float bgRot,
            string character,
            float  charX,     float  charY,     float  charW,     float  charH,     float charRot,
            float  dlgX,      float  dlgY,      float  dlgW,      float  dlgH)
        {
            if (!IsInitialized) return;

            InvokeNative(handle => NativeBridge.RowlEngine_UpdateSceneEx(
                handle, speaker ?? "", dialogue ?? "", background ?? "",
                bgX, bgY, bgW, bgH, bgRot, character ?? "",
                charX, charY, charW, charH, charRot, dlgX, dlgY, dlgW, dlgH));

            if (!IsPlaying)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
        }

        /// <summary>
        /// Pushes component-based scene data to the engine as a JSON string.
        /// This is the component-aware alternative to UpdateScene.
        /// </summary>
        public bool UpdateSceneFromComponents(string componentsJson, bool skipIfUnchanged = false)
        {
            if (!IsInitialized || string.IsNullOrEmpty(componentsJson)) return false;
            if (skipIfUnchanged && !IsPlaying && string.Equals(_lastPreviewComponentsJson, componentsJson, StringComparison.Ordinal))
                return false;

            var stopwatch = Stopwatch.StartNew();
            InvokeNative(handle => NativeBridge.RowlEngine_UpdateSceneFromJson(handle, componentsJson));
            stopwatch.Stop();
            LastSceneUpdateMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
            _lastPreviewComponentsJson = componentsJson;
            RefreshScriptRuntimeDiagnostics();

            if (!IsPlaying)
            {
                stopwatch.Restart();
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                stopwatch.Stop();
                LastPreviewStepMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
                UpdatePixelBuffer();
            }
            return true;
        }

        public void RefreshScriptRuntimeDiagnostics()
        {
            if (!IsInitialized) return;
            try
            {
                string json = InvokeNative(handle => NativeBridge.PtrToString(
                    NativeBridge.RowlEngine_GetScriptRuntimeDiagnosticsJsonWithLength(handle, out uint diagLen), diagLen), string.Empty);
                ScriptRuntimeDiagnostics = JsonSerializer.Deserialize<List<ScriptRuntimeDiagnostic>>(json)
                    ?? new List<ScriptRuntimeDiagnostic>();
                OnPropertyChanged(nameof(ScriptRuntimeDiagnostics));
            }
            catch (Exception ex)
            {
                // MS-4: counted instead of silent. Json-dışı hatalar
                // (yerel/marshal/worker) da 60 Hz yolunda yakalanır.
                DiagnosticsParseErrorCount++;
                Debug.WriteLine($"EngineHost script diagnostics parse failed ({DiagnosticsParseErrorCount}): {ex.Message}");
                ScriptRuntimeDiagnostics = Array.Empty<ScriptRuntimeDiagnostic>();
            }
        }

        public void RefreshDialogueHistory()
        {
            if (!IsInitialized) return;
            try
            {
                string json = InvokeNative(handle => NativeBridge.PtrToString(
                    NativeBridge.RowlEngine_GetDialogueHistoryJsonWithLength(handle, out uint histLen), histLen), string.Empty);
                DialogueHistory = JsonSerializer.Deserialize<List<DialogueHistoryEntry>>(json)
                    ?? new List<DialogueHistoryEntry>();
                OnPropertyChanged(nameof(DialogueHistory));
            }
            catch (Exception ex)
            {
                // MS-4: counted instead of silent. Json-dışı hatalar
                // (yerel/marshal/worker) da 60 Hz yolunda yakalanır.
                DialogueHistoryParseErrorCount++;
                Debug.WriteLine($"EngineHost dialogue history parse failed ({DialogueHistoryParseErrorCount}): {ex.Message}");
                DialogueHistory = Array.Empty<DialogueHistoryEntry>();
            }
        }

        /// <summary>Loads (or reloads) a story graph JSON file into the engine.</summary>
        public void LoadStoryGraph(string jsonPath)
        {
            if (IsInitialized && !string.IsNullOrEmpty(jsonPath))
            {
                InvokeNative(handle => NativeBridge.RowlEngine_LoadStoryGraph(handle, jsonPath));
                if (!IsPlaying)
                {
                    InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                    UpdatePixelBuffer();
                }
            }
        }

        /// <summary>Loads a graph from the active project's VFS/package.</summary>
        public bool LoadStoryGraphFromVfs(string vfsPath)
        {
            if (!IsInitialized || string.IsNullOrEmpty(vfsPath)) return false;
            bool loaded = InvokeNative(
                handle => NativeBridge.RowlEngine_LoadStoryGraphFromVfs(handle, vfsPath) != 0, false);
            if (loaded && !IsPlaying)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
            return loaded;
        }

        public string LastStoryGraphError => InvokeNative(handle => NativeBridge.PtrToString(
            NativeBridge.RowlEngine_GetLastStoryGraphErrorWithLength(handle, out uint sgLen), sgLen), string.Empty);

        /// <summary>Sets the active project root directory, isolating VFS mounts to that project.</summary>
        public void SetProjectDirectory(string projectRoot)
        {
            if (IsInitialized && !string.IsNullOrEmpty(projectRoot))
            {
                _lastPreviewComponentsJson = null;
                InvokeNative(handle => NativeBridge.RowlEngine_SetProjectDirectory(handle, projectRoot));
                if (!IsPlaying)
                {
                    InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                    UpdatePixelBuffer();
                }
            }
        }

        /// <summary>
        /// Reads the native last-result channel (code + operation + message).
        /// Callers must clear the channel with NativeBridge.RowlEngine_ClearLastResult
        /// <i>before</i> the native call under test; otherwise a stale result from
        /// an earlier failure can be misread as fresh evidence.
        /// </summary>
        public bool TryGetLastEngineResult(out int code, out string operation, out string message)
        {
            code = 0;
            operation = string.Empty;
            message = string.Empty;
            if (!IsInitialized) return false;
            code = InvokeNative(handle => NativeBridge.RowlEngine_GetLastResultCode(handle), 0);
            operation = InvokeNative(handle => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetLastResultOperationWithLength(handle, out uint opLen), opLen), string.Empty);
            message = InvokeNative(handle => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetLastResultMessageWithLength(handle, out uint msgLen), msgLen), string.Empty);
            return true;
        }

        /// <summary>
        /// Ends the native session without replay (cursor to start, step/history
        /// reset, Lua quarantine cleared) so the next SetProjectDirectory is
        /// accepted as a fresh mount. No-op on an uninitialized engine.
        /// </summary>
        public void EndSession()
        {
            if (!IsInitialized) return;
            InvokeNative(handle => NativeBridge.RowlEngine_EndSession(handle));
        }

        /// <summary>
        /// Mounts the project like <see cref="SetProjectDirectory"/>, but reports
        /// the native rejection instead of swallowing it. Pre-clears the
        /// last-result channel before the mount, then rejects on
        /// (operation "set_project_directory" + non-zero code) so both refusal
        /// modes — StateError (mid-session) and IoError (unwritable save dir) —
        /// are detected. Returns false when the mount was rejected.
        /// </summary>
        public bool SetProjectDirectoryChecked(string projectRoot, out string detail)
        {
            detail = string.Empty;
            if (!IsInitialized || string.IsNullOrEmpty(projectRoot)) return true;
            _lastPreviewComponentsJson = null;
            InvokeNative(handle => NativeBridge.RowlEngine_ClearLastResult(handle));
            InvokeNative(handle => NativeBridge.RowlEngine_SetProjectDirectory(handle, projectRoot));
            // Kanal mount öncesi temizlendi; sıfır-dışı her kod bu mounta
            // aittir (ölü-kök reddi load_story_graph op'uyla damgalanır).
            // Anlık görüntü iç Step'ten ÖNCE alınır: Step WrongThread
            // damgası yanlış reje yol açardı. Detay da burada yakalanır.
            bool rejected = TryGetLastEngineResult(out int code, out _, out string message)
                && code != 0;
            if (rejected)
                detail = message;
            if (!IsPlaying)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
            return !rejected;
        }

        public void SetBgmTransitionDefaults(string transition, float durationSeconds)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetBgmTransitionDefaults(handle, transition, durationSeconds));
        }

        /// <summary>Forces an immediate single-step render and pixel buffer refresh (zero-latency UI update).</summary>
        public void ForceRenderFrame()
        {
            if (IsInitialized)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
        }

        /// <summary>Advances the story to the next node on the given branch.</summary>
        public void AdvanceNode(uint choiceIndex = 0)
        {
            if (IsInitialized)
            {
                InvokeNative(handle =>
                {
                    NativeBridge.RowlEngine_AdvanceNode(handle, choiceIndex);
                    NativeBridge.RowlEngine_Step(handle, 0.0f);
                });
                InvalidateDialogueHistory();
                UpdatePixelBuffer();
            }
        }

        /// <summary>
        /// Faz 2 player-loop advance: snapshots the presented content ids,
        /// advances, marks the departed line read in <paramref name="loop"/>,
        /// and atomically persists the profile. Plain <see cref="AdvanceNode"/>
        /// stays preview-side and untracked. Returns a save error or null.
        /// </summary>
        public string? AdvancePlayerLoop(PlayerLoopService loop, uint choiceIndex = 0)
        {
            ArgumentNullException.ThrowIfNull(loop);
            if (!IsInitialized)
                return "Engine is not initialized; player-loop advance aborted.";
            return loop.AdvanceAndTrack(() => GetActiveDialogueContentIds(), AdvanceNode, choiceIndex);
        }

        /// <summary>
        /// Faz 2 single skip step: evaluates the profile skip gate against the
        /// current presentation and advances once when allowed. The continuous
        /// auto-skip driver belongs to the Playing-state loop; this is the
        /// step it will call per tick. Returns true when an advance happened.
        /// </summary>
        public bool TrySkipPlayerLoopStep(
            PlayerLoopService loop, Func<bool> hasChoices, out string? saveError)
        {
            ArgumentNullException.ThrowIfNull(loop);
            ArgumentNullException.ThrowIfNull(hasChoices);
            saveError = null;
            if (!IsInitialized)
                return false;
            return loop.TrySkipStep(() => GetActiveDialogueContentIds(), hasChoices, AdvanceNode, out saveError);
        }

        /// <summary>How many choice buttons await manual input (0 = none).</summary>
        public uint GetChoiceCount()
            => InvokeNative(NativeBridge.RowlEngine_GetChoiceCount, 0u);

        /// <summary>True while at least one choice button awaits manual input.</summary>
        public bool HasChoices() => GetChoiceCount() > 0;

        /// <summary>
        /// Resolves a presented choice by stable option id (tracked like an
        /// advance by player-loop callers). Returns false when uninitialized
        /// or when the engine rejects the id.
        /// </summary>
        public bool SelectChoice(string optionId)
        {
            if (!IsInitialized || string.IsNullOrWhiteSpace(optionId)) return false;
            bool ok = InvokeNative(
                handle => NativeBridge.RowlEngine_SelectChoice(handle, optionId) != 0, false);
            if (ok)
            {
                InvalidateDialogueHistory();
                UpdatePixelBuffer();
            }
            return ok;
        }

        /// <summary>Stable option ids parallel to <see cref="GetChoiceLabels"/>.</summary>
        public IReadOnlyList<string> GetChoiceOptionIds()
        {
            var ids = new List<string>();
            if (!IsInitialized) return ids;
            uint count = GetChoiceCount();
            for (uint index = 0; index < count; index++)
            {
                uint current = index;
                string id = InvokeNative(handle =>
                {
                    if (NativeBridge.RowlEngine_GetChoiceOptionIdAtUtf8(
                            handle, current, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
                        required == 0)
                        return string.Empty;
                    IntPtr buffer = Marshal.AllocHGlobal((int)required);
                    try
                    {
                        if (NativeBridge.RowlEngine_GetChoiceOptionIdAtUtf8(
                                handle, current, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                            return string.Empty;
                        return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }, string.Empty);
                ids.Add(id);
            }
            return ids;
        }

        /// <summary>
        /// Presented choice button labels for selection UI (empty when
        /// uninitialized). Follows the caller-buffer contract per label.
        /// </summary>
        public IReadOnlyList<string> GetChoiceLabels()
        {
            var labels = new List<string>();
            if (!IsInitialized) return labels;
            uint count = GetChoiceCount();
            for (uint index = 0; index < count; index++)
            {
                uint current = index;
                string label = InvokeNative(handle =>
                {
                    if (NativeBridge.RowlEngine_GetChoiceLabelAtUtf8(
                            handle, current, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
                        required == 0)
                        return string.Empty;
                    IntPtr buffer = Marshal.AllocHGlobal((int)required);
                    try
                    {
                        if (NativeBridge.RowlEngine_GetChoiceLabelAtUtf8(
                                handle, current, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                            return string.Empty;
                        return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }, string.Empty);
                labels.Add(label);
            }
            return labels;
        }

        /// <summary>
        /// Content ids of the currently presented dialogues (Faz 2 read
        /// tracking). Empty when uninitialized or unparsable; legacy lines
        /// contribute "" and must never count as read.
        /// </summary>
        public IReadOnlyList<string> GetActiveDialogueContentIds()
        {
            if (!IsInitialized) return Array.Empty<string>();
            try
            {
                return InvokeNative(handle =>
                {
                    if (NativeBridge.RowlEngine_GetActiveDialogueContentIdsJson(
                            handle, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
                        required == 0)
                        return new List<string>();
                    IntPtr buffer = Marshal.AllocHGlobal((int)required);
                    try
                    {
                        if (NativeBridge.RowlEngine_GetActiveDialogueContentIdsJson(
                                handle, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                            return new List<string>();
                        string json = Marshal.PtrToStringUTF8(buffer) ?? "[]";
                        return JsonSerializer.Deserialize<List<string>>(json) ?? new List<string>();
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }, new List<string>());
            }
            catch (JsonException ex)
            {
                // Counted instead of silent (MS-4 rule).
                ActiveContentIdsParseErrorCount++;
                Debug.WriteLine($"EngineHost active content ids parse failed ({ActiveContentIdsParseErrorCount}): {ex.Message}");
                return Array.Empty<string>();
            }
        }

        public bool PointerDown(float virtualX, float virtualY)
        {
            if (!IsInitialized) return false;
            bool consumed = InvokeNative(handle =>
            {
                bool handled = NativeBridge.RowlEngine_PointerDown(handle, virtualX, virtualY) != 0;
                NativeBridge.RowlEngine_Step(handle, 0.0f);
                return handled;
            }, false);
            UpdatePixelBuffer();
            return consumed;
        }

        // ── Viewport control ─────────────────────────────────────────────────

        /// <summary>Notifies the engine that the render area was resized.</summary>
        public void ResizeViewport(uint newWidth, uint newHeight)
        {
            if (!IsInitialized) return;
            InvokeNative(handle => NativeBridge.RowlEngine_ResizeViewport(handle, newWidth, newHeight));
            // MS-4: paused ticks no longer copy every frame, so refresh
            // synchronously here; while playing the next tick covers it.
            if (!IsPlaying)
            {
                InvokeNative(handle => NativeBridge.RowlEngine_Step(handle, 0.0f));
                UpdatePixelBuffer();
            }
        }

        // ── State queries ─────────────────────────────────────────────────────

        public string GetSpeaker()
            => InvokeNative(handle => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetSpeakerWithLength(handle, out uint spkLen), spkLen), string.Empty);

        public string GetDialogue()
            => InvokeNative(handle => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetDialogueWithLength(handle, out uint dlgLen), dlgLen), string.Empty);

        public ulong GetCurrentNodeId()
            => InvokeNative(NativeBridge.RowlEngine_GetCurrentNodeId, 0UL);

        public float GetBackgroundRotation()
            => InvokeNative(NativeBridge.RowlEngine_GetBackgroundRotation, 0.0f);

        public void SetBackgroundParallax(float parallaxX, float parallaxY)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetBackgroundParallax(handle, parallaxX, parallaxY));
        }

        public float GetBackgroundParallaxX()
            => InvokeNative(NativeBridge.RowlEngine_GetBackgroundParallaxX, 1.0f);

        public float GetBackgroundParallaxY()
            => InvokeNative(NativeBridge.RowlEngine_GetBackgroundParallaxY, 1.0f);

        public float GetBackgroundOpacity()
            => InvokeNative(NativeBridge.RowlEngine_GetBackgroundOpacity, 1.0f);

        public float GetCharacterRotation()
            => InvokeNative(NativeBridge.RowlEngine_GetCharacterRotation, 0.0f);

        public bool IsBgmPlaying
            => InvokeNative(handle => NativeBridge.RowlEngine_IsBgmPlaying(handle) != 0, false);

        public bool IsVoicePlaying
            => InvokeNative(handle => NativeBridge.RowlEngine_IsVoicePlaying(handle) != 0, false);

        public int ActiveDspFilter
            => InvokeNative(NativeBridge.RowlEngine_GetActiveDspFilter, 0);

        public string LastAudioError
            => InvokeNative(handle => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetLastAudioErrorWithLength(handle, out uint audLen), audLen), string.Empty);

        public bool IsAudioDeviceAvailable
            => InvokeNative(handle => NativeBridge.RowlEngine_IsAudioDeviceAvailable(handle) != 0, false);

        public bool IsAudioOutputSuspended
            => InvokeNative(handle => NativeBridge.RowlEngine_IsAudioOutputSuspended(handle) != 0, false);

        // B4 — NativeGuard öndoğrulaması: non-finite forward edilmez
        // (NaN std::clamp'ten sızar, engine.cpp:1777-1782), finite [0,1].
        // Native'e sıfır dokunuş (D3 yasağı); yeni public API/alan yok.
        public void SetMasterVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetMasterVolume(handle, v));
        }

        public float GetMasterVolume()
            => InvokeNative(NativeBridge.RowlEngine_GetMasterVolume, 1.0f);

        public void SetBgmVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetBgmVolume(handle, v));
        }

        public void SetVoiceVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetVoiceVolume(handle, v));
        }

        public void SetSfxVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetSfxVolume(handle, v));
        }

        public void SetTextSpeedMultiplier(float multiplier)
        {
            if (!NativeGuard.TryClamp(multiplier, 0.25f, 4.0f, out float m))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetTextSpeedMultiplier(handle, m));
        }

        /// <summary>
        /// Faz 3 Dilim 5 — applies the accessibility display settings to the
        /// native renderer/camera. Dead handles stay silent (fail closed).
        /// </summary>
        public void SetTextScale(float scale)
        {
            if (!NativeGuard.TryClamp(scale, 1.0f, 2.0f, out float s))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetTextScale(handle, s));
        }

        // B4 — simetrik tamamlama: test-thread bridge-direkt okuyamaz
        // (owner-thread affinity, toEngineChecked fail-closed); okuma
        // worker-dispatch'tan akar.
        public float GetTextScale()
            => InvokeNative(NativeBridge.RowlEngine_GetTextScale, 1.0f);

        public void SetHighContrast(bool enabled)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetHighContrast(handle, enabled ? 1 : 0));
        }

        public void SetReducedMotion(bool enabled)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetReducedMotion(handle, enabled ? 1 : 0));
        }

        public void SetAutoAdvanceDelayOffset(float seconds)
        {
            if (!NativeGuard.TryClamp(seconds, 0.0f, 60.0f, out float s))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetAutoAdvanceDelayOffset(handle, s));
        }

        public void PlayAudio(string assetPath, int channelType = 0, int filterType = 0)
        {
            if (IsInitialized && !string.IsNullOrEmpty(assetPath))
                InvokeNative(handle => NativeBridge.RowlEngine_PlayAudio(handle, assetPath, channelType, filterType));
        }

        public void StopBgm()
        {
            InvokeNative(NativeBridge.RowlEngine_StopBgm);
        }

        public float GetAudioChannelPeak(int channelType, int channelIndex = 0)
            => InvokeNative(handle => NativeBridge.RowlEngine_GetAudioChannelPeak(handle, channelType, channelIndex), 0.0f);

        public float GetAudioChannelRms(int channelType, int channelIndex = 0)
            => InvokeNative(handle => NativeBridge.RowlEngine_GetAudioChannelRms(handle, channelType, channelIndex), 0.0f);

        public void GetAudioSpectrum(float[] outBands)
        {
            if (!IsInitialized || outBands == null || outBands.Length == 0) return;
            // #76-tur2: a foreign-handle call stamps WRONG_THREAD(14) and leaves
            // the buffer untouched (stale bands would read as live). Pre-clear
            // so the post-read belongs to this call; on a 14 hit zero the
            // array. The dead-handle path already zero-fills natively without
            // stamping.
            InvokeNative(handle => NativeBridge.RowlEngine_ClearLastResult(handle));
            InvokeNative(handle => NativeBridge.RowlEngine_GetAudioSpectrum(handle, outBands, outBands.Length));
            if (TryGetLastEngineResult(out int code, out _, out _) && code == (int)RuntimeErrorCode.WrongThread)
                Array.Clear(outBands);
        }

        // ── Typewriter Voice Blips & Audio Effects (Milestone 25) ─────────────

        public void PlayVoiceBlip(string soundPath, float pitch = 1.0f, float volume = 0.85f, int channelType = 1)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_PlayVoiceBlip(handle, soundPath, pitch, volume, channelType));
        }

        public void SetDialogueVoiceBlip(string soundPath, float basePitch, float pitchVariance, int cadence, bool skipPunctuation, int channelType)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetDialogueVoiceBlip(
                handle, soundPath, basePitch, pitchVariance, cadence, skipPunctuation ? 1 : 0, channelType));
        }

        public string GetDialogueVoiceBlipSound()
            => InvokeNative(handle => NativeBridge.PtrToString(
                NativeBridge.RowlEngine_GetDialogueVoiceBlipSoundWithLength(handle, out uint blipLen), blipLen), string.Empty);

        public float GetDialogueVoiceBlipPitch()
            => InvokeNative(NativeBridge.RowlEngine_GetDialogueVoiceBlipPitch, 1.0f);

        public float GetDialogueVoiceBlipVariance()
            => InvokeNative(NativeBridge.RowlEngine_GetDialogueVoiceBlipVariance, 0.08f);

        public int GetDialogueVoiceBlipCadence()
            => InvokeNative(NativeBridge.RowlEngine_GetDialogueVoiceBlipCadence, 1);

        public bool GetDialogueVoiceBlipSkipPunctuation()
            => InvokeNative(handle => NativeBridge.RowlEngine_GetDialogueVoiceBlipSkipPunctuation(handle) != 0, false);

        public int GetDialogueVoiceBlipChannel()
            => InvokeNative(NativeBridge.RowlEngine_GetDialogueVoiceBlipChannel, 1);

        public float GetDialogueVoiceBlipVolume()
            => InvokeNative(NativeBridge.RowlEngine_GetDialogueVoiceBlipVolume, 0.85f);

        public void SetDialogueVoiceBlipVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v))
                return;
            InvokeNative(handle => NativeBridge.RowlEngine_SetDialogueVoiceBlipVolume(handle, v));
        }

        public uint GetVoiceBlipCount()
            => InvokeNative(NativeBridge.RowlEngine_GetVoiceBlipCount, 0u);

        public void ResetVoiceBlipCount()
        {
            InvokeNative(NativeBridge.RowlEngine_ResetVoiceBlipCount);
        }

        // ── Save / Load Slots & History Rewind ────────────────────────────────

        public bool SaveGameSlot(int slotIndex)
            => InvokeNative(handle => NativeBridge.RowlEngine_SaveGameSlot(handle, slotIndex) != 0, false);

        public bool LoadGameSlot(int slotIndex)
        {
            if (!IsInitialized) return false;
            bool success = InvokeNative(handle => NativeBridge.RowlEngine_LoadGameSlot(handle, slotIndex) != 0, false);
            if (success) InvalidateDialogueHistory();
            if (success) ForceRenderFrame();
            return success;
        }

        public bool HasSaveSlot(int slotIndex)
            => InvokeNative(handle => NativeBridge.RowlEngine_HasSaveSlot(handle, slotIndex) != 0, false);

        public bool DeleteSaveSlot(int slotIndex)
            => InvokeNative(handle => NativeBridge.RowlEngine_DeleteSaveSlot(handle, slotIndex) != 0, false);

        /// <summary>
        /// Display metadata for one save slot without loading it into the
        /// live story (Faz 2 Dilim 4/5 slot picker). Null when the slot is
        /// missing, unreadable or unparsable; failures are counted, not
        /// thrown. The thumbnail stays base64 here; views decode it lazily.
        /// </summary>
        public SaveSlotMetadata? GetSaveSlotMetadata(int slotIndex)
        {
            if (!IsInitialized) return null;
            try
            {
                string json = InvokeNative(handle =>
                {
                    if (NativeBridge.RowlEngine_GetSaveSlotMetadataJson(
                            handle, slotIndex, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
                        required == 0)
                        return string.Empty;
                    IntPtr buffer = Marshal.AllocHGlobal((int)required);
                    try
                    {
                        if (NativeBridge.RowlEngine_GetSaveSlotMetadataJson(
                                handle, slotIndex, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                            return string.Empty;
                        return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }, string.Empty);
                if (string.IsNullOrWhiteSpace(json)) return null;
                return SaveSlotMetadata.FromJson(slotIndex, json);
            }
            catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or FormatException)
            {
                SlotMetadataParseErrorCount++;
                Debug.WriteLine($"EngineHost slot metadata parse failed ({SlotMetadataParseErrorCount}): {ex.Message}");
                return null;
            }
        }

        public bool Rewind(uint steps = 1)
        {
            if (!IsInitialized) return false;
            bool success = InvokeNative(handle => NativeBridge.RowlEngine_Rewind(handle, steps) != 0, false);
            if (success) InvalidateDialogueHistory();
            if (success) ForceRenderFrame();
            return success;
        }

        public ulong GetCurrentStepId()
            => InvokeNative(NativeBridge.RowlEngine_GetCurrentStepId, 0UL);

        // ── Scripting & Dynamic Variables ─────────────────────────────────────

        public void SetVariable(string key, string value)
        {
            if (IsInitialized && !string.IsNullOrEmpty(key))
                InvokeNative(handle => NativeBridge.RowlEngine_SetVariable(handle, key, value ?? string.Empty));
        }

        public string GetVariable(string key)
            => IsInitialized && !string.IsNullOrEmpty(key)
               ? InvokeNative(handle => NativeBridge.PtrToString(
                   NativeBridge.RowlEngine_GetVariableWithLength(handle, key, out uint varLen), varLen), string.Empty)
               : string.Empty;

        public bool EvaluateCondition(string conditionExpr)
            => InvokeNative(handle => NativeBridge.RowlEngine_EvaluateCondition(handle, conditionExpr) != 0, false);

        public bool ExecuteScript(string scriptCode)
            => InvokeNative(handle => NativeBridge.RowlEngine_ExecuteScript(handle, scriptCode) != 0, false);

        // ── Structured Runtime Results & Diagnostics ──────────────────────────

        public RuntimeErrorCode LastResultCode => InvokeNative(
            handle => (RuntimeErrorCode)NativeBridge.RowlEngine_GetLastResultCode(handle),
            RuntimeErrorCode.InvalidHandle);

        public string LastResultOperation => InvokeNative(handle => NativeBridge.PtrToString(
            NativeBridge.RowlEngine_GetLastResultOperationWithLength(handle, out uint opLen), opLen), "none");

        public string LastResultMessage => InvokeNative(handle => NativeBridge.PtrToString(
            NativeBridge.RowlEngine_GetLastResultMessageWithLength(handle, out uint msgLen), msgLen),
            "Invalid or uninitialized engine handle");

        public string LastResultTarget => InvokeNative(handle => NativeBridge.PtrToString(
            NativeBridge.RowlEngine_GetLastResultTargetWithLength(handle, out uint tgtLen), tgtLen), string.Empty);

        public void ClearLastResult()
        {
            InvokeNative(NativeBridge.RowlEngine_ClearLastResult);
        }

        // ── 2D Camera & Screen Shake Controls ──────────────────────────────────

        public void SetCamera(float x, float y, float zoom)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetCamera(handle, x, y, zoom));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost SetCamera presented paused frame (x={x}, y={y}, zoom={zoom}).");
            }
        }

        public void ResetCamera()
        {
            InvokeNative(NativeBridge.RowlEngine_ResetCamera);
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine("EngineHost ResetCamera presented paused frame.");
            }
        }

        public void CameraPanTo(float targetX, float targetY, float durationSeconds, int easingType = 3)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_CameraPanTo(handle, targetX, targetY, durationSeconds, easingType));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost CameraPanTo presented paused frame (target={targetX},{targetY}, duration={durationSeconds}).");
            }
        }

        public void CameraZoomTo(float targetZoom, float durationSeconds, int easingType = 3)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_CameraZoomTo(handle, targetZoom, durationSeconds, easingType));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost CameraZoomTo presented paused frame (target={targetZoom}, duration={durationSeconds}).");
            }
        }

        public bool IsCameraMoving()
        {
            return InvokeNative(handle => NativeBridge.RowlEngine_IsCameraMoving(handle) != 0, false);
        }

        public void TriggerCameraShake(float intensity, float durationSeconds)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_TriggerCameraShake(handle, intensity, durationSeconds));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost TriggerCameraShake presented paused frame (intensity={intensity}, duration={durationSeconds}).");
            }
        }

        public void TriggerCameraShakePreset(string presetName, float intensityMultiplier = 1.0f, float durationSeconds = 0.0f)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_TriggerCameraShakePreset(handle, presetName, intensityMultiplier, durationSeconds));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost TriggerCameraShakePreset presented paused frame (preset={presetName}).");
            }
        }

        public void TriggerCameraShakeProfile(float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_TriggerCameraShakeProfile(
                handle, intensity, durationSeconds, frequency, damping, dirX, dirY));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine("EngineHost TriggerCameraShakeProfile presented paused frame.");
            }
        }

        public float GetCameraShakeOffsetX()
        {
            return InvokeNative(NativeBridge.RowlEngine_GetCameraShakeOffsetX, 0.0f);
        }

        public float GetCameraShakeOffsetY()
        {
            return InvokeNative(NativeBridge.RowlEngine_GetCameraShakeOffsetY, 0.0f);
        }

        // ── Screen Visual FX (Flash, Tint, Vignette, Transitions) ────────────

        public void StartTransition(string kind, float durationSeconds, string? colorHex = null)
        {
            // #162: snapshot-capture failure stamps IoError (op=start_transition)
            // on the void path. Pre-clear so the read below belongs to this
            // call; the log is conditional — the single result-gated setter
            // among the 14 camera/FX setters.
            InvokeNative(handle => NativeBridge.RowlEngine_ClearLastResult(handle));
            InvokeNative(handle => NativeBridge.RowlEngine_StartTransition(handle, kind, durationSeconds, colorHex));
            if (TryGetLastEngineResult(out int code, out string op, out string message) && code != 0)
                Debug.WriteLine($"EngineHost StartTransition last-result {code} ({op}): {message}");
        }

        public bool IsTransitionActive()
        {
            return InvokeNative(handle => NativeBridge.RowlEngine_IsTransitionActive(handle) != 0, false);
        }

        public void TriggerScreenFlash(byte r, byte g, byte b, float durationSeconds, float intensity = 1.0f)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_TriggerScreenFlash(handle, r, g, b, durationSeconds, intensity));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost TriggerScreenFlash presented paused frame (duration={durationSeconds}).");
            }
        }

        public void TriggerScreenFlashHex(string colorHex, float durationSeconds, float intensity = 1.0f)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_TriggerScreenFlashHex(handle, colorHex, durationSeconds, intensity));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost TriggerScreenFlashHex presented paused frame (color={colorHex}).");
            }
        }

        public bool IsScreenFlashActive()
        {
            return InvokeNative(handle => NativeBridge.RowlEngine_IsScreenFlashActive(handle) != 0, false);
        }

        public void SetScreenTint(byte r, byte g, byte b, float opacity)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetScreenTint(handle, r, g, b, opacity));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost SetScreenTint presented paused frame (opacity={opacity}).");
            }
        }

        public void SetScreenTintHex(string colorHex, float opacity)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetScreenTintHex(handle, colorHex, opacity));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost SetScreenTintHex presented paused frame (color={colorHex}, opacity={opacity}).");
            }
        }

        public void ClearScreenTint()
        {
            InvokeNative(NativeBridge.RowlEngine_ClearScreenTint);
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine("EngineHost ClearScreenTint presented paused frame.");
            }
        }

        public float GetScreenTintOpacity()
        {
            return InvokeNative(NativeBridge.RowlEngine_GetScreenTintOpacity, 0.0f);
        }

        public void SetVignette(float intensity, float radius = 0.75f, string? colorHex = null)
        {
            InvokeNative(handle => NativeBridge.RowlEngine_SetVignette(handle, intensity, radius, colorHex));
            if (!IsPlaying)
            {
                Step();
                Debug.WriteLine($"EngineHost SetVignette presented paused frame (intensity={intensity}).");
            }
        }

        public float GetVignetteIntensity()
        {
            return InvokeNative(NativeBridge.RowlEngine_GetVignetteIntensity, 0.0f);
        }

        // ── Disposal ──────────────────────────────────────────────────────────

        public void Dispose()
        {
            _tickTimer?.Stop();
            _tickTimer = null;

            OffscreenRuntimeWorker? runtime = _runtime;
            _runtime = null;
            runtime?.Dispose();
            _lastPreviewComponentsJson = null;
            IsPlaying = false;
            var bitmap = RenderTargetBitmap;
            RenderTargetBitmap = null;
            bitmap?.Dispose();
        }
    }

    public enum RuntimeErrorCode
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
        // B4 — B3a'da NativeBridge.ResultCode'a eklenen 12/13'ün public
        // aynası (sözleşme-aynalama; class-logic sıfır-diff).
        BufferTooSmall = 12,
        Unsupported = 13,
        // D3 (B1d #102) — 14'ün public aynası (canlı handle'a yabancı
        // thread çağrısı; LastResultCode zaten bunu taşır, cast değişmez).
        WrongThread = 14,
        UnknownError = 99
    }

    public sealed class ScriptRuntimeDiagnostic
    {
        public string module_id { get; set; } = string.Empty;
        public string path { get; set; } = string.Empty;
        public string state { get; set; } = string.Empty;
        public string error { get; set; } = string.Empty;
    }

    public sealed class DialogueHistoryEntry
    {
        public ulong node_id { get; set; }
        public string speaker { get; set; } = string.Empty;
        public string dialogue { get; set; } = string.Empty;
        public bool read { get; set; }
        /// <summary>
        /// Persistent Faz 2 content identity (UUID form); empty when the
        /// presented line predates content_id migration.
        /// </summary>
        public string content_id { get; set; } = string.Empty;
    }

    /// <summary>
    /// Display-only save-slot metadata (Faz 2 Dilim 4/5 slot picker).
    /// Parsed from <c>RowlEngine_GetSaveSlotMetadataJson</c>; missing keys
    /// fall back to backward-compatible defaults (pre-thumbnail on-disk
    /// slots), malformed JSON throws <see cref="JsonException"/> for the
    /// caller to count.
    /// </summary>
    public sealed class SaveSlotMetadata
    {
        public int Slot { get; set; }
        public string SavedAt { get; set; } = string.Empty;
        public double PlaytimeSeconds { get; set; }
        public string ChapterId { get; set; } = string.Empty;
        public string ChapterTitle { get; set; } = string.Empty;
        public string Summary { get; set; } = string.Empty;
        public uint ThumbnailWidth { get; set; }
        public uint ThumbnailHeight { get; set; }
        public bool HasThumbnail { get; set; }
        public string ThumbnailPngBase64 { get; set; } = string.Empty;

        public static SaveSlotMetadata FromJson(int slot, string json)
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            return new SaveSlotMetadata
            {
                Slot = slot,
                SavedAt = GetString(root, "saved_at"),
                PlaytimeSeconds = GetDouble(root, "playtime_seconds"),
                ChapterId = GetString(root, "chapter_id"),
                ChapterTitle = GetString(root, "chapter_title"),
                Summary = GetString(root, "summary"),
                ThumbnailWidth = GetUInt32(root, "thumbnail_width"),
                ThumbnailHeight = GetUInt32(root, "thumbnail_height"),
                HasThumbnail = GetBoolean(root, "has_thumbnail"),
                ThumbnailPngBase64 = GetString(root, "thumbnail_png_base64"),
            };

            static string GetString(JsonElement element, string name)
                => element.TryGetProperty(name, out var value) &&
                   value.ValueKind == JsonValueKind.String
                    ? value.GetString() ?? string.Empty
                    : string.Empty;

            static double GetDouble(JsonElement element, string name)
                => element.TryGetProperty(name, out var value) &&
                   value.ValueKind == JsonValueKind.Number &&
                   value.TryGetDouble(out double number)
                    ? number
                    : 0.0;

            static uint GetUInt32(JsonElement element, string name)
                => element.TryGetProperty(name, out var value) &&
                   value.ValueKind == JsonValueKind.Number &&
                   value.TryGetUInt32(out uint number)
                    ? number
                    : 0;

            static bool GetBoolean(JsonElement element, string name)
                => element.TryGetProperty(name, out var value) &&
                   (value.ValueKind == JsonValueKind.True ||
                    value.ValueKind == JsonValueKind.False)
                    ? value.GetBoolean()
                    : false;
        }
    }
}
