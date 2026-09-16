using System;
using System.Collections.Generic;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.ViewModels.Player;

/// <summary>
/// Production <see cref="IPlayerEngine"/> over the shared editor
/// <see cref="EngineHost"/>. Pure delegation — zero player logic lives
/// here; <see cref="PlayerViewModel"/> owns every decision.
/// </summary>
public sealed class EngineHostPlayerAdapter : IPlayerEngine
{
    private readonly EngineHost _host;

    public EngineHostPlayerAdapter(EngineHost host)
    {
        _host = host ?? throw new ArgumentNullException(nameof(host));
    }

    public bool IsAvailable => _host.IsInitialized;
    public void SetPlayState(bool playing) => _host.SetPlayState(playing);
    public void SetPaused(bool paused) => _host.SetPaused(paused);
    public void ResetToStartNode() => _host.ResetToStartNode();
    public void Step(float dt) => _host.Step(dt);
    public void AdvanceNode(uint choiceIndex) => _host.AdvanceNode(choiceIndex);

    public bool SelectChoice(string optionId) => _host.SelectChoice(optionId);

    public IReadOnlyList<string> GetActiveDialogueContentIds() => _host.GetActiveDialogueContentIds();
    public bool HasChoices() => _host.HasChoices();
    public IReadOnlyList<string> GetChoiceLabels() => _host.GetChoiceLabels();
    public IReadOnlyList<string> GetChoiceOptionIds() => _host.GetChoiceOptionIds();
    public string Speaker => _host.GetSpeaker();
    public string Dialogue => _host.GetDialogue();

    public IReadOnlyList<DialogueHistoryEntry> History => _host.DialogueHistory;

    public void RefreshHistory() => _host.RefreshDialogueHistory();

    public bool SaveSlot(int index) => _host.SaveGameSlot(index);

    public bool LoadSlot(int index)
    {
        bool ok = _host.LoadGameSlot(index);
        if (ok)
            _host.RefreshDialogueHistory();
        return ok;
    }

    public bool HasSlot(int index) => _host.HasSaveSlot(index);
    public bool DeleteSlot(int index) => _host.DeleteSaveSlot(index);
    public SaveSlotMetadata? GetSlotMetadata(int index) => _host.GetSaveSlotMetadata(index);
    public void SetMasterVolume(float value) => _host.SetMasterVolume(value);
    public void SetBgmVolume(float value) => _host.SetBgmVolume(value);
    public void SetVoiceVolume(float value) => _host.SetVoiceVolume(value);
    public void SetSfxVolume(float value) => _host.SetSfxVolume(value);

    // Faz 5 Dilim 2 fix turu 1 — EngineHost SIFIR-DIFF: bu iki bus için
    // EngineHost'ta wrapper YOKTUR; adaptör canlı handle'a mevcut public
    // yüzeyden (IsInitialized + Handle) erişip NativeBridge delegesini
    // doğrudan çağırır. Ölü handle no-op, clamp [0,1], non-finite ignore
    // (AudioMixerService.AcceptBedVolume semantiği).
    public void SetAmbienceVolume(float value) =>
        ForwardVolume(NativeBridge.RowlEngine_SetAmbienceVolume, value);

    public void SetUiVolume(float value) =>
        ForwardVolume(NativeBridge.RowlEngine_SetUiVolume, value);

    // Faz 5 Dilim 2 fix turu 1 — global mixer config aynı fail-closed
    // yoldan akar: eğri 0/1 dışı ignore, derinlik [1,16] clamp.
    public void SetFadeCurve(int curve)
    {
        if (curve != 0 && curve != 1)
            return;
        ForwardSetting(NativeBridge.RowlEngine_SetFadeCurve, curve);
    }

    public void SetSfxPoolDepth(int depth) =>
        ForwardSetting(NativeBridge.RowlEngine_SetSfxPoolDepth, Math.Clamp(depth, 1, 16));

    private void ForwardVolume(Action<IntPtr, float> write, float value)
    {
        if (!float.IsFinite(value) || !_host.IsInitialized)
            return;
        IntPtr handle = _host.Handle;
        if (handle == IntPtr.Zero)
            return;
        try
        {
            write(handle, Math.Clamp(value, 0.0f, 1.0f));
        }
        catch (Exception)
        {
            // Ölü handle / kapalı native: sessiz fail-closed.
        }
    }

    private void ForwardSetting(Action<IntPtr, int> write, int value)
    {
        if (!_host.IsInitialized)
            return;
        IntPtr handle = _host.Handle;
        if (handle == IntPtr.Zero)
            return;
        try
        {
            write(handle, value);
        }
        catch (Exception)
        {
            // Ölü handle / kapalı native: sessiz fail-closed.
        }
    }
    public void SetTextScale(float value) => _host.SetTextScale(value);
    public void SetHighContrast(bool enabled) => _host.SetHighContrast(enabled);
    public void SetReducedMotion(bool enabled) => _host.SetReducedMotion(enabled);
}
