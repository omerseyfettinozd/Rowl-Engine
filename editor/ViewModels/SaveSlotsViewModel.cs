using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace RowlEngine.Editor.ViewModels;

public sealed partial class SaveSlotEntry : ObservableObject
{
    public int Index { get; }
    [ObservableProperty] private bool _isOccupied;
    public string Label => $"Slot {Index + 1}";
    public string State => IsOccupied ? "Dolu" : "Boş";
    public SaveSlotEntry(int index, bool isOccupied) { Index = index; IsOccupied = isOccupied; }
}

/// <summary>Project-local runtime save slots exposed by the embedded player.</summary>
public sealed partial class SaveSlotsViewModel : ViewModelBase
{
    private readonly MainWindowViewModel _main;
    public ObservableCollection<SaveSlotEntry> Slots { get; } = new();
    public SaveSlotsViewModel(MainWindowViewModel main) { _main = main; Refresh(); }

    public void Refresh()
    {
        Slots.Clear();
        for (int index = 0; index < _main.ProjectRuntimeSettings.SaveSlotCount; index++)
            Slots.Add(new SaveSlotEntry(index, _main.EngineHost.HasSaveSlot(index)));
    }

    [RelayCommand]
    private void Save(SaveSlotEntry? slot)
    {
        if (slot is null) return;
        if (_main.EngineHost.SaveGameSlot(slot.Index)) { _main.AppendLog($"💾 Oyun slotu {slot.Index + 1} kaydedildi."); Refresh(); }
        else { _main.AppendLog($"⚠️ Oyun slotu {slot.Index + 1} kaydedilemedi."); _main.CheckEngineDiagnostics(); }
    }

    [RelayCommand]
    private void Load(SaveSlotEntry? slot)
    {
        if (slot is null || !slot.IsOccupied) return;
        if (_main.EngineHost.LoadGameSlot(slot.Index)) { _main.SyncEditorToRuntimeNode(); _main.BacklogViewModel.Refresh(); _main.AppendLog($"📂 Oyun slotu {slot.Index + 1} yüklendi."); }
        else { _main.AppendLog($"⚠️ Oyun slotu {slot.Index + 1} yüklenemedi; çalışan sahne korundu."); _main.CheckEngineDiagnostics(); }
    }

    [RelayCommand]
    private async System.Threading.Tasks.Task DeleteAsync(SaveSlotEntry? slot)
    {
        if (slot is null || !slot.IsOccupied || !await _main.ConfirmDeleteSaveSlotAsync(slot.Index)) return;
        if (_main.EngineHost.DeleteSaveSlot(slot.Index)) { _main.AppendLog($"🗑️ Oyun slotu {slot.Index + 1} silindi."); Refresh(); }
        else { _main.AppendLog($"⚠️ Oyun slotu {slot.Index + 1} silinemedi."); _main.CheckEngineDiagnostics(); }
    }
}
