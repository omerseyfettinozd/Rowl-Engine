using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// A component type this editor has no view model for (a newer engine, a
    /// hand-written project, a future plugin). The editor cannot edit it, but it
    /// must never delete it: open + save keeps the type key, id, enabled flag and
    /// the <c>data</c> object byte-for-byte in meaning (D07-3; before this the
    /// loader dropped such components and the next save lost them).
    /// </summary>
    public sealed class UnknownComponentViewModel : NodeComponentViewModel
    {
        private readonly string _typeKey;
        // Detached clones: a JsonElement taken from a parsed document is only valid
        // while that document lives, and the save runs long after the load.
        private readonly Dictionary<string, JsonElement> _data = new(StringComparer.Ordinal);

        public UnknownComponentViewModel(string typeKey)
        {
            _typeKey = typeKey ?? string.Empty;
        }

        public override string TypeKey => _typeKey;

        public override string DisplayNameKey => "Component.Unknown.DisplayName";
        protected override string DisplayNameTrFallback => "Bilinmeyen bileşen";
        protected override string DisplayNameEnFallback => "Unknown component";
        public override string DisplayName => base.DisplayName + ": " + _typeKey;
        public override string Icon => "?";

        /// <summary>Number of kept data keys (the inspector card shows it).</summary>
        public int DataKeyCount => _data.Count;

        /// <summary>Compact raw JSON of the kept data (read-only display).</summary>
        public string RawDataPreview
        {
            get
            {
                string text = JsonSerializer.Serialize(_data);
                return text.Length <= 240 ? text : text[..237] + "...";
            }
        }

        public override Dictionary<string, object> Serialize()
        {
            var result = new Dictionary<string, object>(_data.Count, StringComparer.Ordinal);
            foreach (var pair in _data) result[pair.Key] = pair.Value;
            return result;
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            _data.Clear();
            foreach (var pair in data)
            {
                if (pair.Value is null)
                {
                    _data[pair.Key] = JsonSerializer.SerializeToElement<object?>(null);
                    continue;
                }
                _data[pair.Key] = pair.Value is JsonElement element
                    ? element.Clone()
                    : JsonSerializer.SerializeToElement(pair.Value, pair.Value.GetType());
            }
            OnPropertyChanged(nameof(DataKeyCount));
            OnPropertyChanged(nameof(RawDataPreview));
        }

        /// <summary>Verbatim copy (data and enabled flag; a fresh component id like every copy).</summary>
        public UnknownComponentViewModel CloneComponent()
        {
            var copy = new UnknownComponentViewModel(_typeKey) { IsEnabled = IsEnabled };
            copy.Deserialize(Serialize().ToDictionary(pair => pair.Key, pair => (object?)pair.Value));
            return copy;
        }
    }
}
