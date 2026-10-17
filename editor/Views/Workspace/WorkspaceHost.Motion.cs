using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;
using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Views.Workspace;

/// <summary>
/// Faz 5 D6 — motion of the workspace: layout changes settle in ~200 ms, the
/// drop highlight eases in ~180 ms, both with the prototype's easing; a
/// splitter drag or keyboard move never animates.
/// <para>
/// Technique (cheap, one clock): the tree always lays out to its final slots —
/// <see cref="GetPanelRect"/> and the drop hit test see the final rectangles —
/// and one eased progress value (<see cref="LayoutProgressProperty"/>, driven by
/// an Avalonia <see cref="DoubleTransition"/>) blends each panel's <i>displayed</i>
/// rectangle from where it was to where it is now. Panels that were not shown
/// fade in and rise 6 px (prototype <c>enter</c>); splitters slide with their
/// neighbours. Nothing is re-parented and no panel is hosted twice, so focus,
/// scroll and selection are untouched. A moving panel is laid out once, at its final slot; the
/// in-between frames only change its render transform (scale + translate), so no frame re-measures
/// panel content. During a pointer drag only the highlight is arranged (one rectangle).
/// </para>
/// </summary>
public partial class WorkspaceHost
{
    /// <summary>Prototype <c>workspace-motion.js</c> <c>animate()</c> default: 200 ms (layout settle, entering panel).</summary>
    public static readonly TimeSpan LayoutMotionDuration = TimeSpan.FromMilliseconds(200);

    /// <summary>Prototype <c>workspace-motion.js</c> <c>exit()</c>: 180 ms (also used for the drop highlight, which has no value of its own).</summary>
    public static readonly TimeSpan DropMotionDuration = TimeSpan.FromMilliseconds(180);

    private static Easing? _motionEasing;

    /// <summary>Prototype easing: <c>cubic-bezier(.22,1,.36,1)</c>.</summary>
    public static Easing MotionEasing => _motionEasing ??= new SplineEasing(0.22, 1, 0.36, 1);

    /// <summary>Prototype <c>enter</c>: a new panel rises from <c>translateY(6px)</c>.</summary>
    public const double EnterRise = 6;

    /// <summary>
    /// "Reduce motion": every transition is 0 ms. Avalonia 11.3 has no operating
    /// system reduced-motion query; the default comes from <see cref="SystemPrefersReducedMotion"/>
    /// (the <c>ROWL_REDUCE_MOTION</c> environment variable) and the active editor settings profile
    /// (<see cref="ProfilePrefersReducedMotion"/>), and can also be bound or set per host.
    /// </summary>
    public static readonly StyledProperty<bool> ReduceMotionProperty =
        AvaloniaProperty.Register<WorkspaceHost, bool>(nameof(ReduceMotion), ReadSystemPreference());

    /// <summary>Eased 0 → 1 position of the running layout motion (1 = settled).</summary>
    public static readonly StyledProperty<double> LayoutProgressProperty =
        AvaloniaProperty.Register<WorkspaceHost, double>(nameof(LayoutProgress), 1.0);

    /// <summary>Eased 0 → 1 position of the drop highlight's last change (1 = settled).</summary>
    public static readonly StyledProperty<double> DropProgressProperty =
        AvaloniaProperty.Register<WorkspaceHost, double>(nameof(DropProgress), 1.0);

    /// <summary>Best-effort operating-system preference (env <c>ROWL_REDUCE_MOTION</c> = 1 / true); false when unknown.</summary>
    public static bool SystemPrefersReducedMotion => ReadSystemPreference();

    private static readonly List<WeakReference<WorkspaceHost>> LiveHosts = new();
    private static bool _profilePrefersReducedMotion;

    /// <summary>
    /// Active editor settings profile preference (<c>EditorSettingsProfile.ReduceMotion</c>),
    /// synchronized by <c>EditorSettingsSyncService</c> and <c>SettingsViewModel</c>.
    /// </summary>
    public static bool ProfilePrefersReducedMotion
    {
        get => _profilePrefersReducedMotion;
        set
        {
            if (_profilePrefersReducedMotion == value)
                return;
            _profilePrefersReducedMotion = value;
            NotifyLiveHosts();
        }
    }

    private readonly Dictionary<string, TranslateTransform> _enterTransforms = new(StringComparer.Ordinal);
    private readonly Dictionary<string, (ScaleTransform Scale, TranslateTransform Move, TransformGroup Group)> _flipTransforms = new(StringComparer.Ordinal);
    private readonly Dictionary<string, TranslateTransform> _splitterTransforms = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Rect> _displayRects = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Rect> _displaySplitters = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Rect> _targetScratch = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Rect> _splitTargetScratch = new(StringComparer.Ordinal);
    private MotionPlan? _pendingPlan;
    private MotionPlan? _plan;
    private bool _suppressMotion;
    private bool _hasExplicitReduceMotion;
    private bool _updatingFromExternal;
    private Rect? _dropTargetRect;
    private Rect _dropFrom;
    private bool _dropFresh;
    private Rect _dropShown;

    static WorkspaceHost()
    {
        AffectsArrange<WorkspaceHost>(LayoutProgressProperty, DropProgressProperty);
    }

    /// <summary>True: all workspace transitions are 0 ms (the layout changes at once).</summary>
    public bool ReduceMotion
    {
        get => GetValue(ReduceMotionProperty);
        set => SetValue(ReduceMotionProperty, value);
    }

    /// <summary>
    /// True when this host, the active editor settings profile, or the
    /// <c>ROWL_REDUCE_MOTION</c> environment variable requests reduced motion.
    /// </summary>
    public bool EffectiveReduceMotion =>
        ReduceMotion || _profilePrefersReducedMotion || ReadSystemPreference();

    public double LayoutProgress => GetValue(LayoutProgressProperty);

    public double DropProgress => GetValue(DropProgressProperty);

    /// <summary>A layout motion is running (not yet <see cref="CompleteMotion"/>d / settled).</summary>
    public bool IsMotionRunning => _plan is not null || _pendingPlan is not null;

    private static bool ReadSystemPreference()
    {
        string? value = Environment.GetEnvironmentVariable("ROWL_REDUCE_MOTION");
        return value is not null
            && (value == "1" || value.Equals("true", StringComparison.OrdinalIgnoreCase));
    }

    private static void NotifyLiveHosts()
    {
        WorkspaceHost[] hosts;
        lock (LiveHosts)
        {
            LiveHosts.RemoveAll(r => !r.TryGetTarget(out _));
            hosts = LiveHosts
                .Select(r => r.TryGetTarget(out WorkspaceHost? h) ? h : null)
                .Where(h => h is not null)
                .ToArray()!;
        }
        foreach (WorkspaceHost host in hosts)
        {
            if (Dispatcher.UIThread.CheckAccess())
                host.ApplyExternalReduceMotion();
            else
                Dispatcher.UIThread.Post(host.ApplyExternalReduceMotion);
        }
    }

    private void ApplyExternalReduceMotion()
    {
        if (!_hasExplicitReduceMotion)
        {
            _updatingFromExternal = true;
            try
            {
                SetCurrentValue(ReduceMotionProperty, ReadSystemPreference() || _profilePrefersReducedMotion);
            }
            finally
            {
                _updatingFromExternal = false;
            }
        }
        OnReduceMotionChanged(externalUpdate: true);
    }

    /// <summary>The transitions of the current mode: two eased double transitions, or none when reduced.</summary>
    private Transitions? BuildTransitions() => EffectiveReduceMotion
        ? null
        : new Transitions
        {
            new DoubleTransition { Property = LayoutProgressProperty, Duration = LayoutMotionDuration, Easing = MotionEasing },
            new DoubleTransition { Property = DropProgressProperty, Duration = DropMotionDuration, Easing = MotionEasing },
        };

    private void InitializeMotion()
    {
        lock (LiveHosts)
            LiveHosts.Add(new WeakReference<WorkspaceHost>(this));
        if (!IsSet(ReduceMotionProperty) && (ReadSystemPreference() || _profilePrefersReducedMotion))
        {
            _updatingFromExternal = true;
            try
            {
                SetCurrentValue(ReduceMotionProperty, true);
            }
            finally
            {
                _updatingFromExternal = false;
            }
        }
        Transitions = BuildTransitions();
        // Before any child sees the press (a splitter drag starts from it): settle what is moving.
        AddHandler(PointerPressedEvent, OnMotionPointerPressed, RoutingStrategies.Tunnel, handledEventsToo: true);
    }

    private void OnReduceMotionChanged(bool externalUpdate = false)
    {
        if (!externalUpdate && !_updatingFromExternal)
            _hasExplicitReduceMotion = IsSet(ReduceMotionProperty);
        CompleteMotion();
        Transitions = BuildTransitions();
    }

    /// <summary>
    /// Jumps every running motion to its end now (a pointer press does this, like the
    /// prototype's <c>motion.cancel()</c>; tests use it instead of waiting).
    /// </summary>
    public void CompleteMotion()
    {
        Transitions? running = Transitions;
        Transitions = null; // disposes a running transition
        SetValue(LayoutProgressProperty, 1.0);
        SetValue(DropProgressProperty, 1.0);
        Transitions = running;
        _pendingPlan = null;
        EndLayoutMotion();
        _dropFresh = false;
        _dropFrom = _dropTargetRect ?? default;
        InvalidateArrange();
    }

    /// <summary>Restarts <paramref name="progress"/> at 0 and lets its transition run it to 1 (no transition: it ends at 1 at once).</summary>
    private void RunProgress(StyledProperty<double> progress)
    {
        Transitions? running = Transitions;
        Transitions = null; // cancels the previous run
        SetValue(progress, 0.0);
        Transitions = running;
        SetValue(progress, 1.0);
    }

    // ---- layout motion ----

    private sealed class MotionPlan(Dictionary<string, Rect> panels, Dictionary<string, Rect> splitters)
    {
        public Dictionary<string, Rect> Panels { get; } = panels;

        public Dictionary<string, Rect> Splitters { get; } = splitters;
    }

    /// <summary>Called when the tree is about to change: remember where everything is displayed now.</summary>
    private void CaptureMotion()
    {
        if (EffectiveReduceMotion || _suppressMotion || _pendingPlan is not null || _displayRects.Count == 0)
            return;
        _pendingPlan = new MotionPlan(new Dictionary<string, Rect>(_displayRects, StringComparer.Ordinal),
            new Dictionary<string, Rect>(_displaySplitters, StringComparer.Ordinal));
    }

    private bool PlanChangesLayout(MotionPlan plan, Dictionary<string, Rect> panels, Dictionary<string, Rect> splitters)
    {
        foreach ((string id, Rect target) in panels)
        {
            if (!plan.Panels.TryGetValue(id, out Rect from) || !SameRect(from, target))
                return true;
        }
        foreach ((string id, Rect target) in splitters)
        {
            if (!plan.Splitters.TryGetValue(id, out Rect from) || !SameRect(from, target))
                return true;
        }
        return false;
    }

    private static bool SameRect(Rect a, Rect b) =>
        Math.Abs(a.X - b.X) < 0.5 && Math.Abs(a.Y - b.Y) < 0.5
        && Math.Abs(a.Width - b.Width) < 0.5 && Math.Abs(a.Height - b.Height) < 0.5;

    private static Rect Blend(Rect from, Rect to, double t) => new(
        from.X + (to.X - from.X) * t,
        from.Y + (to.Y - from.Y) * t,
        from.Width + (to.Width - from.Width) * t,
        from.Height + (to.Height - from.Height) * t);

    /// <summary>Hedef dikdörtgende yerleşmiş paneli <paramref name="shown"/> üzerinde gösteren ölçek + kaydırma (sol üst köken).</summary>
    private Transform FlipTransform(string id, Rect shown, Rect target)
    {
        if (!_flipTransforms.TryGetValue(id, out (ScaleTransform Scale, TranslateTransform Move, TransformGroup Group) flip))
        {
            var scale = new ScaleTransform();
            var move = new TranslateTransform();
            flip = (scale, move, new TransformGroup { Children = { scale, move } });
            _flipTransforms[id] = flip;
        }
        flip.Scale.ScaleX = target.Width > 0 ? shown.Width / target.Width : 1;
        flip.Scale.ScaleY = target.Height > 0 ? shown.Height / target.Height : 1;
        flip.Move.X = shown.X - target.X;
        flip.Move.Y = shown.Y - target.Y;
        return flip.Group;
    }

    private void EndLayoutMotion()
    {
        if (_plan is null && _enterTransforms.Count == 0 && _splitterTransforms.Count == 0 && _flipTransforms.Count == 0)
            return;
        _plan = null;
        foreach (WorkspacePanel panel in _panels.Values)
        {
            panel.Opacity = 1;
            panel.RenderTransform = null;
            panel.ZIndex = 0;
        }
        foreach ((string id, SplitParts parts) in _splits)
        {
            parts.Splitter.Opacity = 1;
            parts.Splitter.RenderTransform = null;
        }
        _enterTransforms.Clear();
        _splitterTransforms.Clear();
        _flipTransforms.Clear();
    }

    /// <summary>
    /// Arranges every panel over its final slot — or, while a motion runs, over the
    /// blend of its previous and final rectangle — and the splitters with them.
    /// </summary>
    private void ArrangePanelsAndSplitters()
    {
        Dictionary<string, Rect> targets = _targetScratch;
        targets.Clear();
        foreach ((string id, WorkspacePanel panel) in _panels)
        {
            if (panel.IsVisible && _slots.TryGetValue(id, out Control? slot))
                targets[id] = SlotRect(slot);
        }
        Dictionary<string, Rect> splitTargets = _splitTargetScratch;
        splitTargets.Clear();
        foreach ((string id, SplitParts parts) in _splits)
            splitTargets[id] = SlotRect(parts.Splitter);

        if (_pendingPlan is { } pending)
        {
            _pendingPlan = null;
            if (!EffectiveReduceMotion && PlanChangesLayout(pending, targets, splitTargets))
            {
                _plan = pending;
                RunProgress(LayoutProgressProperty);
            }
        }

        double t = 1;
        if (_plan is not null)
        {
            t = LayoutProgress;
            if (t >= 1)
                EndLayoutMotion();
        }
        MotionPlan? plan = _plan;

        _displayRects.Clear();
        _displaySplitters.Clear();
        foreach ((string id, WorkspacePanel panel) in _panels)
        {
            if (!targets.TryGetValue(id, out Rect target))
            {
                _panelRects.Remove(id);
                panel.Arrange(default);
                continue;
            }
            _panelRects[id] = target;
            Rect shown = target;
            if (plan is not null)
            {
                if (plan.Panels.TryGetValue(id, out Rect from))
                {
                    // Faz 10: panel yalnız son yerine yerleşir; ara kareler bir görsel dönüşümle
                    // (kaydır + ölçekle) çizilir. Ara boyutlarda yeniden ölçmek, Denetçi gibi ağır
                    // panellerde kare başına 30–120 ms tutup editörü donduruyordu.
                    shown = Blend(from, target, t);
                    panel.Opacity = 1;
                    panel.RenderTransformOrigin = RelativePoint.TopLeft;
                    panel.RenderTransform = FlipTransform(id, shown, target);
                    panel.ZIndex = 1;
                }
                else
                {
                    // Entering: fades in and rises (prototype: panel-content opacity 0 → 1, translateY 6px → 0);
                    // painted below its neighbours so they uncover it while they move out of its way.
                    panel.Opacity = t;
                    if (!_enterTransforms.TryGetValue(id, out TranslateTransform? rise))
                        _enterTransforms[id] = rise = new TranslateTransform();
                    rise.Y = EnterRise * (1 - t);
                    panel.RenderTransform = rise;
                    panel.ZIndex = 0;
                }
            }
            _displayRects[id] = shown;
            panel.Measure(target.Size);
            panel.Arrange(target);
        }

        foreach ((string id, SplitParts parts) in _splits)
        {
            Rect target = splitTargets[id];
            Rect shown = target;
            if (plan is not null)
            {
                if (plan.Splitters.TryGetValue(id, out Rect from))
                {
                    if (!_splitterTransforms.TryGetValue(id, out TranslateTransform? slide))
                        _splitterTransforms[id] = slide = new TranslateTransform();
                    slide.X = (from.X - target.X) * (1 - t);
                    slide.Y = (from.Y - target.Y) * (1 - t);
                    parts.Splitter.RenderTransform = slide;
                    parts.Splitter.Opacity = 1;
                    shown = new Rect(target.X + slide.X, target.Y + slide.Y, target.Width, target.Height);
                }
                else
                {
                    parts.Splitter.RenderTransform = null;
                    parts.Splitter.Opacity = t;
                }
            }
            _displaySplitters[id] = shown;
        }
    }

    // ---- drop highlight motion ----

    /// <summary>The highlight's target changed: ease from the old shape to the new one (a new highlight fades in).</summary>
    private void NoteDropTarget(Rect? target)
    {
        if (target is null)
        {
            _dropTargetRect = null;
            _dropFresh = false;
            return;
        }
        if (_dropTargetRect is { } current && SameRect(current, target.Value))
            return;
        bool fresh = _dropTargetRect is null;
        _dropFrom = fresh ? target.Value : _dropShown;
        _dropFresh = fresh;
        _dropTargetRect = target;
        if (EffectiveReduceMotion)
        {
            _dropFresh = false;
            _dropFrom = target.Value;
        }
        RunProgress(DropProgressProperty);
    }

    /// <summary>Highlight rectangle and opacity for this frame.</summary>
    private Rect ShownDropRect(Rect target)
    {
        double t = DropProgress;
        _dropShown = t >= 1 ? target : Blend(_dropFrom, target, t);
        _dropPreview.Opacity = _dropFresh ? t : 1;
        return _dropShown;
    }

    private void OnMotionPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (IsMotionRunning)
            CompleteMotion();
    }

    /// <summary>For tests and diagnostics: panels currently entering (not in the previous layout).</summary>
    internal IReadOnlyCollection<string> EnteringPanels =>
        _plan is null ? [] : _panelRects.Keys.Where(id => !_plan.Panels.ContainsKey(id)).ToArray();

    /// <summary>The rectangle a panel is displayed at right now (host layout space); the final one when settled.</summary>
    public Rect GetDisplayedPanelRect(string panelId) =>
        _displayRects.TryGetValue(panelId, out Rect rect) ? rect : default;
}
