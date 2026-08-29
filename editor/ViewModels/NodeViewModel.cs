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

        // ── Graph metadata (these remain on the node, NOT in components) ──

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

        partial void OnIsStartNodeChanged(bool value)
        {
            BorderColor = value ? "#10B981" : "#2A2A3D";
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  COMPONENT SYSTEM  ██
        // ══════════════════════════════════════════════════════════════════════

        /// <summary>
        /// The ordered collection of components attached to this node.
        /// Components are rendered/serialized in list order.
        /// </summary>
        public ObservableCollection<NodeComponentViewModel> Components { get; } = new();

        // ── Component access helpers ──

        /// <summary>
        /// Returns the first component of the specified type, or null if none exists.
        /// </summary>
        public T? GetComponent<T>() where T : NodeComponentViewModel
            => Components.OfType<T>().FirstOrDefault();

        /// <summary>
        /// Returns all components of the specified type.
        /// </summary>
        public IEnumerable<T> GetComponents<T>() where T : NodeComponentViewModel
            => Components.OfType<T>();

        /// <summary>
        /// Creates and adds a new component of the specified type.
        /// </summary>
        public T AddComponent<T>() where T : NodeComponentViewModel, new()
        {
            var comp = new T { Node = this };
            Components.Add(comp);
            SubscribeComponentChanges(comp);

            if (comp is BackgroundComponentViewModel bg) bg.RefreshBitmap();
            else if (comp is CharacterComponentViewModel ch) ch.RefreshBitmap();

            OnPropertyChanged(nameof(Components));
            OnPropertyChanged(nameof(CharacterComponents));
            OnPropertyChanged(nameof(HasDialogueBox));
            OnPropertyChanged(nameof(HasBackground));
            return comp;
        }

        /// <summary>
        /// Adds an existing component instance to this node.
        /// </summary>
        public void AddComponent(NodeComponentViewModel component)
        {
            component.Node = this;
            Components.Add(component);
            SubscribeComponentChanges(component);

            if (component is BackgroundComponentViewModel bg) bg.RefreshBitmap();
            else if (component is CharacterComponentViewModel ch) ch.RefreshBitmap();

            OnPropertyChanged(nameof(Components));
            OnPropertyChanged(nameof(CharacterComponents));
            OnPropertyChanged(nameof(HasDialogueBox));
            OnPropertyChanged(nameof(HasBackground));
        }

        /// <summary>
        /// Removes a component from this node.
        /// </summary>
        public void RemoveComponent(NodeComponentViewModel component)
        {
            UnsubscribeComponentChanges(component);
            Components.Remove(component);
            component.Node = null;
            OnPropertyChanged(nameof(Components));
            OnPropertyChanged(nameof(CharacterComponents));
            OnPropertyChanged(nameof(HasDialogueBox));
            OnPropertyChanged(nameof(HasBackground));
        }

        public bool HasDialogueBox => Components.OfType<DialogueComponentViewModel>().Any(d => d.IsEnabled);
        public bool HasBackground => Components.OfType<BackgroundComponentViewModel>().Any(b => b.IsEnabled);

        /// <summary>
        /// Moves a component up in the list (decreases its render order index).
        /// </summary>
        public void MoveComponentUp(NodeComponentViewModel component)
        {
            int idx = Components.IndexOf(component);
            if (idx > 0)
            {
                Components.Move(idx, idx - 1);
                OnPropertyChanged(nameof(Components));
                OnPropertyChanged(nameof(CharacterComponents));
            }
        }

        /// <summary>
        /// Moves a component down in the list (increases its render order index).
        /// </summary>
        public void MoveComponentDown(NodeComponentViewModel component)
        {
            int idx = Components.IndexOf(component);
            if (idx >= 0 && idx < Components.Count - 1)
            {
                Components.Move(idx, idx + 1);
                OnPropertyChanged(nameof(Components));
                OnPropertyChanged(nameof(CharacterComponents));
            }
        }

        // ── Component change propagation ──

        private void SubscribeComponentChanges(NodeComponentViewModel component)
        {
            component.PropertyChanged += OnComponentPropertyChanged;
        }

        private void UnsubscribeComponentChanges(NodeComponentViewModel component)
        {
            component.PropertyChanged -= OnComponentPropertyChanged;
        }

        private void OnComponentPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(NodeComponentViewModel.IsEnabled))
            {
                OnPropertyChanged(nameof(HasDialogueBox));
                OnPropertyChanged(nameof(HasBackground));
                OnPropertyChanged(nameof(Components));
            }

            // Sync proxy property change notifications without thrashing CharacterComponents collection
            if (sender is DialogueComponentViewModel)
            {
                if (e.PropertyName == nameof(DialogueComponentViewModel.Speaker))
                    OnPropertyChanged(nameof(Speaker));
                else if (e.PropertyName == nameof(DialogueComponentViewModel.DialogueText))
                    OnPropertyChanged(nameof(DialogueText));
                else if (e.PropertyName == nameof(DialogueComponentViewModel.X))
                    OnPropertyChanged(nameof(DialogueBoxX));
                else if (e.PropertyName == nameof(DialogueComponentViewModel.Y))
                    OnPropertyChanged(nameof(DialogueBoxY));
                else if (e.PropertyName == nameof(DialogueComponentViewModel.Width))
                    OnPropertyChanged(nameof(DialogueBoxWidth));
                else if (e.PropertyName == nameof(DialogueComponentViewModel.Height))
                    OnPropertyChanged(nameof(DialogueBoxHeight));
                else if (e.PropertyName == nameof(DialogueComponentViewModel.Scale))
                    OnPropertyChanged(nameof(DialogueBoxScale));
            }
            else if (sender is BackgroundComponentViewModel)
            {
                if (e.PropertyName == nameof(BackgroundComponentViewModel.Texture))
                    OnPropertyChanged(nameof(BackgroundTexture));
                else if (e.PropertyName == nameof(BackgroundComponentViewModel.X))
                    OnPropertyChanged(nameof(BackgroundX));
                else if (e.PropertyName == nameof(BackgroundComponentViewModel.Y))
                    OnPropertyChanged(nameof(BackgroundY));
                else if (e.PropertyName == nameof(BackgroundComponentViewModel.Width))
                    OnPropertyChanged(nameof(BackgroundWidth));
                else if (e.PropertyName == nameof(BackgroundComponentViewModel.Height))
                    OnPropertyChanged(nameof(BackgroundHeight));
                else if (e.PropertyName == nameof(BackgroundComponentViewModel.Scale))
                    OnPropertyChanged(nameof(BackgroundScale));
                else if (e.PropertyName == nameof(BackgroundComponentViewModel.TextureBitmap))
                    OnPropertyChanged(nameof(BackgroundBitmap));
            }
            else if (sender is CharacterComponentViewModel)
            {
                if (e.PropertyName == nameof(CharacterComponentViewModel.Sprite))
                    OnPropertyChanged(nameof(CharacterSprite));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.Position))
                    OnPropertyChanged(nameof(CharacterPosition));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.X))
                    OnPropertyChanged(nameof(CharacterX));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.Y))
                    OnPropertyChanged(nameof(CharacterY));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.Width))
                    OnPropertyChanged(nameof(CharacterWidth));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.Height))
                    OnPropertyChanged(nameof(CharacterHeight));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.Scale))
                    OnPropertyChanged(nameof(CharacterScale));
                else if (e.PropertyName == nameof(CharacterComponentViewModel.SpriteBitmap))
                    OnPropertyChanged(nameof(CharacterBitmap));
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

        // ── Speaker & Dialogue ──
        public string Speaker
        {
            get => GetComponent<DialogueComponentViewModel>()?.Speaker ?? "Evelyn";
            set { var c = GetComponent<DialogueComponentViewModel>(); if (c != null) c.Speaker = value; }
        }

        public string DialogueText
        {
            get => GetComponent<DialogueComponentViewModel>()?.DialogueText ?? "";
            set { var c = GetComponent<DialogueComponentViewModel>(); if (c != null) c.DialogueText = value; }
        }

        // ── Background ──
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

        public Bitmap? BackgroundBitmap => GetComponent<BackgroundComponentViewModel>()?.TextureBitmap;

        public IEnumerable<CharacterComponentViewModel> CharacterComponents => GetComponents<CharacterComponentViewModel>();

        // ── Character (first character component) ──
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

        public Bitmap? CharacterBitmap => GetComponent<CharacterComponentViewModel>()?.SpriteBitmap;

        // ── Dialogue Box ──
        public double DialogueBoxX
        {
            get => GetComponent<DialogueComponentViewModel>()?.X ?? 80;
            set { var d = GetComponent<DialogueComponentViewModel>(); if (d != null) d.X = value; }
        }

        public double DialogueBoxY
        {
            get => GetComponent<DialogueComponentViewModel>()?.Y ?? 860;
            set { var d = GetComponent<DialogueComponentViewModel>(); if (d != null) d.Y = value; }
        }

        public double DialogueBoxWidth
        {
            get => GetComponent<DialogueComponentViewModel>()?.Width ?? 1760;
            set { var d = GetComponent<DialogueComponentViewModel>(); if (d != null) d.Width = value; }
        }

        public double DialogueBoxHeight
        {
            get => GetComponent<DialogueComponentViewModel>()?.Height ?? 180;
            set { var d = GetComponent<DialogueComponentViewModel>(); if (d != null) d.Height = value; }
        }

        public double DialogueBoxScale
        {
            get => GetComponent<DialogueComponentViewModel>()?.Scale ?? 1.0;
            set { var d = GetComponent<DialogueComponentViewModel>(); if (d != null) d.Scale = value; }
        }

        // ── Audio ──
        public string DspFilter
        {
            get => GetComponent<AudioComponentViewModel>()?.DspFilter ?? "Normal";
            set { var c = GetComponent<AudioComponentViewModel>(); if (c != null) c.DspFilter = value; }
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  BITMAP REFRESH (legacy compat)  ██
        // ══════════════════════════════════════════════════════════════════════

        /// <summary>
        /// Refreshes bitmaps on all visual components.
        /// </summary>
        public void RefreshBitmaps()
        {
            GetComponent<BackgroundComponentViewModel>()?.RefreshBitmap();
            foreach (var charComp in GetComponents<CharacterComponentViewModel>())
                charComp.RefreshBitmap();
        }

        // ══════════════════════════════════════════════════════════════════════
        // ██  CONSTRUCTOR  ██
        // ══════════════════════════════════════════════════════════════════════

        /// <summary>
        /// Creates a new node with default components (Dialogue, Background, Character, Audio).
        /// </summary>
        public NodeViewModel(ulong id, string title, double x, double y)
        {
            Id = id;
            Title = title;
            X = x;
            Y = y;

            // Add default 4 modular components (Dialogue, Background, Character, Audio)
            AddComponent<DialogueComponentViewModel>();
            AddComponent<BackgroundComponentViewModel>();
            AddComponent<CharacterComponentViewModel>();
            AddComponent<AudioComponentViewModel>();

            // Trigger initial bitmap loads
            RefreshBitmaps();
        }

        /// <summary>
        /// Creates a bare node without any default components.
        /// Used during deserialization when components will be added manually.
        /// </summary>
        public NodeViewModel(ulong id, string title, double x, double y, bool bare)
        {
            Id = id;
            Title = title;
            X = x;
            Y = y;
            if (!bare)
            {
                AddComponent<DialogueComponentViewModel>();
                AddComponent<BackgroundComponentViewModel>();
                AddComponent<CharacterComponentViewModel>();
                AddComponent<AudioComponentViewModel>();
                RefreshBitmaps();
            }
        }
    }
}