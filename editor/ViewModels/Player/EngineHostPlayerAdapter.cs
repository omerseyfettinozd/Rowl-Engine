using System;
using System.Collections.Generic;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

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

    // B5 trio-1 — worker-dispatch göçü: Faz 5 Dilim 2'deki direkt-forward
    // canlıda fail-closed-suskunluktu (B5-kırmızısı kanıtlı); marshal artık
    // owner-thread'den akar. Semantik birebir aynı (clamp/guard worker'da).
    public void SetAmbienceVolume(float value) => ForwardWorker(w => w.SetAmbienceVolume(value));

    public void SetUiVolume(float value) => ForwardWorker(w => w.SetUiVolume(value));

    // B5 trio-1 — global mixer config aynı worker-yoldan akar: eğri 0/1
    // dışı ignore, derinlik [1,16] clamp (guard worker'da).
    public void SetFadeCurve(int curve) => ForwardWorker(w => w.SetFadeCurve(curve));

    public void SetSfxPoolDepth(int depth) => ForwardWorker(w => w.SetSfxPoolDepth(depth));

    // B5 trio-2 — worker-dispatch göçü: servis-ayna pre-check'ler adapter'da
    // kalır (domain-önkoşul; ölü-handle/no-throw yolları değişmez), marshal +
    // clamp worker'dadır. Sözleşme docs/CHARACTER_LAYERS_CONTRACT.md'dedir.
    public void SetCharacterSlotAsset(string slot, string asset)
    {
        if (!CharacterLayersService.IsKnownSlot(slot) ||
            !CharacterLayersService.IsAssetPathSyntaxValid(asset))
            return;
        ForwardWorker(w => w.SetCharacterSlotAsset(slot, asset ?? string.Empty));
    }

    public void SetCharacterSlotOpacity(string slot, float opacity)
    {
        if (!CharacterLayersService.IsKnownSlot(slot) || !float.IsFinite(opacity))
            return;
        ForwardWorker(w => w.SetCharacterSlotOpacity(slot, opacity));
    }

    public void SetCharacterSlotVisible(string slot, bool visible)
    {
        if (!CharacterLayersService.IsKnownSlot(slot))
            return;
        ForwardWorker(w => w.SetCharacterSlotVisible(slot, visible));
    }

    public void RegisterCharacterPreset(string name, string expressionJson)
    {
        if (!CharacterLayersService.IsPresetNameValid(name) ||
            !CharacterLayersService.TryParsePreset(expressionJson, out _))
            return;
        ForwardWorker(w => w.RegisterCharacterPreset(name, expressionJson));
    }

    public void ApplyCharacterExpression(string name)
    {
        if (!CharacterLayersService.IsPresetNameValid(name))
            return;
        ForwardWorker(w => w.ApplyCharacterExpression(name));
    }

    public string GetLastCharacterError() =>
        ForwardWorker(w => w.GetLastCharacterError(), string.Empty);

    // B5 — LiveHandle kalktı (20/20 worker-göçü; direkt-handle kullanımı sıfır).

    // B5 trio-3 — worker-dispatch göçü: servis-ayna pre-check'ler
    // (boş-JSON, chapter-id, bütçe/pump clamp) adapter'da aynen kalır,
    // marshal owner-thread'den akar. Sözleşme
    // docs/PREFETCH_AND_CHAPTERS_CONTRACT.md'dedir.
    public void LoadChapterIndexJson(string indexJson)
    {
        if (string.IsNullOrWhiteSpace(indexJson))
            return;
        ForwardWorker(w => w.LoadChapterIndexJson(indexJson));
    }

    public void AppendChapterFileJson(string chapterJson)
    {
        if (string.IsNullOrWhiteSpace(chapterJson))
            return;
        ForwardWorker(w => w.AppendChapterFileJson(chapterJson));
    }

    public void LoadChapter(string chapterId)
    {
        if (!PrefetchChaptersService.IsChapterIdValid(chapterId))
            return;
        ForwardWorker(w => w.LoadChapter(chapterId));
    }

    public void UnloadChapter(string chapterId)
    {
        if (!PrefetchChaptersService.IsChapterIdValid(chapterId))
            return;
        ForwardWorker(w => w.UnloadChapter(chapterId));
    }

    public string GetLoadedChaptersJson() =>
        ForwardWorker(w => w.GetLoadedChaptersJson(), string.Empty);

    public string GetPrefetchProgressJson() =>
        ForwardWorker(w => w.GetPrefetchProgressJson(), string.Empty);

    public string GetCurrentChapterId() =>
        ForwardWorker(w => w.GetCurrentChapterId(), string.Empty);

    public bool IsChapterBoundaryNode(ulong nodeId) =>
        ForwardWorker(w => w.IsChapterBoundaryNode(nodeId), false);

    public void PrefetchChapterAssets(string? chapterId, ulong budgetBytes)
    {
        if (!string.IsNullOrEmpty(chapterId) &&
            !PrefetchChaptersService.IsChapterIdValid(chapterId))
            return;
        string? id = string.IsNullOrEmpty(chapterId) ? null : chapterId;
        ulong budget = PrefetchChaptersService.ClampBudgetBytes(budgetBytes);
        ForwardWorker(w => w.PrefetchChapterAssets(id, budget));
    }

    public int PumpPrefetch(float maxMilliseconds)
    {
        double clamped = PrefetchChaptersService.ClampPumpMilliseconds(maxMilliseconds);
        return ForwardWorker(w => w.PumpPrefetch((float)clamped), 0);
    }

    // B5 — worker-forward omurgası: marshal owner-thread'den akar;
    // ölü/kapalı worker sessiz fail-closed (Faz 5 dead-handle testleri
    // aynen geçer). Trio-1 ile ForwardVolume/ForwardSetting kalktı.
    private void ForwardWorker(Action<OffscreenRuntimeWorker> write)
    {
        OffscreenRuntimeWorker? runtime = _host.Runtime;
        if (runtime == null)
            return;
        try
        {
            write(runtime);
        }
        catch (Exception)
        {
            // Ölü worker / kapalı native: sessiz fail-closed.
        }
    }

    private T ForwardWorker<T>(Func<OffscreenRuntimeWorker, T> read, T fallback)
    {
        OffscreenRuntimeWorker? runtime = _host.Runtime;
        if (runtime == null)
            return fallback;
        try
        {
            return read(runtime);
        }
        catch (Exception)
        {
            return fallback;
        }
    }
    public void SetTextScale(float value) => _host.SetTextScale(value);
    public void SetHighContrast(bool enabled) => _host.SetHighContrast(enabled);
    public void SetReducedMotion(bool enabled) => _host.SetReducedMotion(enabled);
}
