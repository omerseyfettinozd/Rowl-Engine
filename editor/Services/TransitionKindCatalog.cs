using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Services;

/// <summary>
/// One entry of the transition-kind picker: the JSON <see cref="Kind"/> the
/// native <c>StartTransition</c> accepts plus its editor-string display name
/// (<c>Component.Transition.Kind.&lt;kind&gt;</c>, tr/en). <see cref="DisplayName"/>
/// re-reads the string system, so a language switch refreshes bound lists.
/// </summary>
public sealed class TransitionKindOption : ObservableObject
{
    internal TransitionKindOption(string kind) => Kind = kind;

    /// <summary>The value written to the node's <c>transition</c> component <c>kind</c> key.</summary>
    public string Kind { get; }

    /// <summary>User-facing name; the raw kind when the catalogs know no string for it.</summary>
    public string DisplayName => TransitionKindCatalog.DisplayNameOf(Kind);

    internal void RaiseDisplayNameChanged() => OnPropertyChanged(nameof(DisplayName));
}

/// <summary>
/// D06-2 — the transition kinds the editor offers. The list comes from the
/// native engine (<c>RowlEngine_GetSupportedTransitionKindsJson</c>, handle-free,
/// the same table <c>StartTransition</c> accepts), so a kind the runtime learns
/// later shows up without an editor release. When the library is missing or
/// answers nothing usable the picker falls back to the six kinds the editor
/// has always offered. Display names are editor strings, never hard-coded.
/// </summary>
public static class TransitionKindCatalog
{
    /// <summary>The kinds offered before the native list was wired (fallback).</summary>
    public static IReadOnlyList<string> FallbackKinds { get; } = new[]
    {
        "crossfade",
        "fade_black",
        "fade_white",
        "fade_color",
        "wipe_left",
        "wipe_right"
    };

    // Native accepts a few short aliases (typeForKind in transition_manager.cpp).
    // They resolve to the canonical entry for display; the JSON value is untouched.
    private static readonly Dictionary<string, string> Aliases = new(StringComparer.Ordinal)
    {
        ["fade"] = "crossfade",
        ["black"] = "fade_black",
        ["white"] = "fade_white",
        ["color"] = "fade_color",
        ["push"] = "push_left",
        ["iris"] = "iris_in"
    };

    private static readonly object Gate = new();
    private static IReadOnlyList<TransitionKindOption>? _options;
    private static bool _stringsHooked;

    /// <summary>Kind names in picker order (native list, or the fallback).</summary>
    public static IReadOnlyList<string> Kinds => Options.Select(option => option.Kind).ToArray();

    /// <summary>Picker entries; built once from the native list.</summary>
    public static IReadOnlyList<TransitionKindOption> Options
    {
        get
        {
            lock (Gate)
            {
                if (_options is null)
                {
                    _options = Resolve(ReadNativeJson).Select(kind => new TransitionKindOption(kind)).ToArray();
                    if (!_stringsHooked)
                    {
                        EditorStrings.Instance.PropertyChanged += OnEditorStringsChanged;
                        _stringsHooked = true;
                    }
                }
                return _options;
            }
        }
    }

    /// <summary>
    /// Parses the native JSON array into kind names. Non-string, blank and
    /// duplicate entries are dropped; anything that is not a JSON array of at
    /// least one usable string yields <c>null</c>.
    /// </summary>
    public static IReadOnlyList<string>? ParseKinds(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
            return null;
        try
        {
            using var document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Array)
                return null;
            var kinds = new List<string>();
            foreach (var item in document.RootElement.EnumerateArray())
            {
                if (item.ValueKind != JsonValueKind.String)
                    continue;
                string kind = item.GetString()!.Trim();
                if (kind.Length > 0 && !kinds.Contains(kind, StringComparer.Ordinal))
                    kinds.Add(kind);
            }
            return kinds.Count == 0 ? null : kinds;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    /// <summary>The native list when <paramref name="nativeJson"/> yields one, else <see cref="FallbackKinds"/>. Never throws.</summary>
    public static IReadOnlyList<string> Resolve(Func<string?>? nativeJson)
    {
        try
        {
            return ParseKinds(nativeJson?.Invoke()) ?? FallbackKinds;
        }
        catch (Exception)
        {
            return FallbackKinds;
        }
    }

    /// <summary>The picker entry for a stored kind (aliases resolve to their canonical entry), or null.</summary>
    public static TransitionKindOption? FindOption(string? kind)
    {
        if (string.IsNullOrWhiteSpace(kind))
            return null;
        var options = Options;
        return options.FirstOrDefault(o => string.Equals(o.Kind, kind, StringComparison.Ordinal))
            ?? (Aliases.TryGetValue(kind, out string? canonical)
                ? options.FirstOrDefault(o => string.Equals(o.Kind, canonical, StringComparison.Ordinal))
                : null);
    }

    /// <summary>Editor-string name of a kind; the kind itself when no string exists. Never empty for a non-empty kind.</summary>
    public static string DisplayNameOf(string kind)
    {
        string text = EditorStrings.T("Component.Transition.Kind." + kind, kind, kind);
        return string.IsNullOrWhiteSpace(text) ? kind : text;
    }

    private static void OnEditorStringsChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (!string.IsNullOrEmpty(e.PropertyName) &&
            e.PropertyName != EditorStrings.IndexerPropertyName &&
            e.PropertyName != nameof(EditorStrings.Language))
            return;
        IReadOnlyList<TransitionKindOption>? snapshot;
        lock (Gate)
            snapshot = _options;
        if (snapshot is null)
            return;
        foreach (var option in snapshot)
            option.RaiseDisplayNameChanged();
    }

    /// <summary>Asks the native library; null when it is missing or refuses.</summary>
    private static string? ReadNativeJson()
    {
        try
        {
            if (NativeBridge.RowlEngine_GetSupportedTransitionKindsJson(IntPtr.Zero, 0, out uint required) !=
                    NativeBridge.ResultCode.Ok || required == 0)
                return null;
            IntPtr buffer = Marshal.AllocHGlobal((int)required);
            try
            {
                if (NativeBridge.RowlEngine_GetSupportedTransitionKindsJson(buffer, required, out _) !=
                        NativeBridge.ResultCode.Ok)
                    return null;
                return Marshal.PtrToStringUTF8(buffer);
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        catch (Exception)
        {
            return null;
        }
    }
}
