#nullable enable
using System;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services.Scripting
{
    // Betik yolu değişimini geri alınabilir kılar.
    public sealed class ScriptPathChangeAction : IUndoAction
    {
        private readonly ScriptComponentViewModel _component;
        private readonly string _oldPath;
        private readonly string _newPath;

        public string Description => "Change Script Path";

        public ScriptPathChangeAction(ScriptComponentViewModel component, string oldPath, string newPath)
        {
            _component = component ?? throw new ArgumentNullException(nameof(component));
            _oldPath = oldPath ?? "";
            _newPath = newPath ?? "";
        }

        public void Undo()
        {
            _component.ScriptPath = _oldPath;
        }

        public void Redo()
        {
            _component.ScriptPath = _newPath;
        }
    }
}
