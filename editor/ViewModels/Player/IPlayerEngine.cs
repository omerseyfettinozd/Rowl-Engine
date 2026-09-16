using System.Collections.Generic;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.ViewModels.Player;

/// <summary>
/// Minimal engine surface the Faz 2 player shell needs. Implemented by
/// <see cref="EngineHostPlayerAdapter"/> for production and by fakes in
/// tests, so <see cref="PlayerViewModel"/> never touches native code
/// directly and stays fully unit-testable.
/// </summary>
public interface IPlayerEngine
{
    bool IsAvailable { get; }
    void SetPlayState(bool playing);
    void SetPaused(bool paused);
    void ResetToStartNode();
    void Step(float dt);
    void AdvanceNode(uint choiceIndex);
    bool SelectChoice(string optionId);
    IReadOnlyList<string> GetActiveDialogueContentIds();
    bool HasChoices();
    IReadOnlyList<string> GetChoiceLabels();
    IReadOnlyList<string> GetChoiceOptionIds();
    string Speaker { get; }
    string Dialogue { get; }
    IReadOnlyList<DialogueHistoryEntry> History { get; }
    void RefreshHistory();
    bool SaveSlot(int index);
    bool LoadSlot(int index);
    bool HasSlot(int index);
    bool DeleteSlot(int index);
    SaveSlotMetadata? GetSlotMetadata(int index);
    void SetMasterVolume(float value);
    void SetBgmVolume(float value);
    void SetVoiceVolume(float value);
    void SetSfxVolume(float value);

    /// <summary>
    /// Faz 5 Dilim 2 — mixer masası dikişi: ambience + arayüz bus
    /// hacimleri. Varsayılan no-op'tur (fail-closed); prod adaptör
    /// <see cref="EngineHostPlayerAdapter"/> canlı handle üzerinden
    /// NativeBridge'e forward eder (EngineHost diffsiz).
    /// </summary>
    void SetAmbienceVolume(float value) { }
    void SetUiVolume(float value) { }

    /// <summary>
    /// Faz 5 Dilim 2 fix turu 1 — global mixer config: fade eğrisi
    /// (0 = Linear, 1 = EqualPower; diğer değerler ignore) ve SFX
    /// havuz derinliği ([1,16] clamp). Varsayılan no-op'tur
    /// (fail-closed); prod adaptör NativeBridge'e forward eder.
    /// </summary>
    void SetFadeCurve(int curve) { }
    void SetSfxPoolDepth(int depth) { }

    /// <summary>
    /// Faz 5 Dilim 3 — katmanlı karakter dikişi: slot asset/opaklık/
    /// görünürlük, expression preset kaydı + uygulaması ve son karakter
    /// tanısı. Varsayılan no-op'tur (fail-closed); prod adaptör
    /// <see cref="EngineHostPlayerAdapter"/> canlı handle üzerinden
    /// NativeBridge'e forward eder (EngineHost diffsiz). Bilinmeyen slot,
    /// bozuk asset/preset adı ve aşırı girdi forward edilmeden yoksayılır
    /// (native reddi aynası).
    /// </summary>
    void SetCharacterSlotAsset(string slot, string asset) { }
    void SetCharacterSlotOpacity(string slot, float opacity) { }
    void SetCharacterSlotVisible(string slot, bool visible) { }
    void RegisterCharacterPreset(string name, string expressionJson) { }
    void ApplyCharacterExpression(string name) { }
    string GetLastCharacterError() => string.Empty;

    /// <summary>
    /// Faz 5 Dilim 4 — bütçeli prefetch + chapter penceresi dikişi: chapter
    /// index/dosya besleme, resident yükleme/boşaltma, chapter-sınır sorgusu,
    /// prefetch tetikleme + pump ve ilerleme/yüklü-chapter JSON okumaları.
    /// Varsayılan no-op'tur (fail-closed); prod adaptör
    /// <see cref="EngineHostPlayerAdapter"/> canlı handle üzerinden
    /// NativeBridge'e forward eder (EngineHost diffsiz). Boş chapter-id
    /// prefetch'te aktif chapter demektir; geçersiz id ve boş JSON forward
    /// edilmeden yoksayılır (native reddi aynası). Sözleşme
    /// docs/PREFETCH_AND_CHAPTERS_CONTRACT.md'dedir.
    /// </summary>
    void LoadChapterIndexJson(string indexJson) { }
    void AppendChapterFileJson(string chapterJson) { }
    void LoadChapter(string chapterId) { }
    void UnloadChapter(string chapterId) { }
    string GetLoadedChaptersJson() => string.Empty;
    bool IsChapterBoundaryNode(ulong nodeId) => false;
    string GetCurrentChapterId() => string.Empty;
    void PrefetchChapterAssets(string? chapterId, ulong budgetBytes) { }
    int PumpPrefetch(float maxMilliseconds) => 0;
    string GetPrefetchProgressJson() => string.Empty;
    void SetTextScale(float value);
    void SetHighContrast(bool enabled);
    void SetReducedMotion(bool enabled);
}
