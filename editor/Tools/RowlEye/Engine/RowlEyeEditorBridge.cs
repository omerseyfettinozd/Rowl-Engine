using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using Avalonia.Threading;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tools.RowlEye.Engine;

/// <summary>
/// Status DTO representing the active state and depth of the Undo/Redo stack.
/// </summary>
public sealed record EditorUndoRedoStatusDto(
    [property: JsonPropertyName("canUndo")] bool CanUndo,
    [property: JsonPropertyName("canRedo")] bool CanRedo,
    [property: JsonPropertyName("undoDescription")] string UndoDescription,
    [property: JsonPropertyName("redoDescription")] string RedoDescription
);

/// <summary>
/// DTO representing an action injected into the editor UI.
/// </summary>
public sealed record EditorInputActionDto(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("id")] string? Id = null,
    [property: JsonPropertyName("text")] string? Text = null,
    [property: JsonPropertyName("key")] string? Key = null,
    [property: JsonPropertyName("ctrl")] bool? Ctrl = null,
    [property: JsonPropertyName("shift")] bool? Shift = null,
    [property: JsonPropertyName("alt")] bool? Alt = null,
    [property: JsonPropertyName("isDown")] bool? IsDown = null,
    [property: JsonPropertyName("asset")] string? Asset = null,
    [property: JsonPropertyName("x")] float? X = null,
    [property: JsonPropertyName("y")] float? Y = null
);

/// <summary>
/// Result of an injected editor action.
/// </summary>
public sealed record EditorActionResultDto(
    [property: JsonPropertyName("success")] bool Success,
    [property: JsonPropertyName("actionType")] string ActionType,
    [property: JsonPropertyName("message")] string? Message
);

/// <summary>
/// Pre-queued answer for modal dialogs (confirmation, file pickers) to prevent blocking AI test agents.
/// </summary>
public sealed record DialogAnswerDto(
    [property: JsonPropertyName("dialogType")] string DialogType,
    [property: JsonPropertyName("approve")] bool Approve,
    [property: JsonPropertyName("selectedPath")] string? SelectedPath = null,
    [property: JsonPropertyName("customData")] string? CustomData = null
);

/// <summary>
/// Service managing synthetic UI events, keyboard shortcuts, and Undo/Redo operations for AI models.
/// </summary>
public sealed class RowlEyeEditorBridge
{
    private static readonly Lazy<RowlEyeEditorBridge> InstanceLazy = new(() => new RowlEyeEditorBridge());
    public static RowlEyeEditorBridge Instance => InstanceLazy.Value;

    private readonly object _lock = new();
    private readonly Queue<DialogAnswerDto> _dialogAnswerQueue = new();
    private readonly object _dialogLock = new();

    /// <summary>
    /// When true, unhandled modal dialogs are auto-approved in headless/AI test mode.
    /// </summary>
    public bool AutoApproveDialogs { get; set; } = true;

    /// <summary>
    /// Enqueues a pre-configured answer for an upcoming modal dialog.
    /// </summary>
    public void QueueDialogAnswer(DialogAnswerDto answer)
    {
        if (answer == null) return;
        lock (_dialogLock)
        {
            _dialogAnswerQueue.Enqueue(answer);
        }
    }

    /// <summary>
    /// Attempts to dequeue a pending answer for the requested dialog type.
    /// </summary>
    public bool TryGetPendingDialogAnswer(string dialogType, out DialogAnswerDto? answer)
    {
        lock (_dialogLock)
        {
            if (_dialogAnswerQueue.Count > 0)
            {
                answer = _dialogAnswerQueue.Dequeue();
                return true;
            }
        }
        answer = null;
        return false;
    }

    /// <summary>
    /// Retrieves all currently queued dialog answers.
    /// </summary>
    public IReadOnlyList<DialogAnswerDto> GetPendingDialogAnswers()
    {
        lock (_dialogLock)
        {
            return _dialogAnswerQueue.ToArray();
        }
    }

    /// <summary>
    /// Clears any queued dialog answers.
    /// </summary>
    public void ClearDialogAnswers()
    {
        lock (_dialogLock)
        {
            _dialogAnswerQueue.Clear();
        }
    }

    /// <summary>
    /// Optional command dispatcher hooked from the active editor view or ViewModel.
    /// </summary>
    public Func<string, object?, Task<bool>>? CommandDispatcher { get; set; }

    /// <summary>
    /// Optional text input dispatcher hooked from the focused control.
    /// </summary>
    public Func<string, bool>? TextInputDispatcher { get; set; }

    /// <summary>
    /// Optional drag-drop dispatcher hooked from the active visual panel.
    /// </summary>
    public Func<string, float, float, bool>? DragDropDispatcher { get; set; }

    /// <summary>
    /// Gets the current status of the editor's Undo/Redo stack.
    /// </summary>
    public EditorUndoRedoStatusDto GetUndoRedoStatus()
    {
        var undoSvc = UndoRedoService.Instance;
        return new EditorUndoRedoStatusDto(
            CanUndo: undoSvc.CanUndo,
            CanRedo: undoSvc.CanRedo,
            UndoDescription: undoSvc.UndoDescription,
            RedoDescription: undoSvc.RedoDescription
        );
    }

    /// <summary>
    /// Gets the current status of the editor's Undo/Redo stack asynchronously on the UI thread.
    /// </summary>
    public async Task<EditorUndoRedoStatusDto> GetUndoRedoStatusAsync()
    {
        if (Dispatcher.UIThread.CheckAccess())
        {
            return GetUndoRedoStatus();
        }

        return await Dispatcher.UIThread.InvokeAsync(GetUndoRedoStatus);
    }

    /// <summary>
    /// Executes an Undo operation asynchronously, ensuring atomic UI thread execution.
    /// </summary>
    public async Task<bool> UndoAsync()
    {
        var undoSvc = UndoRedoService.Instance;
        if (!undoSvc.CanUndo) return false;

        if (Dispatcher.UIThread.CheckAccess())
        {
            undoSvc.Undo();
            return true;
        }

        return await Dispatcher.UIThread.InvokeAsync(() =>
        {
            if (!undoSvc.CanUndo) return false;
            undoSvc.Undo();
            return true;
        });
    }

    /// <summary>
    /// Executes an Undo operation synchronously.
    /// </summary>
    public bool Undo()
    {
        var undoSvc = UndoRedoService.Instance;
        if (!undoSvc.CanUndo) return false;

        if (Dispatcher.UIThread.CheckAccess())
        {
            undoSvc.Undo();
            return true;
        }

        return Dispatcher.UIThread.InvokeAsync(() =>
        {
            if (!undoSvc.CanUndo) return false;
            undoSvc.Undo();
            return true;
        }).GetAwaiter().GetResult();
    }

    /// <summary>
    /// Executes a Redo operation asynchronously, ensuring atomic UI thread execution.
    /// </summary>
    public async Task<bool> RedoAsync()
    {
        var undoSvc = UndoRedoService.Instance;
        if (!undoSvc.CanRedo) return false;

        if (Dispatcher.UIThread.CheckAccess())
        {
            undoSvc.Redo();
            return true;
        }

        return await Dispatcher.UIThread.InvokeAsync(() =>
        {
            if (!undoSvc.CanRedo) return false;
            undoSvc.Redo();
            return true;
        });
    }

    /// <summary>
    /// Executes a Redo operation synchronously.
    /// </summary>
    public bool Redo()
    {
        var undoSvc = UndoRedoService.Instance;
        if (!undoSvc.CanRedo) return false;

        if (Dispatcher.UIThread.CheckAccess())
        {
            undoSvc.Redo();
            return true;
        }

        return Dispatcher.UIThread.InvokeAsync(() =>
        {
            if (!undoSvc.CanRedo) return false;
            undoSvc.Redo();
            return true;
        }).GetAwaiter().GetResult();
    }

    /// <summary>
    /// Injects a batch of synthetic editor actions asynchronously.
    /// Rejects unimplemented actions or missing targets honestly without false greens.
    /// </summary>
    public async Task<IReadOnlyList<EditorActionResultDto>> InjectActionsAsync(IReadOnlyList<EditorInputActionDto> actions)
    {
        var results = new List<EditorActionResultDto>();
        if (actions == null || actions.Count == 0) return results;

        foreach (var action in actions)
        {
            string t = action.Type?.ToLowerInvariant() ?? "";
            try
            {
                switch (t)
                {
                    case "shortcut":
                    case "command":
                        string cmdId = action.Id?.ToLowerInvariant() ?? "";
                        if (cmdId == "undo")
                        {
                            bool ok = await UndoAsync();
                            results.Add(new EditorActionResultDto(ok, t, ok ? "Undo executed" : "Nothing to undo"));
                        }
                        else if (cmdId == "redo")
                        {
                            bool ok = await RedoAsync();
                            results.Add(new EditorActionResultDto(ok, t, ok ? "Redo executed" : "Nothing to redo"));
                        }
                        else if (CommandDispatcher != null)
                        {
                            bool ok = await CommandDispatcher(cmdId, action);
                            results.Add(new EditorActionResultDto(ok, t, ok ? $"Command '{cmdId}' executed" : $"Command '{cmdId}' returned failure"));
                        }
                        else
                        {
                            results.Add(new EditorActionResultDto(false, t, $"NoTarget: No command dispatcher registered for '{cmdId}'"));
                        }
                        break;

                    case "key":
                        bool ctrl = action.Ctrl == true;
                        bool shift = action.Shift == true;
                        string key = action.Key?.ToUpperInvariant() ?? "";

                        if (ctrl && key == "Z" && !shift)
                        {
                            bool ok = await UndoAsync();
                            results.Add(new EditorActionResultDto(ok, t, ok ? "Ctrl+Z Undo executed" : "Nothing to undo"));
                        }
                        else if (ctrl && (key == "Y" || (key == "Z" && shift)))
                        {
                            bool ok = await RedoAsync();
                            results.Add(new EditorActionResultDto(ok, t, ok ? "Ctrl+Y Redo executed" : "Nothing to redo"));
                        }
                        else if (CommandDispatcher != null)
                        {
                            bool ok = await CommandDispatcher($"key:{key}", action);
                            results.Add(new EditorActionResultDto(ok, t, ok ? $"Key '{key}' processed by dispatcher" : $"Key '{key}' rejected"));
                        }
                        else
                        {
                            results.Add(new EditorActionResultDto(false, t, $"NoTarget: No focused element or dispatcher registered for key '{key}'"));
                        }
                        break;

                    case "text":
                        if (TextInputDispatcher != null)
                        {
                            bool ok = TextInputDispatcher(action.Text ?? "");
                            results.Add(new EditorActionResultDto(ok, t, ok ? $"Text input '{action.Text}' injected" : $"Text injection failed"));
                        }
                        else
                        {
                            results.Add(new EditorActionResultDto(false, t, $"NoTarget: No focused text control available for input '{action.Text}'"));
                        }
                        break;

                    case "dragdrop":
                    case "drag_drop":
                        if (DragDropDispatcher != null)
                        {
                            bool ok = DragDropDispatcher(action.Asset ?? "", action.X ?? 0, action.Y ?? 0);
                            results.Add(new EditorActionResultDto(ok, t, ok ? $"Drag-drop completed for '{action.Asset}'" : "Drag-drop rejected by target"));
                        }
                        else
                        {
                            results.Add(new EditorActionResultDto(false, t, $"NotImplemented: No active drop target for asset '{action.Asset}' at ({action.X},{action.Y})"));
                        }
                        break;

                    default:
                        results.Add(new EditorActionResultDto(false, t, $"Unrecognized action type: '{t}'"));
                        break;
                }
            }
            catch (Exception ex)
            {
                results.Add(new EditorActionResultDto(false, t, $"Error: {ex.Message}"));
            }
        }

        return results;
    }

    /// <summary>
    /// Injects a batch of synthetic editor actions synchronously.
    /// </summary>
    public IReadOnlyList<EditorActionResultDto> InjectActions(IReadOnlyList<EditorInputActionDto> actions)
    {
        return InjectActionsAsync(actions).GetAwaiter().GetResult();
    }
}
