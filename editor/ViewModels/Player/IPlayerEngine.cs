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
    void SetTextScale(float value);
    void SetHighContrast(bool enabled);
    void SetReducedMotion(bool enabled);
}
