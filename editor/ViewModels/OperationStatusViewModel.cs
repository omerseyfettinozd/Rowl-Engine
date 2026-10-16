using System;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels
{
    public enum OperationKind { Save, SaveAs, Export }
    public enum OperationState { Idle, Running, Succeeded, Failed }

    // Kaydet / Farklı kaydet / Dışa aktar sonucunu taşır.
    public partial class OperationStatusViewModel : ObservableObject
    {
        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsVisible), nameof(IsRunning), nameof(IsSucceeded), nameof(IsFailed))]
        [NotifyCanExecuteChangedFor(nameof(ShowLogCommand))]
        private OperationState _state = OperationState.Idle;

        [ObservableProperty]
        private OperationKind _kind = OperationKind.Save;

        [ObservableProperty]
        private string _title = string.Empty;

        [ObservableProperty]
        private string _detail = string.Empty;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(HasOutputPath))]
        [NotifyCanExecuteChangedFor(nameof(OpenFolderCommand))]
        private string? _outputPath;

        // Artan deneme sayacı; gecikmiş sonuç ezmesin.
        public int Attempt { get; private set; }

        public event Action<string>? OpenFolderRequested;
        public event Action? ShowLogRequested;

        public bool IsVisible => State != OperationState.Idle;
        public bool IsRunning => State == OperationState.Running;
        public bool IsSucceeded => State == OperationState.Succeeded;
        public bool IsFailed => State == OperationState.Failed;
        public bool HasOutputPath => !string.IsNullOrWhiteSpace(OutputPath);

        private static string Key(OperationKind kind, string suffix) => $"Operation.{kind}.{suffix}";

        public void Begin(OperationKind kind)
        {
            Attempt++;
            Kind = kind;
            State = OperationState.Running;
            Title = EditorStrings.T(Key(kind, "Running"));
            Detail = string.Empty;
            OutputPath = null;
        }

        public void Succeed(OperationKind kind, string? outputPath = null, string? detail = null, int? attempt = null)
        {
            if (attempt.HasValue && attempt.Value != Attempt)
                return;
            Kind = kind;
            State = OperationState.Succeeded;
            Title = EditorStrings.T(Key(kind, "Succeeded"));
            OutputPath = outputPath;
            Detail = detail ?? outputPath ?? string.Empty;
        }

        public void Fail(OperationKind kind, string reason, int? attempt = null)
        {
            if (attempt.HasValue && attempt.Value != Attempt)
                return;
            Kind = kind;
            State = OperationState.Failed;
            Title = EditorStrings.T(Key(kind, "Failed"));
            Detail = string.IsNullOrWhiteSpace(reason)
                ? EditorStrings.T("Operation.UnknownError")
                : reason;
            // Başarısız işlem çıktı konumu göstermez.
            OutputPath = null;
        }

        public void Cancelled(OperationKind kind)
        {
            Kind = kind;
            State = OperationState.Idle;
            Title = string.Empty;
            Detail = string.Empty;
            OutputPath = null;
        }

        [RelayCommand]
        public void Dismiss()
        {
            State = OperationState.Idle;
        }

        [RelayCommand(CanExecute = nameof(HasOutputPath))]
        public void OpenFolder()
        {
            if (OutputPath != null)
                OpenFolderRequested?.Invoke(OutputPath);
        }

        [RelayCommand(CanExecute = nameof(IsFailed))]
        public void ShowLog()
        {
            if (IsFailed)
                ShowLogRequested?.Invoke();
        }
    }
}
