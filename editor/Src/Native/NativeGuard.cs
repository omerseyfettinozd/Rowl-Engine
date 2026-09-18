using System;

namespace RowlEngine.Editor.Native
{
    // B4: C# öndoğrulama/clamp bekçisi. Native setter'ların bir kısmı
    // NaN-deliğine sahiptir (Engine::setTextSpeedMultiplier /
    // setAutoAdvanceDelayOffset çıplak std::clamp kullanır — clamp(NaN)
    // NaN'i koruyup alana sızdırır; engine.cpp:1777-1782). Faz 4.5 D3
    // yasağı native'e dokunmayı yasakladığı için delik burada, köprü
    // öncesinde kapatılır: non-finite girdi hiç forward edilmez (mevcut
    // değer korunur — native volume'ların "ignore" semantiğiyle aynı),
    // finite girdi menzile clamp'lenir. Saf statik — engine'siz
    // ünit-test edilir; worker-dispatch/affinity sözleşmesine dokunmaz.
    internal static class NativeGuard
    {
        internal static bool TryClamp01(float value, out float clamped)
        {
            if (!float.IsFinite(value))
            {
                clamped = default;
                return false;
            }
            clamped = Math.Clamp(value, 0.0f, 1.0f);
            return true;
        }

        internal static bool TryClamp(float value, float min, float max, out float clamped)
        {
            if (!float.IsFinite(value))
            {
                clamped = default;
                return false;
            }
            clamped = Math.Clamp(value, min, max);
            return true;
        }
    }
}
