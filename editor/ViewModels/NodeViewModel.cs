using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Reflection;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.ViewModels
{
    public partial class NodeViewModel : ObservableObject
    {
        // ── Path helpers (synced with MainWindowViewModel) ──
        private static string AssetsPath => MainWindowViewModel.AssetsPath;

        // ── Graph metadata ──

        [ObservableProperty]
        private ulong _id;

        [ObservableProperty]
        private string _title = string.Empty;

        [ObservableProperty]
        private double _x;

        [ObservableProperty]
        private double _y;

        [ObservableProperty]
        private bool _isSelected;

        [ObservableProperty]
        private bool _isStartNode;

        [ObservableProperty]
        private string _borderColor = "#2A2A3D";

        partial void OnIsStartNodeChanged(bool value) => RefreshBorderColor();

        partial void OnIsSelectedChanged(bool value) => RefreshBorderColor();

        private void RefreshBorderColor()
        {
            // Selection takes priority so keyboard/pointer focus stays visible;
            // the start badge still preserves the node's semantic role.
            BorderColor = IsSelected ? "#F09A78" : IsStartNode ? "#10B981" : "#2A2A3D";
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  UNITY-STYLE GAMEOBJECT / ENTITY HIERARCHY  ██
        // ══════════════════════════════════════════════════════════════════════

        /// <summary>
        /// Ordered collection of GameObjects (FrameObjects) in this node/frame scene.
        /// In Unity, each item in the Hierarchy is a GameObject.
        /// </summary>
        public ObservableCollection<FrameObjectViewModel> Objects { get; } = new();

        /// <summary>
        /// Flattens all active components across all active GameObjects in this frame.
        /// </summary>
        public IEnumerable<NodeComponentViewModel> AllComponents =>
            Objects.Where(o => o.IsActive).SelectMany(o => o.Components);

        /// <summary>
        /// Compatibility list for AllComponents.
        /// </summary>
        public IReadOnlyList<NodeComponentViewModel> Components => AllComponents.ToList();

        // ── GameObject Management ──

        public FrameObjectViewModel CreateObject(string name = "GameObject")
        {
            var obj = new FrameObjectViewModel(name, this);
            Objects.Add(obj);
            SubscribeObjectChanges(obj);
            NotifyObjectsChanged();
            return obj;
        }

        public void AddObject(FrameObjectViewModel obj)
        {
            obj.Node = this;
            Objects.Add(obj);
            SubscribeObjectChanges(obj);
            NotifyObjectsChanged();
        }

        public void RemoveObject(FrameObjectViewModel obj)
        {
            UnsubscribeObjectChanges(obj);
            Objects.Remove(obj);
            obj.Node = null;
            NotifyObjectsChanged();
        }

        public void MoveObjectUp(FrameObjectViewModel obj)
        {
            int idx = Objects.IndexOf(obj);
            if (idx > 0)
            {
                Objects.Move(idx, idx - 1);
                NotifyObjectsChanged();
            }
        }

        public void MoveObjectDown(FrameObjectViewModel obj)
        {
            int idx = Objects.IndexOf(obj);
            if (idx >= 0 && idx < Objects.Count - 1)
            {
                Objects.Move(idx, idx + 1);
                NotifyObjectsChanged();
            }
        }

        public FrameObjectViewModel DuplicateObject(FrameObjectViewModel source)
        {
            var newObj = CreateObject($"{source.Name} (Copy)");
            foreach (var comp in source.Components)
            {
                var newComp = ComponentRegistry.Create(comp.TypeKey);
                newComp.Deserialize(comp.Serialize().ToDictionary(
                    pair => pair.Key,
                    pair => (object?)pair.Value));
                newObj.AddComponent(newComp);
            }
            return newObj;
        }

        // ── Component Access Helpers ──

        public T? GetComponent<T>() where T : NodeComponentViewModel =>
            AllComponents.OfType<T>().FirstOrDefault();

        public IEnumerable<T> GetComponents<T>() where T : NodeComponentViewModel =>
            AllComponents.OfType<T>();

        public bool HasDialogueBox => AllComponents.OfType<DialogueComponentViewModel>().Any(d => d.IsEnabled);
        public bool HasBackground => AllComponents.OfType<BackgroundComponentViewModel>().Any(b => b.IsEnabled);

        /// Choice options drive the visible output pins on the graph card.
        public IReadOnlyList<ChoiceOptionViewModel> ChoiceOptions =>
            AllComponents.OfType<ChoiceComponentViewModel>().SelectMany(choice => choice.Options).ToList();

        public bool HasChoices => ChoiceOptions.Count > 0;
        public string ComponentSummary => $"{Components.Count} BİLEŞEN";
        public double NodeCardHeight => Math.Max(120, 52 + (ChoiceOptions.Count * 30));
        public bool ChoiceDataChanged => true;
        public double GetOutputPortY(string optionId) => string.IsNullOrEmpty(optionId)
            ? 60 : 60 + (Math.Max(0, ChoiceOptions.ToList().FindIndex(option => option.OptionId == optionId)) * 30);

        /// <summary>
        /// Dedicated ObservableCollection of active Character components for smooth UI binding without tree rebuilds.
        /// </summary>
        public ObservableCollection<CharacterComponentViewModel> CharacterComponents { get; } = new();

        /// <summary>
        /// Dedicated ObservableCollection of active Dialogue components for smooth multi-dialogue UI binding.
        /// </summary>
        public ObservableCollection<DialogueComponentViewModel> DialogueComponents { get; } = new();

        // ── Legacy Component Helper (wraps into default/first object or creates one) ──

        /// <summary>
        /// Legacy fallback for adding a component directly to the first object or a new object.
        /// </summary>
        public T AddComponent<T>() where T : NodeComponentViewModel, new()
        {
            var targetObj = Objects.FirstOrDefault();
            if (targetObj == null)
            {
                targetObj = CreateObject("GameObject");
            }
            return targetObj.AddComponent<T>();
        }

        public void AddComponent(NodeComponentViewModel comp)
        {
            var targetObj = Objects.FirstOrDefault();
            if (targetObj == null)
            {
                targetObj = CreateObject("GameObject");
            }
            targetObj.AddComponent(comp);
        }

        public void RemoveComponent(NodeComponentViewModel comp)
        {
            foreach (var obj in Objects)
            {
                if (obj.Components.Contains(comp))
                {
                    obj.RemoveComponent(comp);
                    break;
                }
            }
        }

        public void MoveComponentUp(NodeComponentViewModel comp)
        {
            comp.OwnerObject?.MoveComponentUp(comp);
        }

        public void MoveComponentDown(NodeComponentViewModel comp)
        {
            comp.OwnerObject?.MoveComponentDown(comp);
        }

        // ── Object & Component Change Notifications ──

        private void SubscribeObjectChanges(FrameObjectViewModel obj)
        {
            obj.PropertyChanged += OnObjectPropertyChanged;
        }

        private void UnsubscribeObjectChanges(FrameObjectViewModel obj)
        {
            obj.PropertyChanged -= OnObjectPropertyChanged;
        }

        private void OnObjectPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(FrameObjectViewModel.IsActive) ||
                e.PropertyName == nameof(FrameObjectViewModel.Name))
            {
                NotifyObjectsChanged();
            }
        }

        public void NotifyObjectsChanged()
        {
            SyncCharacterComponents();
            SyncDialogueComponents();

            OnPropertyChanged(nameof(Objects));
            OnPropertyChanged(nameof(AllComponents));
            OnPropertyChanged(nameof(Components));
            OnPropertyChanged(nameof(DialogueComponents));
            OnPropertyChanged(nameof(HasDialogueBox));
            OnPropertyChanged(nameof(HasBackground));
            OnPropertyChanged(nameof(ChoiceOptions));
            OnPropertyChanged(nameof(HasChoices));
            OnPropertyChanged(nameof(ComponentSummary));
            OnPropertyChanged(nameof(NodeCardHeight));

            // Refresh proxy properties
            OnPropertyChanged(nameof(Speaker));
            OnPropertyChanged(nameof(DialogueText));
            OnPropertyChanged(nameof(BackgroundTexture));
            OnPropertyChanged(nameof(BackgroundBitmap));
            OnPropertyChanged(nameof(CharacterSprite));
            OnPropertyChanged(nameof(CharacterBitmap));
            OnPropertyChanged(nameof(DialogueBoxX));
            OnPropertyChanged(nameof(DialogueBoxY));
            OnPropertyChanged(nameof(DialogueBoxWidth));
            OnPropertyChanged(nameof(DialogueBoxHeight));
        }

        private void SyncCharacterComponents()
        {
            var currentChars = AllComponents.OfType<CharacterComponentViewModel>().ToList();
            if (!CharacterComponents.SequenceEqual(currentChars))
            {
                CharacterComponents.Clear();
                foreach (var ch in currentChars)
                {
                    CharacterComponents.Add(ch);
                }
            }
        }

        private void SyncDialogueComponents()
        {
            var currentDlgs = AllComponents.OfType<DialogueComponentViewModel>().ToList();
            if (!DialogueComponents.SequenceEqual(currentDlgs))
            {
                DialogueComponents.Clear();
                foreach (var d in currentDlgs)
                {
                    DialogueComponents.Add(d);
                }
            }
        }

        public void NotifyComponentPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (string.IsNullOrEmpty(e.PropertyName)) return;

            if (e.PropertyName == nameof(NodeComponentViewModel.IsEnabled))
            {
                NotifyObjectsChanged();
                return;
            }

            if (sender is ChoiceComponentViewModel && e.PropertyName == nameof(ChoiceComponentViewModel.Options))
            {
                // The collection shape changed, so refresh only choice-dependent
                // projections. Do not rebuild every component proxy in the node.
                OnPropertyChanged(nameof(ChoiceOptions));
                OnPropertyChanged(nameof(HasChoices));
                OnPropertyChanged(nameof(NodeCardHeight));
                return;
            }

            if (sender is ChoiceComponentViewModel && e.PropertyName == nameof(ChoiceComponentViewModel.ChoiceDataChanged))
            {
                // Existing option bindings update themselves; this marker is for
                // debounced persistence/runtime synchronization only.
                OnPropertyChanged(nameof(ChoiceDataChanged));
                return;
            }

            if (sender is DialogueComponentViewModel)
            {
                switch (e.PropertyName)
                {
                    case nameof(DialogueComponentViewModel.Speaker):
                        OnPropertyChanged(nameof(Speaker));
                        break;
                    case nameof(DialogueComponentViewModel.DialogueText):
                        OnPropertyChanged(nameof(DialogueText));
                        break;
                    case nameof(DialogueComponentViewModel.FontSize):
                        OnPropertyChanged(nameof(FontSize));
                        break;
                    case nameof(DialogueComponentViewModel.SpeakerFontSize):
                        OnPropertyChanged(nameof(SpeakerFontSize));
                        break;
                    case nameof(DialogueComponentViewModel.TextColor):
                        OnPropertyChanged(nameof(TextColor));
                        break;
                    case nameof(DialogueComponentViewModel.SpeakerColor):
                        OnPropertyChanged(nameof(SpeakerColor));
                        break;
                    case nameof(DialogueComponentViewModel.BoxOpacity):
                        OnPropertyChanged(nameof(BoxOpacity));
                        break;
                    case nameof(DialogueComponentViewModel.BoxColor):
                        OnPropertyChanged(nameof(BoxColor));
                        break;
                    case nameof(DialogueComponentViewModel.BorderColor):
                        OnPropertyChanged(nameof(BorderColorHex));
                        break;
                    case nameof(DialogueComponentViewModel.BorderThickness):
                        OnPropertyChanged(nameof(BorderThickness));
                        break;
                    case nameof(DialogueComponentViewModel.CornerRadius):
                        OnPropertyChanged(nameof(CornerRadius));
                        break;
                    case nameof(DialogueComponentViewModel.TextAlignment):
                        OnPropertyChanged(nameof(TextAlignment));
                        break;
                    case nameof(DialogueComponentViewModel.X):
                        OnPropertyChanged(nameof(DialogueBoxX));
                        break;
                    case nameof(DialogueComponentViewModel.Y):
                        OnPropertyChanged(nameof(DialogueBoxY));
                        break;
                    case nameof(DialogueComponentViewModel.Width):
                        OnPropertyChanged(nameof(DialogueBoxWidth));
                        break;
                    case nameof(DialogueComponentViewModel.Height):
                        OnPropertyChanged(nameof(DialogueBoxHeight));
                        break;
                    case nameof(DialogueComponentViewModel.Scale):
                        OnPropertyChanged(nameof(DialogueBoxScale));
                        break;
                }
            }
            else if (sender is BackgroundComponentViewModel)
            {
                switch (e.PropertyName)
                {
                    case nameof(BackgroundComponentViewModel.Texture):
                        OnPropertyChanged(nameof(BackgroundTexture));
                        break;
                    case nameof(BackgroundComponentViewModel.X):
                        OnPropertyChanged(nameof(BackgroundX));
                        break;
                    case nameof(BackgroundComponentViewModel.Y):
                        OnPropertyChanged(nameof(BackgroundY));
                        break;
                    case nameof(BackgroundComponentViewModel.Width):
                        OnPropertyChanged(nameof(BackgroundWidth));
                        break;
                    case nameof(BackgroundComponentViewModel.Height):
                        OnPropertyChanged(nameof(BackgroundHeight));
                        break;
                    case nameof(BackgroundComponentViewModel.Scale):
                        OnPropertyChanged(nameof(BackgroundScale));
                        break;
                    case nameof(BackgroundComponentViewModel.Rotation):
                        OnPropertyChanged(nameof(BackgroundRotation));
                        break;
                    case nameof(BackgroundComponentViewModel.ParallaxFactorX):
                        OnPropertyChanged(nameof(BackgroundParallaxX));
                        break;
                    case nameof(BackgroundComponentViewModel.ParallaxFactorY):
                        OnPropertyChanged(nameof(BackgroundParallaxY));
                        break;
                    case nameof(BackgroundComponentViewModel.Opacity):
                        OnPropertyChanged(nameof(BackgroundOpacity));
                        break;
                    case nameof(BackgroundComponentViewModel.TextureBitmap):
                        OnPropertyChanged(nameof(BackgroundBitmap));
                        break;
                }
            }
            else if (sender is CharacterComponentViewModel)
            {
                switch (e.PropertyName)
                {
                    case nameof(CharacterComponentViewModel.Sprite):
                        OnPropertyChanged(nameof(CharacterSprite));
                        break;
                    case nameof(CharacterComponentViewModel.Position):
                        OnPropertyChanged(nameof(CharacterPosition));
                        break;
                    case nameof(CharacterComponentViewModel.X):
                        OnPropertyChanged(nameof(CharacterX));
                        break;
                    case nameof(CharacterComponentViewModel.Y):
                        OnPropertyChanged(nameof(CharacterY));
                        break;
                    case nameof(CharacterComponentViewModel.Width):
                        OnPropertyChanged(nameof(CharacterWidth));
                        break;
                    case nameof(CharacterComponentViewModel.Height):
                        OnPropertyChanged(nameof(CharacterHeight));
                        break;
                    case nameof(CharacterComponentViewModel.Scale):
                        OnPropertyChanged(nameof(CharacterScale));
                        break;
                    case nameof(CharacterComponentViewModel.Rotation):
                        OnPropertyChanged(nameof(CharacterRotation));
                        break;
                    case nameof(CharacterComponentViewModel.ScaleX):
                        OnPropertyChanged(nameof(CharacterScaleX));
                        break;
                    case nameof(CharacterComponentViewModel.ScaleY):
                        OnPropertyChanged(nameof(CharacterScaleY));
                        break;
                    case nameof(CharacterComponentViewModel.MaintainAspectRatio):
                        OnPropertyChanged(nameof(CharacterMaintainAspectRatio));
                        break;
                    case nameof(CharacterComponentViewModel.SpriteBitmap):
                        OnPropertyChanged(nameof(CharacterBitmap));
                        break;
                }
            }
            else if (sender is AudioComponentViewModel)
            {
                if (e.PropertyName == nameof(AudioComponentViewModel.DspFilter))
                    OnPropertyChanged(nameof(DspFilter));
            }
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  BACKWARD-COMPATIBLE PROXY PROPERTIES  ██
        // ══════════════════════════════════════════════════════════════════════

        public DialogueComponentViewModel? PrimaryDialogueComponent =>
            AllComponents.OfType<DialogueComponentViewModel>().FirstOrDefault(d => d.IsEnabled)
            ?? AllComponents.OfType<DialogueComponentViewModel>().FirstOrDefault();

        public string Speaker
        {
            get => PrimaryDialogueComponent?.Speaker ?? "Evelyn";
            set { var c = PrimaryDialogueComponent; if (c != null) c.Speaker = value; }
        }

        public string DialogueText
        {
            get => PrimaryDialogueComponent?.DialogueText ?? "";
            set { var c = PrimaryDialogueComponent; if (c != null) c.DialogueText = value; }
        }

        public string BackgroundTexture
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Texture ?? "bg_beach_sunset.png";
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Texture = value; }
        }

        public double BackgroundX
        {
            get => GetComponent<BackgroundComponentViewModel>()?.X ?? 0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.X = value; }
        }

        public double BackgroundY
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Y ?? 0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Y = value; }
        }

        public double BackgroundWidth
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Width ?? 1920;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Width = value; }
        }

        public double BackgroundHeight
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Height ?? 1080;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Height = value; }
        }

        public double BackgroundScale
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Scale ?? 1.0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Scale = value; }
        }

        public double BackgroundRotation
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Rotation ?? 0.0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Rotation = value; }
        }

        public double BackgroundParallaxX
        {
            get => GetComponent<BackgroundComponentViewModel>()?.ParallaxFactorX ?? 1.0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.ParallaxFactorX = value; }
        }

        public double BackgroundParallaxY
        {
            get => GetComponent<BackgroundComponentViewModel>()?.ParallaxFactorY ?? 1.0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.ParallaxFactorY = value; }
        }

        public double BackgroundOpacity
        {
            get => GetComponent<BackgroundComponentViewModel>()?.Opacity ?? 1.0;
            set { var c = GetComponent<BackgroundComponentViewModel>(); if (c != null) c.Opacity = value; }
        }

        public Bitmap? BackgroundBitmap => GetComponent<BackgroundComponentViewModel>()?.TextureBitmap;

        public string CharacterSprite
        {
            get => GetComponent<CharacterComponentViewModel>()?.Sprite ?? "spr_evelyn.png";
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Sprite = value; }
        }

        public string CharacterPosition
        {
            get => GetComponent<CharacterComponentViewModel>()?.Position ?? "Right";
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Position = value; }
        }

        public double CharacterX
        {
            get => GetComponent<CharacterComponentViewModel>()?.X ?? 1440;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.X = value; }
        }

        public double CharacterY
        {
            get => GetComponent<CharacterComponentViewModel>()?.Y ?? 340;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Y = value; }
        }

        public double CharacterWidth
        {
            get => GetComponent<CharacterComponentViewModel>()?.Width ?? 360;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Width = value; }
        }

        public double CharacterHeight
        {
            get => GetComponent<CharacterComponentViewModel>()?.Height ?? 540;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Height = value; }
        }

        public double CharacterScale
        {
            get => GetComponent<CharacterComponentViewModel>()?.Scale ?? 1.0;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Scale = value; }
        }

        public double CharacterRotation
        {
            get => GetComponent<CharacterComponentViewModel>()?.Rotation ?? 0.0;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.Rotation = value; }
        }

        public double CharacterScaleX
        {
            get => GetComponent<CharacterComponentViewModel>()?.ScaleX ?? 1.0;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.ScaleX = value; }
        }

        public double CharacterScaleY
        {
            get => GetComponent<CharacterComponentViewModel>()?.ScaleY ?? 1.0;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.ScaleY = value; }
        }

        public bool CharacterMaintainAspectRatio
        {
            get => GetComponent<CharacterComponentViewModel>()?.MaintainAspectRatio ?? true;
            set { var c = GetComponent<CharacterComponentViewModel>(); if (c != null) c.MaintainAspectRatio = value; }
        }

        public Bitmap? CharacterBitmap => GetComponent<CharacterComponentViewModel>()?.SpriteBitmap;

        public double DialogueBoxX
        {
            get => PrimaryDialogueComponent?.X ?? 80;
            set { var d = PrimaryDialogueComponent; if (d != null) d.X = value; }
        }

        public double DialogueBoxY
        {
            get => PrimaryDialogueComponent?.Y ?? 860;
            set { var d = PrimaryDialogueComponent; if (d != null) d.Y = value; }
        }

        public double DialogueBoxWidth
        {
            get => PrimaryDialogueComponent?.Width ?? 1760;
            set { var d = PrimaryDialogueComponent; if (d != null) d.Width = value; }
        }

        public double DialogueBoxHeight
        {
            get => PrimaryDialogueComponent?.Height ?? 180;
            set { var d = PrimaryDialogueComponent; if (d != null) d.Height = value; }
        }

        public double DialogueBoxScale
        {
            get => PrimaryDialogueComponent?.Scale ?? 1.0;
            set { var d = PrimaryDialogueComponent; if (d != null) d.Scale = value; }
        }

        public DialogueComponentViewModel? DialogueComponent => PrimaryDialogueComponent;

        public double FontSize
        {
            get => PrimaryDialogueComponent?.FontSize ?? 24.0;
            set { var d = PrimaryDialogueComponent; if (d != null) d.FontSize = value; }
        }

        public double SpeakerFontSize
        {
            get => PrimaryDialogueComponent?.SpeakerFontSize ?? 20.0;
            set { var d = PrimaryDialogueComponent; if (d != null) d.SpeakerFontSize = value; }
        }

        public string TextColor
        {
            get => PrimaryDialogueComponent?.TextColor ?? "#F1F5F9";
            set { var d = PrimaryDialogueComponent; if (d != null) d.TextColor = value; }
        }

        public string SpeakerColor
        {
            get => PrimaryDialogueComponent?.SpeakerColor ?? "#38BDF8";
            set { var d = PrimaryDialogueComponent; if (d != null) d.SpeakerColor = value; }
        }

        public double BoxOpacity
        {
            get => PrimaryDialogueComponent?.BoxOpacity ?? 0.88;
            set { var d = PrimaryDialogueComponent; if (d != null) d.BoxOpacity = value; }
        }

        public string BoxColor
        {
            get => PrimaryDialogueComponent?.BoxColor ?? "#0F0F1A";
            set { var d = PrimaryDialogueComponent; if (d != null) d.BoxColor = value; }
        }

        public string BorderColorHex
        {
            get => PrimaryDialogueComponent?.BorderColor ?? "#00F0FF";
            set { var d = PrimaryDialogueComponent; if (d != null) d.BorderColor = value; }
        }

        public double BorderThickness
        {
            get => PrimaryDialogueComponent?.BorderThickness ?? 2.0;
            set { var d = PrimaryDialogueComponent; if (d != null) d.BorderThickness = value; }
        }

        public double CornerRadius
        {
            get => PrimaryDialogueComponent?.CornerRadius ?? 8.0;
            set { var d = PrimaryDialogueComponent; if (d != null) d.CornerRadius = value; }
        }

        public string TextAlignment
        {
            get => PrimaryDialogueComponent?.TextAlignment ?? "Left";
            set { var d = PrimaryDialogueComponent; if (d != null) d.TextAlignment = value; }
        }

        public string DspFilter
        {
            get => GetComponent<AudioComponentViewModel>()?.DspFilter ?? "Normal";
            set { var c = GetComponent<AudioComponentViewModel>(); if (c != null) c.DspFilter = value; }
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  BITMAP REFRESH  ██
        // ══════════════════════════════════════════════════════════════════════

        public void RefreshBitmaps()
        {
            GetComponent<BackgroundComponentViewModel>()?.RefreshBitmap();
            foreach (var charComp in GetComponents<CharacterComponentViewModel>())
                charComp.RefreshBitmap();
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  CONSTRUCTORS  ██
        // ══════════════════════════════════════════════════════════════════════

        /// <summary>
        /// Creates a new node with default GameObjects (Background, Evelyn, Dialogue Box, Audio).
        /// </summary>
        public NodeViewModel(ulong id, string title, double x, double y)
        {
            Id = id;
            Title = title;
            X = x;
            Y = y;

            // Create default GameObjects (Unity GameObject pattern)
            var bgObj = CreateObject("Background");
            bgObj.AddComponent<BackgroundComponentViewModel>();

            var charObj = CreateObject("Evelyn");
            charObj.AddComponent<CharacterComponentViewModel>();

            var dlgObj = CreateObject("Dialogue Box");
            dlgObj.AddComponent<DialogueComponentViewModel>();

            var audioObj = CreateObject("Audio");
            audioObj.AddComponent<AudioComponentViewModel>();

            RefreshBitmaps();
        }

        /// <summary>
        /// Creates a bare node without default objects (used for JSON loading).
        /// </summary>
        public NodeViewModel(ulong id, string title, double x, double y, bool bare)
        {
            Id = id;
            Title = title;
            X = x;
            Y = y;

            if (!bare)
            {
                var bgObj = CreateObject("Background");
                bgObj.AddComponent<BackgroundComponentViewModel>();

                var charObj = CreateObject("Evelyn");
                charObj.AddComponent<CharacterComponentViewModel>();

                var dlgObj = CreateObject("Dialogue Box");
                dlgObj.AddComponent<DialogueComponentViewModel>();

                var audioObj = CreateObject("Audio");
                audioObj.AddComponent<AudioComponentViewModel>();

                RefreshBitmaps();
            }
        }
    }
}
