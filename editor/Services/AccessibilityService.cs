using System;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 3 Dilim 5 — managed side of the accessibility contract. Owns
/// text-scale stepping and the profile→engine application
/// so <c>PlayerViewModel</c> stays a thin delegation and the rules remain
/// fully unit-testable against fake engines.
/// </summary>
public static class AccessibilityService
{
    /// <summary>Mirrors ROWL_ENGINE_CAPABILITY_ACCESSIBILITY (1 &lt;&lt; 10).</summary>
    public const ulong NativeCapabilityAccessibility = 1024UL;

    /// <summary>
    /// Applies the profile accessibility settings to the engine. Null
    /// arguments are ignored (fail closed); the scale is snapped onto the
    /// offered steps before crossing the seam.
    /// </summary>
    public static void ApplyToEngine(IPlayerEngine? engine, PlayerProfile? profile)
    {
        if (engine is null || profile is null)
            return;
        try
        {
            engine.SetTextScale(PlayerProfile.SnapTextScale(profile.TextScale));
            engine.SetHighContrast(profile.HighContrast);
            engine.SetReducedMotion(profile.ReducedMotion);
        }
        catch (Exception)
        {
            // A dead engine must never break preference saving.
        }
    }

    /// <summary>Advances to the next offered text-scale step, wrapping around.</summary>
    public static float CycleTextScale(float current)
    {
        var steps = PlayerProfile.AllowedTextScales;
        float snapped = PlayerProfile.SnapTextScale(current);
        for (int i = 0; i < steps.Count; i++)
        {
            if (Math.Abs(steps[i] - snapped) < 0.001f)
                return steps[(i + 1) % steps.Count];
        }
        return steps[0];
    }
}
