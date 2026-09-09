/**
 * EngineHost.cs
 *
 * High-level manager for the embedded C++ Engine lifetime.
 *
 * Responsibilities:
 *   - Creates / destroys the native engine handle (RowlEngine_Create / Destroy)
 *   - Drives the engine tick via Avalonia's DispatcherTimer (~60 FPS) in Play mode (Unity-style)
 *   - Copies the offscreen RGBA32 framebuffer into an Avalonia WriteableBitmap
 *   - Controls Play / Stop playback state and story resets
 */

using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;

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

        private IntPtr _handle = IntPtr.Zero;
        private DispatcherTimer? _tickTimer;
        private DateTime _lastTick = DateTime.UtcNow;

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
        public bool IsRunning =>
            _handle != IntPtr.Zero && NativeBridge.RowlEngine_IsRunning(_handle) != 0;

        /// <summary>True after a successful Initialize() call.</summary>
        public bool IsInitialized => _handle != IntPtr.Zero;

        /// <summary>Raw native engine pointer.</summary>
        public IntPtr Handle => _handle;

        public ulong TextureCacheBudgetBytes => _handle == IntPtr.Zero
            ? 0UL
            : NativeBridge.RowlEngine_GetTextureCacheBudgetBytes(_handle);

        public ulong TextureCacheEvictionCount => _handle == IntPtr.Zero
            ? 0UL
            : NativeBridge.RowlEngine_GetTextureCacheEvictionCount(_handle);

        /// <summary>
        /// Sets the decoded texture-cache ceiling for this runtime. Use a
        /// device-profile budget after Initialize; the native layer clamps
        /// values below 1 MiB and evicts least-recently-used textures safely.
        /// </summary>
        public void SetTextureCacheBudgetBytes(ulong bytes)
        {
            if (_handle != IntPtr.Zero)
                NativeBridge.RowlEngine_SetTextureCacheBudgetBytes(_handle, bytes);
        }

        // ── Initialisation ───────────────────────────────────────────────────

        /// <summary>
        /// Creates and initialises the engine in offscreen framebuffer mode.
        /// </summary>
        public bool Initialize(uint width = 1920, uint height = 1080, bool vsync = true)
        {
            if (_handle != IntPtr.Zero)
                return true; // Already initialised

            _handle = NativeBridge.RowlEngine_Create();
            if (_handle == IntPtr.Zero) return false;

            int result = NativeBridge.RowlEngine_Init(
                _handle, width, height, vsync ? 1 : 0);

            if (result == 0)
            {
                Dispose();
                return false;
            }

            // Initial static frame render
            NativeBridge.RowlEngine_Step(_handle, 0.0f);
            UpdatePixelBuffer();

            StartTickTimer();
            return true;
        }

        /// <summary>
        /// Backward compatibility method for view components.
        /// </summary>
        public bool InitializeEmbedded(IntPtr nativeWindowHandle, uint width, uint height, bool vsync = true)
        {
            return Initialize(width, height, vsync);
        }

        // ── Tick loop ────────────────────────────────────────────────────────

        private void StartTickTimer()
        {
            _lastTick = DateTime.UtcNow;
            _tickTimer = new DispatcherTimer(DispatcherPriority.Render)
            {
                Interval = TimeSpan.FromMilliseconds(16) // ~60 FPS
            };
            _tickTimer.Tick += OnTick;
            _tickTimer.Start();
        }

        private void OnTick(object? sender, EventArgs e)
        {
            if (_handle == IntPtr.Zero) return;

            // Unity-style PlayMode check: Live gameplay loop runs ONLY when IsPlaying is true!
            if (!IsPlaying) return;

            var now = DateTime.UtcNow;
            float dt = (float)(now - _lastTick).TotalSeconds;
            _lastTick = now;

            // Clamp: prevent spiral of death after sleep/pause
            if (dt > 0.25f) dt = 0.25f;
            if (dt < 0.0f)  dt = 0.0f;

            NativeBridge.RowlEngine_Step(_handle, dt);
            UpdatePixelBuffer();
        }

        private void UpdatePixelBuffer()
        {
            if (_handle == IntPtr.Zero) return;

            try
            {
                IntPtr pixelPtr = NativeBridge.RowlEngine_GetPixelBuffer(_handle, out uint w, out uint h);
                if (pixelPtr != IntPtr.Zero && w > 0 && h > 0)
                {
                    int width = (int)w;
                    int height = (int)h;

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
                        unsafe
                        {
                            Buffer.MemoryCopy(
                                (void*)pixelPtr,
                                (void*)buf.Address,
                                buf.RowBytes * height,
                                width * height * 4);
                        }
                    }
                    OnPropertyChanged(nameof(RenderTargetBitmap));
                    FrameUpdated?.Invoke();
                }
            }
            catch
            {
                // Safe ignore if running in headless test without Avalonia render interface
            }
        }

        // ── Playback & Engine State Control ──────────────────────────────────

        /// <summary>Sets the C++ engine playback state (true = Play, false = Stop/Pause).</summary>
        public void SetPlayState(bool isPlaying)
        {
            if (IsPlaying == isPlaying) return;
            IsPlaying = isPlaying;
            _lastTick = DateTime.UtcNow;

            if (_handle != IntPtr.Zero)
            {
                NativeBridge.RowlEngine_SetPlayState(_handle, isPlaying ? 1 : 0);
                if (!isPlaying)
                {
                    // Render static frame when stopping
                    NativeBridge.RowlEngine_Step(_handle, 0.0f);
                    UpdatePixelBuffer();
                }
            }
            OnPropertyChanged(nameof(IsPlaying));
        }

        /// <summary>Resets the C++ engine story state back to the starting node.</summary>
        public void ResetToStartNode()
        {
            if (_handle != IntPtr.Zero)
            {
                NativeBridge.RowlEngine_ResetToStartNode(_handle);
                NativeBridge.RowlEngine_Step(_handle, 0.0f);
                UpdatePixelBuffer();
            }
        }

        // ── Scene / story control ────────────────────────────────────────────

        /// <summary>
        /// Pushes a complete visual novel scene update to the engine.
        /// This is a direct in-process call — zero serialisation overhead.
        /// </summary>
        public void UpdateScene(
            string speaker,   string dialogue,  string background,
            float  bgX,       float  bgY,       float  bgW,       float  bgH,
            string character,
            float  charX,     float  charY,     float  charW,     float  charH,
            float  dlgX,      float  dlgY,      float  dlgW,      float  dlgH)
        {
            if (_handle == IntPtr.Zero) return;

            NativeBridge.RowlEngine_UpdateScene(
                _handle,
                speaker ?? "", dialogue ?? "", background ?? "",
                bgX, bgY, bgW, bgH,
                character ?? "",
                charX, charY, charW, charH,
                dlgX,  dlgY,  dlgW,  dlgH);

            if (!IsPlaying)
            {
                NativeBridge.RowlEngine_Step(_handle, 0.0f);
                UpdatePixelBuffer();
            }
        }

        /// <summary>
        /// Pushes component-based scene data to the engine as a JSON string.
        /// This is the component-aware alternative to UpdateScene.
        /// </summary>
        public void UpdateSceneFromComponents(string componentsJson)
        {
            if (_handle == IntPtr.Zero || string.IsNullOrEmpty(componentsJson)) return;

            NativeBridge.RowlEngine_UpdateSceneFromJson(_handle, componentsJson);

            if (!IsPlaying)
            {
                NativeBridge.RowlEngine_Step(_handle, 0.0f);
                UpdatePixelBuffer();
            }
        }

        /// <summary>Loads (or reloads) a story graph JSON file into the engine.</summary>
        public void LoadStoryGraph(string jsonPath)
        {
            if (_handle != IntPtr.Zero && !string.IsNullOrEmpty(jsonPath))
            {
                NativeBridge.RowlEngine_LoadStoryGraph(_handle, jsonPath);
                if (!IsPlaying)
                {
                    NativeBridge.RowlEngine_Step(_handle, 0.0f);
                    UpdatePixelBuffer();
                }
            }
        }

        /// <summary>Sets the active project root directory, isolating VFS mounts to that project.</summary>
        public void SetProjectDirectory(string projectRoot)
        {
            if (_handle != IntPtr.Zero && !string.IsNullOrEmpty(projectRoot))
            {
                NativeBridge.RowlEngine_SetProjectDirectory(_handle, projectRoot);
                if (!IsPlaying)
                {
                    NativeBridge.RowlEngine_Step(_handle, 0.0f);
                    UpdatePixelBuffer();
                }
            }
        }

        /// <summary>Forces an immediate single-step render and pixel buffer refresh (zero-latency UI update).</summary>
        public void ForceRenderFrame()
        {
            if (_handle != IntPtr.Zero)
            {
                NativeBridge.RowlEngine_Step(_handle, 0.0f);
                UpdatePixelBuffer();
            }
        }

        /// <summary>Advances the story to the next node on the given branch.</summary>
        public void AdvanceNode(uint choiceIndex = 0)
        {
            if (_handle != IntPtr.Zero)
            {
                NativeBridge.RowlEngine_AdvanceNode(_handle, choiceIndex);
                NativeBridge.RowlEngine_Step(_handle, 0.0f);
                UpdatePixelBuffer();
            }
        }

        public bool PointerDown(float virtualX, float virtualY)
        {
            if (_handle == IntPtr.Zero) return false;
            bool consumed = NativeBridge.RowlEngine_PointerDown(_handle, virtualX, virtualY) != 0;
            NativeBridge.RowlEngine_Step(_handle, 0.0f);
            UpdatePixelBuffer();
            return consumed;
        }

        // ── Viewport control ─────────────────────────────────────────────────

        /// <summary>Notifies the engine that the render area was resized.</summary>
        public void ResizeViewport(uint newWidth, uint newHeight)
        {
            if (_handle != IntPtr.Zero)
                NativeBridge.RowlEngine_ResizeViewport(_handle, newWidth, newHeight);
        }

        // ── State queries ─────────────────────────────────────────────────────

        public string GetSpeaker()
            => _handle == IntPtr.Zero ? string.Empty
               : NativeBridge.PtrToString(NativeBridge.RowlEngine_GetSpeaker(_handle));

        public string GetDialogue()
            => _handle == IntPtr.Zero ? string.Empty
               : NativeBridge.PtrToString(NativeBridge.RowlEngine_GetDialogue(_handle));

        public ulong GetCurrentNodeId()
            => _handle == IntPtr.Zero ? 0
               : NativeBridge.RowlEngine_GetCurrentNodeId(_handle);

        public bool IsBgmPlaying
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_IsBgmPlaying(_handle) != 0;

        public bool IsVoicePlaying
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_IsVoicePlaying(_handle) != 0;

        public int ActiveDspFilter
            => _handle == IntPtr.Zero ? 0 : NativeBridge.RowlEngine_GetActiveDspFilter(_handle);

        public string LastAudioError
            => _handle == IntPtr.Zero ? string.Empty
               : NativeBridge.PtrToString(NativeBridge.RowlEngine_GetLastAudioError(_handle));

        // ── Save / Load Slots & History Rewind ────────────────────────────────

        public bool SaveGameSlot(int slotIndex)
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_SaveGameSlot(_handle, slotIndex) != 0;

        public bool LoadGameSlot(int slotIndex)
        {
            if (_handle == IntPtr.Zero) return false;
            bool success = NativeBridge.RowlEngine_LoadGameSlot(_handle, slotIndex) != 0;
            if (success) ForceRenderFrame();
            return success;
        }

        public bool HasSaveSlot(int slotIndex)
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_HasSaveSlot(_handle, slotIndex) != 0;

        public bool DeleteSaveSlot(int slotIndex)
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_DeleteSaveSlot(_handle, slotIndex) != 0;

        public bool Rewind(uint steps = 1)
        {
            if (_handle == IntPtr.Zero) return false;
            bool success = NativeBridge.RowlEngine_Rewind(_handle, steps) != 0;
            if (success) ForceRenderFrame();
            return success;
        }

        public ulong GetCurrentStepId()
            => _handle != IntPtr.Zero ? NativeBridge.RowlEngine_GetCurrentStepId(_handle) : 0;

        // ── Scripting & Dynamic Variables ─────────────────────────────────────

        public void SetVariable(string key, string value)
        {
            if (_handle != IntPtr.Zero && !string.IsNullOrEmpty(key))
                NativeBridge.RowlEngine_SetVariable(_handle, key, value ?? string.Empty);
        }

        public string GetVariable(string key)
            => _handle != IntPtr.Zero && !string.IsNullOrEmpty(key)
               ? NativeBridge.PtrToString(NativeBridge.RowlEngine_GetVariable(_handle, key))
               : string.Empty;

        public bool EvaluateCondition(string conditionExpr)
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_EvaluateCondition(_handle, conditionExpr) != 0;

        public bool ExecuteScript(string scriptCode)
            => _handle != IntPtr.Zero && NativeBridge.RowlEngine_ExecuteScript(_handle, scriptCode) != 0;

        // ── Disposal ──────────────────────────────────────────────────────────

        public void Dispose()
        {
            _tickTimer?.Stop();
            _tickTimer = null;

            if (_handle != IntPtr.Zero)
            {
                NativeBridge.RowlEngine_Shutdown(_handle);
                NativeBridge.RowlEngine_Destroy(_handle);
                _handle = IntPtr.Zero;
            }
            IsPlaying = false;
            var bitmap = RenderTargetBitmap;
            RenderTargetBitmap = null;
            bitmap?.Dispose();
        }
    }
}
