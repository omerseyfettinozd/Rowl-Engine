using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services.Extras;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>One entry of the unlock kind picker: the JSON <c>kind</c> plus its editor-string name.</summary>
    public sealed class UnlockKindOption : ObservableObject
    {
        internal UnlockKindOption(string kind) => Kind = kind;

        /// <summary>The value written to the component's <c>kind</c> key (cg, music, achievement).</summary>
        public string Kind { get; }

        public string DisplayName => EditorStrings.T("Component.Unlock.Kind." + Kind, Kind);

        internal void RaiseDisplayNameChanged() => OnPropertyChanged(nameof(DisplayName));
    }

    /// <summary>One entry of the unlock id picker. <see cref="InCatalog"/> is false for an id the catalog does not list.</summary>
    public sealed record UnlockIdOption(string Id, string Label, bool InCatalog);

    /// <summary>
    /// "unlock" component (D07-1 native schema): <c>{"kind": "cg|music|achievement", "id": "..."}</c>.
    /// Entering a node with it unlocks that item of the player's profile once (never on load, rewind
    /// or replay). The id is picked from the project's <c>Assets/json/extras.json</c> catalog; with no
    /// usable catalog it is free text. An id the catalog does not list is kept (and flagged by the
    /// project lint), never silently replaced.
    /// </summary>
    public partial class UnlockComponentViewModel : NodeComponentViewModel
    {
        private static readonly UnlockKindOption[] s_kindOptions =
        {
            new(ExtrasRules.KindCg),
            new(ExtrasRules.KindMusic),
            new(ExtrasRules.KindAchievement),
        };

        private static bool s_stringsHooked;
        private static readonly object s_hookGate = new();

        public override string DisplayNameKey => "Component.Unlock.DisplayName";
        protected override string DisplayNameTrFallback => "Açılabilir İçerik";
        protected override string DisplayNameEnFallback => "Unlockable Content";
        public override string Icon => "Kilit";
        public override string TypeKey => "unlock";

        /// <summary>Where the catalog comes from; tests replace it. Null result = no usable catalog.</summary>
        internal static Func<ExtrasCatalogModel?> CatalogProvider { get; set; } = ReadProjectCatalog;

        private static ExtrasCatalogModel? ReadProjectCatalog()
        {
            try
            {
                return ExtrasCatalogStore.ReadShared(MainWindowViewModel.AssetsPath);
            }
            catch (Exception)
            {
                return null;
            }
        }

        [ObservableProperty]
        private string _kind = ExtrasRules.KindCg;

        [ObservableProperty]
        private string _unlockId = "";

        private IReadOnlyList<UnlockIdOption>? _idOptions;

        public UnlockComponentViewModel()
        {
            HookStrings();
        }

        private static void HookStrings()
        {
            lock (s_hookGate)
            {
                if (s_stringsHooked) return;
                s_stringsHooked = true;
                EditorStrings.Instance.PropertyChanged += (_, e) =>
                {
                    if (!string.IsNullOrEmpty(e.PropertyName) &&
                        e.PropertyName != EditorStrings.IndexerPropertyName &&
                        e.PropertyName != nameof(EditorStrings.Language))
                        return;
                    foreach (var option in s_kindOptions) option.RaiseDisplayNameChanged();
                };
            }
        }

        /// <summary>The three kinds (CG, music, achievement).</summary>
        public static IReadOnlyList<UnlockKindOption> KindOptions => s_kindOptions;

        /// <summary>
        /// Kind picker selection. A stored kind the picker does not know (a hand-edited file) shows
        /// no selection; the stored string is only replaced when the author picks an entry.
        /// </summary>
        public UnlockKindOption? SelectedKindOption
        {
            get => s_kindOptions.FirstOrDefault(option => option.Kind == Kind);
            set
            {
                if (value is not null) Kind = value.Kind;
            }
        }

        /// <summary>The catalog ids of the current kind, plus the current id when the catalog lacks it.</summary>
        public IReadOnlyList<UnlockIdOption> IdOptions => _idOptions ??= BuildIdOptions();

        /// <summary>True when the catalog lists at least one item of the current kind (the id is then a picker).</summary>
        public bool HasCatalogIds => IdOptions.Any(option => option.InCatalog);

        /// <summary>No catalog item of this kind: the id is typed by hand.</summary>
        public bool UsesFreeText => !HasCatalogIds;

        /// <summary>The current id is not in the catalog (a typo, or the catalog changed).</summary>
        public bool IdNotInCatalog =>
            HasCatalogIds && UnlockId.Length > 0 && !IdOptions.Any(option => option.InCatalog && option.Id == UnlockId);

        public UnlockIdOption? SelectedIdOption
        {
            get => UnlockId.Length == 0 ? null : IdOptions.FirstOrDefault(option => option.Id == UnlockId);
            set
            {
                // The ComboBox reports null whenever its item list is replaced; writing that back
                // would erase the id (the D06-2 Denetçi lesson).
                if (value is not null) UnlockId = value.Id;
            }
        }

        /// <summary>Re-reads the catalog (the view calls it when it appears and when the picker opens).</summary>
        public void RefreshCatalog()
        {
            _idOptions = null;
            NotifyIdOptions();
        }

        partial void OnKindChanged(string value)
        {
            _idOptions = null;
            OnPropertyChanged(nameof(SelectedKindOption));
            NotifyIdOptions();
        }

        partial void OnUnlockIdChanged(string value)
        {
            // Keep the item list stable while a pick is being applied; only append the current id
            // when it is new to the list (a rebuilt list makes the ComboBox clear its selection).
            if (_idOptions is not null && value.Length > 0 && _idOptions.All(option => option.Id != value))
                _idOptions = null;
            OnPropertyChanged(nameof(SelectedIdOption));
            OnPropertyChanged(nameof(IdNotInCatalog));
            if (_idOptions is null) NotifyIdOptions();
        }

        private void NotifyIdOptions()
        {
            OnPropertyChanged(nameof(IdOptions));
            OnPropertyChanged(nameof(HasCatalogIds));
            OnPropertyChanged(nameof(UsesFreeText));
            OnPropertyChanged(nameof(IdNotInCatalog));
            OnPropertyChanged(nameof(SelectedIdOption));
        }

        private IReadOnlyList<UnlockIdOption> BuildIdOptions()
        {
            var options = new List<UnlockIdOption>();
            var catalog = CatalogProvider();
            if (catalog is not null)
            {
                string language = EditorStrings.Instance.Language;
                foreach (var item in catalog.ItemsOf(Kind))
                {
                    if (item.Id.Length == 0) continue;
                    string title = item.TitleText.Get(language);
                    if (title.Length == 0) title = item.TitleText.Get(language == "tr" ? "en" : "tr");
                    options.Add(new UnlockIdOption(item.Id, title.Length > 0 ? $"{item.Id}  -  {title}" : item.Id, true));
                }
            }
            if (UnlockId.Length > 0 && options.All(option => option.Id != UnlockId) && options.Count > 0)
            {
                string note = EditorStrings.T("Component.Unlock.NotInCatalog", "katalogda yok", "not in catalog");
                options.Add(new UnlockIdOption(UnlockId, $"{UnlockId}  ({note})", false));
            }
            return options;
        }

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["kind"] = Kind,
                ["id"]   = UnlockId
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("kind", out var kind) && kind is string kindText)
                Kind = kindText;
            if (data.TryGetValue("id", out var id) && id is string idText)
                UnlockId = idText;
        }
    }
}
