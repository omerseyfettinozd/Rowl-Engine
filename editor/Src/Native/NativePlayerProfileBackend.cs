using System;
using System.Collections.Generic;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Native
{
    /// <summary>
    /// D01c-2 - the native engine's per-game player profile as the editor
    /// player loop's store. Single-writer rule: while this backend is in use the
    /// managed code never writes <c>player-profile.json</c>; preferences go
    /// through <c>RowlEngine_ProfileSetPreferencesJson</c>, reads through
    /// <c>RowlEngine_ProfileMarkRead</c> and explicit save points through
    /// <c>RowlEngine_ProfileFlush</c>. Every native call runs on the engine
    /// thread (<see cref="EngineHost.InvokeOnEngineThread{T}"/>).
    /// A backend is bound to the project mounted when it was created: if the
    /// editor mounts another project meanwhile it turns read-only (the native
    /// store has switched to the other game's profile).
    /// </summary>
    internal sealed class NativePlayerProfileBackend : IPlayerProfileBackend
    {
        private readonly EngineHost _host;
        private readonly string _mountedRoot;
        private bool _storeReadOnly;
        private bool _detached;
        private bool _detachReported;

        private NativePlayerProfileBackend(EngineHost host, string mountedRoot, string profileDirectory)
        {
            _host = host;
            _mountedRoot = mountedRoot;
            ProfileDirectory = profileDirectory;
        }

        /// <summary>Profile directory the native store reads and writes.</summary>
        internal string ProfileDirectory { get; }

        /// <summary>
        /// Creates a backend when the library has the capability, the engine is
        /// up and a project is mounted (the native profile is then loaded);
        /// otherwise null and the caller falls back to the managed store.
        /// </summary>
        internal static NativePlayerProfileBackend? TryCreate(EngineHost host)
        {
            ArgumentNullException.ThrowIfNull(host);
            if (!NativeProfileBridge.IsSupported() || !host.IsInitialized)
                return null;
            string root = host.MountedProjectRoot;
            if (string.IsNullOrEmpty(root))
                return null;
            var probe = host.InvokeOnEngineThread(
                handle => (Status: NativeProfileBridge.GetStatus(handle), Dir: NativeProfileBridge.GetProfileDirectory(handle)),
                (Status: NativeProfileStatus.Unavailable, Dir: string.Empty));
            if (!probe.Status.IsMounted || probe.Dir.Length == 0)
                return null;
            return new NativePlayerProfileBackend(host, root, probe.Dir);
        }

        internal sealed record OpenResult(
            PlayerProfile Profile,
            PlayerProfileLoadStatus Status,
            string? Diagnostic,
            bool ReadOnly,
            GameIdentityInfo Identity);

        /// <summary>Reads the current native profile and its status.</summary>
        internal OpenResult Open()
        {
            var snapshot = _host.InvokeOnEngineThread(
                handle => (
                    Status: NativeProfileBridge.GetStatus(handle),
                    Json: NativeProfileBridge.GetJson(handle),
                    Identity: GameIdentityBridge.Read(handle)),
                (Status: NativeProfileStatus.Unavailable, Json: string.Empty, Identity: GameIdentityInfo.None));

            var status = snapshot.Status;
            var parsed = PlayerProfileStore.Parse(snapshot.Json, status.Path.Length > 0 ? status.Path : "native profile");
            var profile = parsed.Status == PlayerProfileLoadStatus.Loaded ? parsed.Profile : new PlayerProfile();
            PlayerProfileLoadStatus mapped = status.Status switch
            {
                "loaded" => PlayerProfileLoadStatus.Loaded,
                "missing" => PlayerProfileLoadStatus.MissingDefaults,
                "unsupported_version" => PlayerProfileLoadStatus.UnsupportedVersionDefaults,
                "wrong_game" => PlayerProfileLoadStatus.WrongGameDefaults,
                _ => PlayerProfileLoadStatus.CorruptDefaults,
            };
            bool readOnly = status.ReadOnly || !status.IsMounted;
            _storeReadOnly = readOnly;
            string? diagnostic = status.Diagnostic.Length > 0 ? status.Diagnostic : parsed.Diagnostic;
            if (!status.IsMounted)
                diagnostic ??= "The native player profile is not available; preferences are not saved.";
            return new OpenResult(profile, mapped, diagnostic, readOnly, snapshot.Identity);
        }

        public bool IsWritable => !_storeReadOnly && StillAttached();

        public string? MarkRead(IReadOnlyList<string> contentIds)
        {
            if (!TryAttach(out string? detachError))
                return detachError;
            string? error = _host.InvokeOnEngineThread<string?>(
                handle =>
                {
                    string? failure = null;
                    foreach (string id in contentIds)
                    {
                        var result = NativeProfileBridge.MarkRead(handle, id);
                        if (result == NativeBridge.ResultCode.Ok)
                            continue;
                        if (result == NativeBridge.ResultCode.StateError)
                        {
                            _storeReadOnly = true; // native refuses writes (read-only / unmounted)
                            return null;
                        }
                        failure ??= result == NativeBridge.ResultCode.ValidationError
                            ? "The player profile's read set is full; new lines are no longer recorded."
                            : $"The native player profile rejected a read mark ({result}).";
                    }
                    return failure;
                },
                "The engine is not running; read marks were not saved.");
            return error;
        }

        public string? SavePreferences(PlayerProfile profile)
        {
            ArgumentNullException.ThrowIfNull(profile);
            if (!TryAttach(out string? detachError))
                return detachError;
            string json = PlayerProfileStore.PreferencesToJson(profile);
            return _host.InvokeOnEngineThread<string?>(
                handle =>
                {
                    var set = NativeProfileBridge.SetPreferencesJson(handle, json);
                    if (set == NativeBridge.ResultCode.StateError)
                    {
                        _storeReadOnly = true;
                        return null;
                    }
                    if (set != NativeBridge.ResultCode.Ok)
                        return $"The native player profile rejected the preferences ({set}).";
                    return FlushCore(handle);
                },
                "The engine is not running; preferences were not saved.");
        }

        public string? Flush()
        {
            if (!TryAttach(out string? detachError))
                return detachError;
            return _host.InvokeOnEngineThread<string?>(
                FlushCore,
                "The engine is not running; the player profile was not saved.");
        }

        private static string? FlushCore(IntPtr handle)
        {
            var flush = NativeProfileBridge.Flush(handle);
            return flush == NativeBridge.ResultCode.Ok
                ? null
                : $"The native player profile could not be written ({flush}).";
        }

        private bool StillAttached()
        {
            if (_detached)
                return false;
            if (!string.Equals(_host.MountedProjectRoot, _mountedRoot, StringComparison.Ordinal))
                _detached = true;
            return !_detached;
        }

        /// <summary>
        /// False once another project replaced the one this backend was opened
        /// for; the first call after that returns the explanatory error, later
        /// ones stay silent.
        /// </summary>
        private bool TryAttach(out string? error)
        {
            error = null;
            if (_storeReadOnly)
                return false;
            if (StillAttached())
                return true;
            if (!_detachReported)
            {
                _detachReported = true;
                error = "Another project was opened since this player window; its profile is no longer written.";
            }
            return false;
        }
    }
}
