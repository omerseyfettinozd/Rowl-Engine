using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;

namespace RowlEngine.Editor.Services.Inspector
{
    /// <summary>
    /// Faz 4 Dilim 4 — static Inspector schema. Maps component type keys
    /// (plus the synthetic <c>"node"</c> section) to the field descriptors
    /// the Inspector renders and validates. Unknown component types yield
    /// an empty schema (their bespoke editors keep working; they simply get
    /// no dynamic widgets or field badges yet — progressive coverage).
    /// Ranges mirror the limits already hardcoded in the component views so
    /// the schema documents, rather than changes, accepted values.
    /// </summary>
    public static class InspectorSchemaProvider
    {
        /// <summary>Node-level section (title + chapter assignment).</summary>
        public static IReadOnlyList<InspectorFieldDescriptor> NodeFields { get; } =
            new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
            {
                new InspectorFieldDescriptor(
                    InspectorFieldDescriptor.Keys.NodeTitle, "Başlık",
                    InspectorFieldKind.Text, "node",
                    Hint: "Düğüm başlığı (boş bırakılmamalı)"),
                new InspectorFieldDescriptor(
                    InspectorFieldDescriptor.Keys.NodeChapter, "Bölüm",
                    InspectorFieldKind.Chapter, "node",
                    Hint: "Kayıt/yükleme sınırı (boş = tanımsız)"),
            });

        private static readonly Dictionary<string, IReadOnlyList<InspectorFieldDescriptor>> Schemas =
            new(StringComparer.Ordinal)
            {
                ["dialogue"] = new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
                {
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueSpeaker, "Konuşmacı",
                        InspectorFieldKind.Text, "dialogue"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueText, "Diyalog Metni",
                        InspectorFieldKind.MultilineText, "dialogue"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueContentId, "Content ID",
                        InspectorFieldKind.Text, "dialogue",
                        Hint: "UUID biçimi (8-4-4-4-12); düğümler arasında tekil olmalı"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueSpeakerFontSize, "Konuşmacı Yazı Boyutu",
                        InspectorFieldKind.Range, "dialogue", Min: 10, Max: 64, Step: 2),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueFontSize, "Metin Boyutu",
                        InspectorFieldKind.Range, "dialogue", Min: 12, Max: 64, Step: 2),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueSpeakerColor, "Konuşmacı Rengi",
                        InspectorFieldKind.Color, "dialogue"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueTextColor, "Metin Rengi",
                        InspectorFieldKind.Color, "dialogue"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueBoxColor, "Kutu Rengi",
                        InspectorFieldKind.Color, "dialogue"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueBoxOpacity, "Kutu Opaklığı",
                        InspectorFieldKind.Range, "dialogue", Min: 0, Max: 1, Step: 0.05),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueTextSpeed, "Daktilo Hızı (ms/karakter)",
                        InspectorFieldKind.Range, "dialogue", Min: 5, Max: 100, Step: 5),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueBlipPitch, "Blip Perdesi",
                        InspectorFieldKind.Range, "dialogue", Min: 0.5, Max: 2.0, Step: 0.05),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueBlipVolume, "Blip Ses Düzeyi",
                        InspectorFieldKind.Range, "dialogue", Min: 0, Max: 1, Step: 0.05),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.DialogueBlipSound, "Blip Sesi",
                        InspectorFieldKind.Asset, "dialogue",
                        AssetKind: InspectorAssetKind.Audio,
                        Hint: "Boşsa prosedürel synth blip"),
                }),
                ["background"] = new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
                {
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.BackgroundTexture, "Arka Plan Görseli",
                        InspectorFieldKind.Asset, "background",
                        AssetKind: InspectorAssetKind.Image),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.BackgroundOpacity, "Opaklık",
                        InspectorFieldKind.Range, "background", Min: 0, Max: 1, Step: 0.05),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.BackgroundScale, "Ölçek",
                        InspectorFieldKind.Range, "background", Min: 0.1, Max: 5.0, Step: 0.1),
                }),
                ["character"] = new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
                {
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.CharacterSprite, "Karakter Görseli",
                        InspectorFieldKind.Asset, "character",
                        AssetKind: InspectorAssetKind.Image),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.CharacterVoiceBlip, "Ses Blip'i",
                        InspectorFieldKind.Asset, "character",
                        AssetKind: InspectorAssetKind.Audio,
                        Hint: "Boş bırakılabilir"),
                }),
                ["audio"] = new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
                {
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.AudioBgmTrack, "BGM Parçası",
                        InspectorFieldKind.Asset, "audio",
                        AssetKind: InspectorAssetKind.Audio,
                        Hint: "Boş bırakılabilir"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.AudioSfxTrack, "SFX Parçası",
                        InspectorFieldKind.Asset, "audio",
                        AssetKind: InspectorAssetKind.Audio,
                        Hint: "Boş bırakılabilir"),
                }),
                ["choice"] = new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
                {
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.ChoiceOptionText, "Seçenek Metni",
                        InspectorFieldKind.Text, "choice"),
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.ChoiceOptionTarget, "Seçenek Hedefi",
                        InspectorFieldKind.Text, "choice",
                        Hint: "Hedef düğüm kimliği (0 = bağlı değil)"),
                }),
                ["script"] = new ReadOnlyCollection<InspectorFieldDescriptor>(new[]
                {
                    new InspectorFieldDescriptor(
                        InspectorFieldDescriptor.Keys.ScriptPath, "Betik Yolu",
                        InspectorFieldKind.Asset, "script",
                        AssetKind: InspectorAssetKind.Script,
                        Hint: "Boşsa yalnızca satır-içi kod çalışır"),
                }),
            };

        /// <summary>Descriptors for one component type (empty when unknown).</summary>
        public static IReadOnlyList<InspectorFieldDescriptor> ForComponent(string? typeKey)
        {
            if (string.IsNullOrEmpty(typeKey))
                return Array.Empty<InspectorFieldDescriptor>();
            return Schemas.TryGetValue(typeKey, out var schema)
                ? schema
                : Array.Empty<InspectorFieldDescriptor>();
        }

        /// <summary>Range descriptor lookup by validation key (null when absent/not a range).</summary>
        public static InspectorFieldDescriptor? FindRange(string key)
        {
            foreach (var schema in Schemas.Values)
                foreach (var field in schema)
                    if (field.Kind == InspectorFieldKind.Range &&
                        string.Equals(field.Key, key, StringComparison.Ordinal))
                        return field;
            return null;
        }
    }
}
