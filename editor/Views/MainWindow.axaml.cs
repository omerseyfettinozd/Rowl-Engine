using System;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views
{
    public partial class MainWindow : Window
    {
        public MainWindow() : this(new MainWindowViewModel())
        {
        }

        public MainWindow(string projectPath) : this(CreateAppViewModel(projectPath))
        {
        }

        /// <summary>
        /// Faz 5 D5: the real app opens a project with the user's saved workspace
        /// layout and keeps saving it (loaded before the window binds, so it never
        /// flashes the default). Tests and RowlEye build the view model themselves
        /// and so never touch the user's profile.
        /// </summary>
        private static MainWindowViewModel CreateAppViewModel(string projectPath)
        {
            var vm = new MainWindowViewModel(projectPath);
            vm.AttachWorkspacePersistence();
            return vm;
        }

        public MainWindow(MainWindowViewModel vm)
        {
            InitializeComponent();
            DataContext = vm;
            vm.TopLevelHint = this;
            KeyDown += MainWindow_KeyDown;
            // Faz 8 D2: arayüz ölçeği canlı uygulanır; pencere kapanınca abonelik bırakılır.
            ApplyUiScale(EditorUiScale.Current);
            EditorUiScale.Changed += ApplyUiScale;
            Closed += (_, _) => EditorUiScale.Changed -= ApplyUiScale;
            // Bildirim kartları birlikte açılınca panelleri örtmesin: alan pencereye göre sınırlı.
            if (this.FindControl<StackPanel>("NotificationStack") is { Parent: Control shell } &&
                this.FindControl<ScrollViewer>("NotificationCardScroller") is { } cardScroller)
            {
                shell.PropertyChanged += (_, e) =>
                {
                    if (e.Property == BoundsProperty)
                        cardScroller.MaxHeight = NotificationStackMetrics.MaxCardsHeight(shell.Bounds.Height);
                };
            }
            // Faz 5 D3 — tek yön: bölücü taşınınca görünüm yalnız ağaç işlemini çağırır.
            WorkspaceHost.SplitResized += (_, e) =>
            {
                if (DataContext is MainWindowViewModel current)
                    current.ResizeWorkspaceSplit(e.SplitId, e.Ratio);
            };
            // Faz 5 D4 — başlıktan taşıma / sabitle / kapat: görünüm yalnız isteği iletir.
            WorkspaceHost.PanelDockRequested += (_, e) =>
            {
                if (DataContext is MainWindowViewModel current)
                    current.DockWorkspacePanel(e.SourcePanelId, e.TargetPanelId, e.Edge);
            };
            WorkspaceHost.PanelPinRequested += (_, e) =>
            {
                if (DataContext is MainWindowViewModel current)
                    current.SetWorkspacePanelPinned(e.PanelId, e.Pinned);
            };
            WorkspaceHost.PanelCloseRequested += (_, e) =>
            {
                if (DataContext is MainWindowViewModel current)
                    current.CloseWorkspacePanel(e.PanelId);
            };
            // Faz 4 Dilim 3 — breadcrumb navigation (Tag="CrumbNav" buttons).
            AddHandler(Button.ClickEvent, OnCrumbButtonClick,
                Avalonia.Interactivity.RoutingStrategies.Bubble);
            Closing += async (_, e) =>
            {
                if (_allowClose) return;
                if (!vm.IsProjectDirty || !IsVisible) return;
                e.Cancel = true;
                if (await vm.ResolveUnsavedChangesAsync(this))
                {
                    _allowClose = true;
                    Close();
                }
            };
            Closed += (_, _) => vm.Dispose();
        }

        private bool _allowClose;

        /// <summary>
        /// Faz 4 Dilim 3 — breadcrumb click navigates the subgraph depth
        /// stack (the button's DataContext is the crumb). Other buttons pass
        /// through untouched.
        /// </summary>
        private void OnCrumbButtonClick(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            if (e.Source is not Button button ||
                !string.Equals(button.Tag as string, "CrumbNav", System.StringComparison.Ordinal))
                return;
            if (button.DataContext is not Services.NavCrumb crumb)
                return;
            if (DataContext is not MainWindowViewModel vm)
                return;
            if (string.Equals(crumb.Kind, "root", System.StringComparison.Ordinal))
                vm.Subgraphs.GoToRoot();
            else
                vm.Subgraphs.GoToDepth(crumb.Depth);
            e.Handled = true;
        }

        private void ApplyUiScale(double scale)
        {
            if (!Avalonia.Threading.Dispatcher.UIThread.CheckAccess())
            {
                Avalonia.Threading.Dispatcher.UIThread.Post(() => ApplyUiScale(scale));
                return;
            }
            if (this.FindControl<LayoutTransformControl>("UiScaleHost") is { } host)
                host.LayoutTransform = Math.Abs(scale - 1.0) < 0.001 ? null : new ScaleTransform(scale, scale);
        }

        /// <summary>
        /// Faz 8 D1: F6 / Shift+F6 görünür paneller arasında ekran sırasıyla (üstten alta,
        /// soldan sağa) odak gezdirir; panelin ilk odaklanabilir öğesi klavye odağı alır
        /// (odak halkası görünür). Döner: odak taşındı mı.
        /// </summary>
        internal bool FocusAdjacentPanel(bool backwards)
        {
            var panels = this.GetVisualDescendants().OfType<Workspace.WorkspacePanel>()
                .Where(p => p.IsEffectivelyVisible && !string.IsNullOrEmpty(p.PanelId) && p.Bounds.Width > 0)
                .Select(p => (Panel: p, At: p.TranslatePoint(default, this) ?? default))
                .OrderBy(x => Math.Round(x.At.Y / 8)).ThenBy(x => x.At.X)
                .Select(x => x.Panel)
                .ToList();
            var focused = FocusManager?.GetFocusedElement() as Visual;
            string? current = focused?.GetSelfAndVisualAncestors().OfType<Workspace.WorkspacePanel>().FirstOrDefault()?.PanelId;
            string? next = WorkspaceFocusOrder.Next(panels.Select(p => p.PanelId!).ToList(), current, backwards);
            var target = panels.FirstOrDefault(p => p.PanelId == next);
            if (target is null)
                return false;
            var input = target.GetVisualDescendants().OfType<InputElement>()
                .FirstOrDefault(c => c.Focusable && c.IsEffectivelyVisible && c.IsEffectivelyEnabled);
            return input?.Focus(NavigationMethod.Tab) ?? false;
        }

        private void MainWindow_KeyDown(object? sender, KeyEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;

            bool ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
            bool shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);

            // Faz 8 D1: panel odak döngüsü (metin kutusunda da çalışır: F6 metin yazmaz).
            if (e.Key == Key.F6 && !ctrl)
            {
                FocusAdjacentPanel(backwards: shift);
                e.Handled = true;
                return;
            }

            // MS-5: canvas shortcuts must not steal keystrokes from text inputs.
            bool isTextInput = e.Source is Avalonia.Controls.TextBox;

            // ── Undo / Redo ──────────────────────────────────────────
            if (ctrl && !shift && e.Key == Key.Z)
            {
                vm.UndoCommand.Execute(null);
                e.Handled = true;
            }
            else if ((ctrl && e.Key == Key.Y) || (ctrl && shift && e.Key == Key.Z))
            {
                vm.RedoCommand.Execute(null);
                e.Handled = true;
            }

            // ── Dosya İşlemleri ─────────────────────────────────────
            else if (ctrl && !shift && e.Key == Key.S)
            {
                vm.SaveProjectCommand.Execute(null);
                e.Handled = true;
            }
            else if (ctrl && shift && e.Key == Key.S)
            {
                _ = vm.SaveProjectAsCommand.ExecuteAsync(null);
                e.Handled = true;
            }

            // ── Node İşlemleri ──────────────────────────────────────
            else if (ctrl && e.Key == Key.N)
            {
                vm.AddNodeCommand.Execute(null);
                e.Handled = true;
            }
            else if (ctrl && !isTextInput && e.Key == Key.A)
            {
                vm.SelectAllNodesCommand.Execute(null);
                e.Handled = true;
            }
            else if (ctrl && !isTextInput && e.Key == Key.C)
            {
                vm.CopySelectedNodesCommand.Execute(null);
                e.Handled = true;
            }
            else if (ctrl && !isTextInput && e.Key == Key.V)
            {
                vm.PasteClipboardNodesCommand.Execute(null);
                e.Handled = true;
            }
            else if (ctrl && !isTextInput && e.Key == Key.D)
            {
                vm.DuplicateSelectedNodesCommand.Execute(null);
                e.Handled = true;
            }
            else if (!ctrl && !isTextInput && (e.Key == Key.Left || e.Key == Key.Right || e.Key == Key.Up || e.Key == Key.Down))
            {
                double step = shift ? 10.0 : 1.0;
                double dx = e.Key == Key.Left ? -step : e.Key == Key.Right ? step : 0.0;
                double dy = e.Key == Key.Up ? -step : e.Key == Key.Down ? step : 0.0;
                vm.NudgeSelectedNodes(dx, dy);
                e.Handled = true;
            }
            else if (e.Key == Key.Delete)
            {
                vm.DeleteSelectedNodeCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.F2)
            {
                vm.AssetBrowserViewModel?.StartRenameCommand?.Execute(null);
                e.Handled = true;
            }

            // ── Build & Play ────────────────────────────────────────
            else if (ctrl && e.Key == Key.B)
            {
                _ = vm.BuildGameCommand.ExecuteAsync(null);
                e.Handled = true;
            }
            else if (e.Key == Key.F5)
            {
                vm.TogglePlayStandaloneCommand.Execute(null);
                e.Handled = true;
            }
            else if (vm.IsPlayingStandalone && (e.Key == Key.Space || e.Key == Key.Enter))
            {
                if (vm.EngineHost.IsInitialized)
                {
                    vm.EngineHost.AdvanceNode(0);
                    ulong currentNodeId = vm.EngineHost.GetCurrentNodeId();
                    if (currentNodeId != 0)
                    {
                        var matchNode = System.Linq.Enumerable.FirstOrDefault(vm.Nodes, n => n.Id == currentNodeId);
                        if (matchNode != null)
                        {
                            vm.SelectNodeQuiet(matchNode);
                        }
                    }
                }
                e.Handled = true;
            }

            // ── Arama (Faz 4 Dilim 2: sonuç listesi + filtreler) ─────
            else if (ctrl && e.Key == Key.F)
            {
                vm.ToggleSearchCommand.Execute(null);
                e.Handled = true;
            }
            else if (vm.IsSearchVisible && isTextInput && e.Key == Key.Down)
            {
                vm.Search.MoveSelection(1);
                e.Handled = true;
            }
            else if (vm.IsSearchVisible && isTextInput && e.Key == Key.Up)
            {
                vm.Search.MoveSelection(-1);
                e.Handled = true;
            }
            else if (vm.IsSearchVisible && e.Key == Key.Enter && !vm.IsPlayingStandalone)
            {
                vm.Search.JumpToSelectedCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.Escape && vm.IsDraggingWire)
            {
                vm.CancelWireDrag();
                e.Handled = true;
            }
            else if (e.Key == Key.Escape && vm.IsSearchVisible)
            {
                vm.IsSearchVisible = false;
                vm.SearchQuery = "";
                vm.Search.ClearHighlight();
                e.Handled = true;
            }

            // ── Paneller (Faz 5: Pencereler menüsü kısayolları Ctrl+1..9; Faz 7: 8 kütüphane, 9 Lua) ──
            else if (ctrl && !shift && !isTextInput && TryPanelShortcut(e.Key, out string? panel))
            {
                vm.ShowPanel(panel!);
                e.Handled = true;
            }

            // ── Tam Ekran ───────────────────────────────────────────
            else if (e.Key == Key.F11)
            {
                vm.ToggleFullscreenCommand.Execute(null);
                e.Handled = true;
            }

            // ── Ayarlar ─────────────────────────────────────────────
            else if (ctrl && e.Key == Key.OemComma)
            {
                _ = vm.OpenSettingsCommand.ExecuteAsync(null);
                e.Handled = true;
            }

            // ── Hub ─────────────────────────────────────────────────
            else if (ctrl && e.Key == Key.H)
            {
                vm.OpenProjectHubCommand.Execute(null);
                e.Handled = true;
            }
        }

        /// <summary>
        /// Faz 5: Pencereler menüsü kısayolları (Ctrl+1..9, menü sırasıyla; Faz 7: Ctrl+8 Düğüm kütüphanesi, Ctrl+9 Lua editörü).
        /// Metin girişinde çalışmaz (MS-5 kuralı).
        /// </summary>
        internal static bool TryPanelShortcut(Key key, out string? panel)
        {
            panel = key switch
            {
                Key.D1 or Key.NumPad1 => "Hierarchy",
                Key.D2 or Key.NumPad2 => "Inspector",
                Key.D3 or Key.NumPad3 => "Log",
                Key.D4 or Key.NumPad4 => "Assets",
                Key.D5 or Key.NumPad5 => "Backlog",
                Key.D6 or Key.NumPad6 => "SaveSlots",
                Key.D7 or Key.NumPad7 => "ProjectIssues",
                Key.D8 or Key.NumPad8 => "Library",
                Key.D9 or Key.NumPad9 => "Lua",
                _ => null,
            };
            return panel != null;
        }
    }
}
