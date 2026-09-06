using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using System;
using System.Linq;
using Avalonia.VisualTree;

namespace RowlEngine.Editor.Views
{
    public partial class LivePreviewControl : UserControl
    {
        // ── Constants for virtual canvas bounds ──
        private const double VirtualCanvasWidth = 1920.0;
        private const double VirtualCanvasHeight = 1080.0;
        private const double DragLimitPadding = 500.0;

        private enum ResizeTarget { None, Background, Character, DialogueBox }
        private enum ResizeCorner { TopLeft, TopRight, BottomLeft, BottomRight }

        private bool _isDraggingBackground = false;
        private bool _isDraggingCharacter = false;
        private bool _isDraggingDialogueBox = false;

        private bool _isResizing = false;
        private ResizeTarget _resizeTarget = ResizeTarget.None;
        private ResizeCorner _resizeCorner = ResizeCorner.BottomRight;

        private Point _dragStartPointerCanvasPos;
        private double _dragStartStartX;
        private double _dragStartStartY;
        private double _dragStartStartWidth;
        private double _dragStartStartHeight;

        // Individual Character resize state
        private bool _isResizingCharacter = false;
        private CharacterComponentViewModel? _resizingCharacterComponent;
        private Point _charResizeStartPointerCanvasPos;
        private double _charResizeStartWidth;
        private double _charResizeStartHeight;

        private CharacterComponentViewModel? _draggedCharacterComponent;

        // Individual Dialogue Box drag & resize state
        private bool _isResizingDialogue = false;
        private DialogueComponentViewModel? _resizingDialogueComponent;
        private DialogueComponentViewModel? _draggedDialogueComponent;
        private ResizeCorner _dialogueResizeCorner = ResizeCorner.BottomRight;
        private ChoiceOptionViewModel? _draggedChoiceOption;
        private ChoiceOptionViewModel? _resizingChoiceOption;
        private bool _isDraggingChoice;
        private bool _isResizingChoice;

        // Cached UI controls to avoid repeated visual tree searches during fast mouse moves
        private Canvas? _cachedCanvas;
        private Border? _cachedGuideV;
        private Border? _cachedGuideH;
        private Border? _cachedBadge;
        private TextBlock? _cachedBadgeText;

        public LivePreviewControl()
        {
            InitializeComponent();

            _cachedCanvas = this.FindControl<Canvas>("ViewportCanvas");
            _cachedGuideV = this.FindControl<Border>("SnapGuideV");
            _cachedGuideH = this.FindControl<Border>("SnapGuideH");
            _cachedBadge = this.FindControl<Border>("SnapBadge");
            _cachedBadgeText = this.FindControl<TextBlock>("SnapBadgeText");

            // Hook unified pointer events on the root control
            PointerMoved += OnGlobalPointerMoved;
            PointerReleased += OnGlobalPointerReleased;
            PointerCaptureLost += OnGlobalPointerCaptureLost;

            var bgBox = this.FindControl<Border>("BackgroundBox");
            if (bgBox != null)
            {
                bgBox.PointerPressed += OnBackgroundPointerPressed;
            }

            AddHandler(DragDrop.DragOverEvent, OnCanvasDragOver);
            AddHandler(DragDrop.DropEvent, OnCanvasDrop);

            // Bind Resize Handles
            BindHandle("BgHandleBR", ResizeTarget.Background, ResizeCorner.BottomRight);
        }

        private void OnCanvasDragOver(object? sender, DragEventArgs e)
        {
            if (e.Data.Contains("AssetNode") || e.Data.Contains("AssetFileName") || e.Data.Contains(DataFormats.Files) || e.Data.Contains(DataFormats.Text))
            {
                e.DragEffects = DragDropEffects.Copy;
                e.Handled = true;
            }
        }

        private void OnCanvasDrop(object? sender, DragEventArgs e)
        {
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;

            string? importedFileName = null;
            if (e.Data.Get("AssetNode") is AssetNodeViewModel node)
            {
                importedFileName = node.RelativePath.Replace('\\', '/');
            }
            else if (e.Data.Get("AssetFileName") is string fileName)
            {
                importedFileName = fileName;
            }
            else if (e.Data.Contains(DataFormats.Files))
            {
                var files = e.Data.GetFiles();
                if (files != null && files.Any())
                {
                    string fullPath = files.First().Path.LocalPath;
                    importedFileName = mainVm.ImportImageFileToProject(fullPath);
                }
            }
            else if (e.Data.Contains(DataFormats.Text))
            {
                string? text = e.Data.GetText();
                if (!string.IsNullOrEmpty(text))
                    importedFileName = System.IO.Path.GetFileName(text);
            }

            if (!string.IsNullOrEmpty(importedFileName))
            {
                string ext = System.IO.Path.GetExtension(importedFileName).ToLowerInvariant();
                var droppedChoice = FindChoiceOption(e.Source);
                if (droppedChoice != null && ext is ".ttf" or ".otf")
                {
                    droppedChoice.FontFamily = importedFileName;
                    mainVm.ScheduleSave();
                    e.Handled = true;
                    return;
                }
                if (ext is ".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga")
                {
                    var choiceOption = droppedChoice;
                    if (choiceOption != null)
                    {
                        choiceOption.BackgroundImage = importedFileName;
                        mainVm.AssetBrowserViewModel.RefreshAssets();
                        mainVm.ScheduleSave();
                        e.Handled = true;
                        return;
                    }
                    var canvas = GetViewportCanvas();
                    var pos = canvas != null ? e.GetPosition(canvas) : new Point(960, 540);

                    var charComp = mainVm.SelectedNode.GetComponent<CharacterComponentViewModel>();
                    var bgComp = mainVm.SelectedNode.GetComponent<BackgroundComponentViewModel>();

                    if (pos.X > 800 && charComp != null)
                    {
                        charComp.Sprite = importedFileName;
                        charComp.RefreshBitmap();
                    }
                    else if (bgComp != null)
                    {
                        bgComp.Texture = importedFileName;
                        bgComp.RefreshBitmap();
                    }
                    else if (charComp != null)
                    {
                        charComp.Sprite = importedFileName;
                        charComp.RefreshBitmap();
                    }

                    mainVm.AssetBrowserViewModel.RefreshAssets();
                    mainVm.ScheduleSave();
                    if (mainVm.SelectedNode != null)
                        mainVm.PushSceneToEngine(mainVm.SelectedNode);

                    e.Handled = true;
                }
            }
        }

        private void OnChoiceButtonDragOver(object? sender, DragEventArgs e)
        {
            if (e.Data.Contains("AssetNode") || e.Data.Contains("AssetFileName") || e.Data.Contains(DataFormats.Files))
            {
                e.DragEffects = DragDropEffects.Copy;
                e.Handled = true;
            }
        }

        private void OnChoiceButtonDrop(object? sender, DragEventArgs e)
        {
            if (sender is not Control { DataContext: ChoiceOptionViewModel option } || DataContext is not MainWindowViewModel mainVm) return;
            string? fileName = e.Data.Get("AssetNode") is AssetNodeViewModel asset
                ? asset.RelativePath.Replace('\\', '/')
                : e.Data.Get("AssetFileName") as string;
            if (string.IsNullOrEmpty(fileName) && e.Data.Contains(DataFormats.Files))
            {
                var file = e.Data.GetFiles()?.FirstOrDefault();
                if (file != null) fileName = mainVm.ImportImageFileToProject(file.Path.LocalPath);
            }
            if (string.IsNullOrEmpty(fileName)) return;
            var extension = System.IO.Path.GetExtension(fileName).ToLowerInvariant();
            if (extension is not (".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga")) return;
            option.BackgroundImage = fileName;
            mainVm.AssetBrowserViewModel.RefreshAssets();
            mainVm.ScheduleSave();
            e.Handled = true;
        }

        private void BindHandle(string name, ResizeTarget target, ResizeCorner corner)
        {
            var handle = this.FindControl<Border>(name);
            if (handle != null)
            {
                handle.PointerPressed += (s, e) => StartResize(e, target, corner);
            }
        }

        private Canvas? GetViewportCanvas()
        {
            return _cachedCanvas ??= this.FindControl<Canvas>("ViewportCanvas");
        }

        private bool HasActiveOperation =>
            _isResizing || _isResizingCharacter || _isResizingDialogue || _isDraggingCharacter || _isDraggingDialogueBox || _isDraggingBackground || _isDraggingChoice || _isResizingChoice;

        private void EndAllOperations(IPointer? pointer)
        {
            HideSnapGuides();
            if (!HasActiveOperation) return;

            _isResizing = false;
            _resizeTarget = ResizeTarget.None;
            _isResizingCharacter = false;
            _resizingCharacterComponent = null;
            _isResizingDialogue = false;
            _resizingDialogueComponent = null;
            _isDraggingCharacter = false;
            _draggedCharacterComponent = null;
            _isDraggingDialogueBox = false;
            _draggedDialogueComponent = null;
            _isDraggingBackground = false;
            _isDraggingChoice = false;
            _isResizingChoice = false;
            _draggedChoiceOption = null;
            _resizingChoiceOption = null;

            pointer?.Capture(null);

            if (DataContext is MainWindowViewModel mainVm)
            {
                mainVm.IsInteractivelyDragging = false;
                if (mainVm.SelectedNode != null)
                {
                    mainVm.PushSceneToEngine(mainVm.SelectedNode);
                }
                mainVm.ScheduleSave();
            }
        }

        private void OnGlobalPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
        {
            EndAllOperations(e.Pointer);
        }

        private void OnGlobalPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            EndAllOperations(e.Pointer);
            e.Handled = true;
        }

        private void OnGlobalPointerMoved(object? sender, PointerEventArgs e)
        {
            if (!HasActiveOperation) return;

            // Strict Drag & Drop: If left mouse button is NOT currently pressed, terminate drag immediately!
            var pt = e.GetCurrentPoint(this);
            if (!pt.Properties.IsLeftButtonPressed)
            {
                EndAllOperations(e.Pointer);
                return;
            }

            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null)
            {
                EndAllOperations(e.Pointer);
                return;
            }

            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            var currentPointerPos = e.GetPosition(canvas);
            double deltaX = currentPointerPos.X - _dragStartPointerCanvasPos.X;
            double deltaY = currentPointerPos.Y - _dragStartPointerCanvasPos.Y;
            var node = mainVm.SelectedNode;

            if (_isDraggingChoice && _draggedChoiceOption != null)
            {
                _draggedChoiceOption.X = Math.Clamp(_dragStartStartX + deltaX, 0, VirtualCanvasWidth - _draggedChoiceOption.Width);
                _draggedChoiceOption.Y = Math.Clamp(_dragStartStartY + deltaY, 0, VirtualCanvasHeight - _draggedChoiceOption.Height);
                return;
            }
            if (_isResizingChoice && _resizingChoiceOption != null)
            {
                _resizingChoiceOption.Width = Math.Clamp(_dragStartStartWidth + deltaX, 48, VirtualCanvasWidth - _resizingChoiceOption.X);
                _resizingChoiceOption.Height = Math.Clamp(_dragStartStartHeight + deltaY, 48, VirtualCanvasHeight - _resizingChoiceOption.Y);
                return;
            }

            // 1. Dialogue Box / Background / Generic Resize
            if (_isResizing)
            {
                double newX = _dragStartStartX;
                double newY = _dragStartStartY;
                double newW = _dragStartStartWidth;
                double newH = _dragStartStartHeight;

                switch (_resizeCorner)
                {
                    case ResizeCorner.BottomRight:
                        newW = Math.Max(60, _dragStartStartWidth + deltaX);
                        newH = Math.Max(60, _dragStartStartHeight + deltaY);
                        break;
                    case ResizeCorner.BottomLeft:
                        newW = Math.Max(60, _dragStartStartWidth - deltaX);
                        newX = _dragStartStartX + (_dragStartStartWidth - newW);
                        newH = Math.Max(60, _dragStartStartHeight + deltaY);
                        break;
                    case ResizeCorner.TopRight:
                        newW = Math.Max(60, _dragStartStartWidth + deltaX);
                        newH = Math.Max(60, _dragStartStartHeight - deltaY);
                        newY = _dragStartStartY + (_dragStartStartHeight - newH);
                        break;
                    case ResizeCorner.TopLeft:
                        newW = Math.Max(60, _dragStartStartWidth - deltaX);
                        newX = _dragStartStartX + (_dragStartStartWidth - newW);
                        newH = Math.Max(60, _dragStartStartHeight - deltaY);
                        newY = _dragStartStartY + (_dragStartStartHeight - newH);
                        break;
                }

                if (_resizeTarget == ResizeTarget.DialogueBox)
                {
                    node.DialogueBoxX = newX;
                    node.DialogueBoxY = newY;
                    node.DialogueBoxWidth = newW;
                    node.DialogueBoxHeight = newH;
                }
                else if (_resizeTarget == ResizeTarget.Background)
                {
                    node.BackgroundX = newX;
                    node.BackgroundY = newY;
                    node.BackgroundWidth = newW;
                    node.BackgroundHeight = newH;
                }
                e.Handled = true;
                return;
            }

            // 2. Dialogue Component Resize
            if (_isResizingDialogue && _resizingDialogueComponent != null)
            {
                double newX = _dragStartStartX;
                double newY = _dragStartStartY;
                double newW = _dragStartStartWidth;
                double newH = _dragStartStartHeight;

                switch (_dialogueResizeCorner)
                {
                    case ResizeCorner.BottomRight:
                        newW = Math.Max(120, _dragStartStartWidth + deltaX);
                        newH = Math.Max(60, _dragStartStartHeight + deltaY);
                        break;
                    case ResizeCorner.BottomLeft:
                        newW = Math.Max(120, _dragStartStartWidth - deltaX);
                        newX = _dragStartStartX + (_dragStartStartWidth - newW);
                        newH = Math.Max(60, _dragStartStartHeight + deltaY);
                        break;
                    case ResizeCorner.TopRight:
                        newW = Math.Max(120, _dragStartStartWidth + deltaX);
                        newH = Math.Max(60, _dragStartStartHeight - deltaY);
                        newY = _dragStartStartY + (_dragStartStartHeight - newH);
                        break;
                    case ResizeCorner.TopLeft:
                        newW = Math.Max(120, _dragStartStartWidth - deltaX);
                        newX = _dragStartStartX + (_dragStartStartWidth - newW);
                        newH = Math.Max(60, _dragStartStartHeight - deltaY);
                        newY = _dragStartStartY + (_dragStartStartHeight - newH);
                        break;
                }

                _resizingDialogueComponent.X = newX;
                _resizingDialogueComponent.Y = newY;
                _resizingDialogueComponent.Width = newW;
                _resizingDialogueComponent.Height = newH;
                e.Handled = true;
                return;
            }

            // 3. Character Component Resize
            if (_isResizingCharacter && _resizingCharacterComponent != null)
            {
                double charDeltaX = currentPointerPos.X - _charResizeStartPointerCanvasPos.X;
                double charDeltaY = currentPointerPos.Y - _charResizeStartPointerCanvasPos.Y;
                _resizingCharacterComponent.Width = Math.Max(60, _charResizeStartWidth + charDeltaX);
                _resizingCharacterComponent.Height = Math.Max(60, _charResizeStartHeight + charDeltaY);
                e.Handled = true;
                return;
            }

            // 4. Character Component Drag
            if (_isDraggingCharacter && _draggedCharacterComponent != null)
            {
                double targetX = _dragStartStartX + deltaX;
                double targetY = _dragStartStartY + deltaY;

                if (mainVm.IsSnapAssistEnabled && !e.KeyModifiers.HasFlag(KeyModifiers.Shift))
                {
                    (targetX, targetY) = ApplySnapping(targetX, targetY, _draggedCharacterComponent.Width, _draggedCharacterComponent.Height);
                }
                else
                {
                    HideSnapGuides();
                }

                _draggedCharacterComponent.X = Math.Clamp(targetX, -DragLimitPadding, VirtualCanvasWidth);
                _draggedCharacterComponent.Y = Math.Clamp(targetY, -DragLimitPadding, VirtualCanvasHeight);
                e.Handled = true;
                return;
            }

            // 5. Dialogue Box Drag
            if (_isDraggingDialogueBox)
            {
                var targetDlg = _draggedDialogueComponent ?? node.PrimaryDialogueComponent;
                double currentW = targetDlg?.Width ?? node.DialogueBoxWidth;
                double currentH = targetDlg?.Height ?? node.DialogueBoxHeight;

                double targetX = _dragStartStartX + deltaX;
                double targetY = _dragStartStartY + deltaY;

                if (mainVm.IsSnapAssistEnabled && !e.KeyModifiers.HasFlag(KeyModifiers.Shift))
                {
                    (targetX, targetY) = ApplySnapping(targetX, targetY, currentW, currentH);
                }
                else
                {
                    HideSnapGuides();
                }

                double clampedX = Math.Clamp(targetX, -DragLimitPadding, VirtualCanvasWidth);
                double clampedY = Math.Clamp(targetY, -DragLimitPadding, VirtualCanvasHeight);

                if (targetDlg != null)
                {
                    targetDlg.X = clampedX;
                    targetDlg.Y = clampedY;
                }
                else
                {
                    node.DialogueBoxX = clampedX;
                    node.DialogueBoxY = clampedY;
                }
                e.Handled = true;
                return;
            }

            // 5. Background Drag
            if (_isDraggingBackground)
            {
                double targetX = _dragStartStartX + deltaX;
                double targetY = _dragStartStartY + deltaY;

                if (mainVm.IsSnapAssistEnabled && !e.KeyModifiers.HasFlag(KeyModifiers.Shift))
                {
                    (targetX, targetY) = ApplySnapping(targetX, targetY, node.BackgroundWidth, node.BackgroundHeight);
                }
                else
                {
                    HideSnapGuides();
                }

                node.BackgroundX = targetX;
                node.BackgroundY = targetY;
                e.Handled = true;
                return;
            }
        }

        private static ChoiceOptionViewModel? FindChoiceOption(object? source)
        {
            for (var visual = source as Visual; visual != null; visual = visual.GetVisualParent())
                if (visual is Control { DataContext: ChoiceOptionViewModel option }) return option;
            return null;
        }

        private void OnChoiceButtonPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (sender is not Control { DataContext: ChoiceOptionViewModel option } ||
                !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;
            _draggedChoiceOption = option;
            _isDraggingChoice = true;
            _dragStartPointerCanvasPos = e.GetPosition(canvas);
            _dragStartStartX = option.X;
            _dragStartStartY = option.Y;
            e.Pointer.Capture(this);
            if (DataContext is MainWindowViewModel vm) vm.IsInteractivelyDragging = true;
            e.Handled = true;
        }

        private void OnChoiceButtonResizePointerPressed(object? sender, PointerPressedEventArgs e)
        {
            var option = FindChoiceOption(e.Source);
            if (option == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;
            _resizingChoiceOption = option;
            _isResizingChoice = true;
            _dragStartPointerCanvasPos = e.GetPosition(canvas);
            _dragStartStartWidth = option.Width;
            _dragStartStartHeight = option.Height;
            e.Pointer.Capture(this);
            if (DataContext is MainWindowViewModel vm) vm.IsInteractivelyDragging = true;
            e.Handled = true;
        }

        private const double SnapThreshold = 22.0;

        private void HideSnapGuides()
        {
            var guideV = _cachedGuideV ??= this.FindControl<Border>("SnapGuideV");
            var guideH = _cachedGuideH ??= this.FindControl<Border>("SnapGuideH");
            var badge = _cachedBadge ??= this.FindControl<Border>("SnapBadge");

            if (guideV != null) guideV.IsVisible = false;
            if (guideH != null) guideH.IsVisible = false;
            if (badge != null) badge.IsVisible = false;
        }

        private void UpdateSnapVisuals(bool snapH, double guideX, bool snapV, double guideY, string snapInfo, double itemX, double itemY)
        {
            var guideV = _cachedGuideV ??= this.FindControl<Border>("SnapGuideV");
            var guideH = _cachedGuideH ??= this.FindControl<Border>("SnapGuideH");
            var badge = _cachedBadge ??= this.FindControl<Border>("SnapBadge");
            var badgeText = _cachedBadgeText ??= this.FindControl<TextBlock>("SnapBadgeText");

            if (guideV != null)
            {
                guideV.IsVisible = snapH;
                if (snapH) Canvas.SetLeft(guideV, guideX);
            }

            if (guideH != null)
            {
                guideH.IsVisible = snapV;
                if (snapV) Canvas.SetTop(guideH, guideY);
            }

            if (badge != null && badgeText != null)
            {
                bool hasSnap = snapH || snapV;
                badge.IsVisible = hasSnap;
                if (hasSnap)
                {
                    badgeText.Text = snapInfo;
                    Canvas.SetLeft(badge, Math.Clamp(itemX + 15, 20, VirtualCanvasWidth - 300));
                    Canvas.SetTop(badge, Math.Clamp(itemY - 35, 90, VirtualCanvasHeight - 60));
                }
            }
        }

        private (double x, double y) ApplySnapping(double rawX, double rawY, double width, double height)
        {
            double snappedX = rawX;
            double snappedY = rawY;
            bool snappedHorizontally = false;
            bool snappedVertically = false;
            double guideLineX = 0;
            double guideLineY = 0;
            string snapInfo = "";

            // --- X AXIS SNAPPING ---
            // 1. Canvas Left Edge (0)
            if (Math.Abs(rawX - 0.0) < SnapThreshold)
            {
                snappedX = 0.0;
                snappedHorizontally = true;
                guideLineX = 0.0;
                snapInfo += "🧲 Sol Kenar (0) ";
            }
            // 2. Canvas Horizontal Center ((1920 - width) / 2)
            else if (Math.Abs(rawX - ((VirtualCanvasWidth - width) / 2.0)) < SnapThreshold)
            {
                snappedX = (VirtualCanvasWidth - width) / 2.0;
                snappedHorizontally = true;
                guideLineX = VirtualCanvasWidth / 2.0;
                snapInfo += "🧲 Yatay Merkez (960) ";
            }
            // 3. Canvas Right Edge (1920 - width)
            else if (Math.Abs(rawX - (VirtualCanvasWidth - width)) < SnapThreshold)
            {
                snappedX = VirtualCanvasWidth - width;
                snappedHorizontally = true;
                guideLineX = VirtualCanvasWidth;
                snapInfo += "🧲 Sağ Kenar (1920) ";
            }
            // 4. Safe Margin Left (60)
            else if (Math.Abs(rawX - 60.0) < SnapThreshold)
            {
                snappedX = 60.0;
                snappedHorizontally = true;
                guideLineX = 60.0;
                snapInfo += "🧲 Kenar Payı (60) ";
            }
            // 5. Safe Margin Right (1920 - width - 60)
            else if (Math.Abs(rawX - (VirtualCanvasWidth - width - 60.0)) < SnapThreshold)
            {
                snappedX = VirtualCanvasWidth - width - 60.0;
                snappedHorizontally = true;
                guideLineX = VirtualCanvasWidth - 60.0;
                snapInfo += "🧲 Kenar Payı (1860) ";
            }

            // --- Y AXIS SNAPPING ---
            // 1. Canvas Top Edge (0)
            if (Math.Abs(rawY - 0.0) < SnapThreshold)
            {
                snappedY = 0.0;
                snappedVertically = true;
                guideLineY = 0.0;
                snapInfo += "🧲 Üst Kenar (0) ";
            }
            // 2. Canvas Vertical Center ((1080 - height) / 2)
            else if (Math.Abs(rawY - ((VirtualCanvasHeight - height) / 2.0)) < SnapThreshold)
            {
                snappedY = (VirtualCanvasHeight - height) / 2.0;
                snappedVertically = true;
                guideLineY = VirtualCanvasHeight / 2.0;
                snapInfo += "🧲 Dikey Merkez (540) ";
            }
            // 3. Canvas Bottom Edge (1080 - height)
            else if (Math.Abs(rawY - (VirtualCanvasHeight - height)) < SnapThreshold)
            {
                snappedY = VirtualCanvasHeight - height;
                snappedVertically = true;
                guideLineY = VirtualCanvasHeight;
                snapInfo += "🧲 Alt Kenar (1080) ";
            }
            // 4. Ground Baseline (1080 - height - 30)
            else if (Math.Abs(rawY - (VirtualCanvasHeight - height - 30.0)) < SnapThreshold)
            {
                snappedY = VirtualCanvasHeight - height - 30.0;
                snappedVertically = true;
                guideLineY = VirtualCanvasHeight - 30.0;
                snapInfo += "🧲 Zemin Çizgisi ";
            }

            UpdateSnapVisuals(snappedHorizontally, guideLineX, snappedVertically, guideLineY, snapInfo.Trim(), snappedX, snappedY);

            return (snappedX, snappedY);
        }

        private void StartResize(PointerPressedEventArgs e, ResizeTarget target, ResizeCorner corner)
        {
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                _isResizing = true;
                _resizeTarget = target;
                _resizeCorner = corner;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);

                mainVm.IsInteractivelyDragging = true;

                var node = mainVm.SelectedNode;
                if (target == ResizeTarget.DialogueBox)
                {
                    _dragStartStartX = node.DialogueBoxX;
                    _dragStartStartY = node.DialogueBoxY;
                    _dragStartStartWidth = node.DialogueBoxWidth;
                    _dragStartStartHeight = node.DialogueBoxHeight;
                }
                else if (target == ResizeTarget.Background)
                {
                    _dragStartStartX = node.BackgroundX;
                    _dragStartStartY = node.BackgroundY;
                    _dragStartStartWidth = node.BackgroundWidth;
                    _dragStartStartHeight = node.BackgroundHeight;
                }

                e.Pointer.Capture(this);
                e.Handled = true;
            }
        }

        private void OnBackgroundPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (HasActiveOperation) return;
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                var bgComp = mainVm.SelectedNode.GetComponent<BackgroundComponentViewModel>();
                if (bgComp?.OwnerObject != null && mainVm.HierarchyViewModel != null)
                {
                    mainVm.HierarchyViewModel.SelectedObject = bgComp.OwnerObject;
                }

                _isDraggingBackground = true;
                mainVm.IsInteractivelyDragging = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartStartX = mainVm.SelectedNode.BackgroundX;
                _dragStartStartY = mainVm.SelectedNode.BackgroundY;
                e.Pointer.Capture(this);
                e.Handled = true;
            }
        }

        public void OnCharacterPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (HasActiveOperation) return;
            if (sender is Control ctrl && ctrl.DataContext is CharacterComponentViewModel charComp)
            {
                if (!charComp.IsEnabled) return;
                var canvas = GetViewportCanvas();
                if (canvas == null) return;

                if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
                {
                    if (DataContext is MainWindowViewModel mainVm)
                    {
                        mainVm.IsInteractivelyDragging = true;
                        if (charComp.OwnerObject != null && mainVm.HierarchyViewModel != null)
                        {
                            mainVm.HierarchyViewModel.SelectedObject = charComp.OwnerObject;
                        }
                    }

                    _isDraggingCharacter = true;
                    _draggedCharacterComponent = charComp;
                    _dragStartPointerCanvasPos = e.GetPosition(canvas);
                    _dragStartStartX = charComp.X;
                    _dragStartStartY = charComp.Y;
                    e.Pointer.Capture(this);
                    e.Handled = true;
                }
            }
        }

        public void OnCharHandlePointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (HasActiveOperation) return;
            if (sender is Control ctrl && ctrl.DataContext is CharacterComponentViewModel charComp)
            {
                if (!charComp.IsEnabled) return;
                var canvas = GetViewportCanvas();
                if (canvas == null) return;

                if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
                {
                    _isResizingCharacter = true;
                    _resizingCharacterComponent = charComp;
                    _charResizeStartPointerCanvasPos = e.GetPosition(canvas);
                    _charResizeStartWidth = charComp.Width;
                    _charResizeStartHeight = charComp.Height;

                    if (DataContext is MainWindowViewModel mainVm)
                    {
                        mainVm.IsInteractivelyDragging = true;
                        if (charComp.OwnerObject != null && mainVm.HierarchyViewModel != null)
                        {
                            mainVm.HierarchyViewModel.SelectedObject = charComp.OwnerObject;
                        }
                    }

                    e.Pointer.Capture(this);
                    e.Handled = true;
                }
            }
        }

        public void OnDialogueBoxItemPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (HasActiveOperation) return;
            if (sender is Control ctrl && ctrl.DataContext is DialogueComponentViewModel dlgComp)
            {
                if (!dlgComp.IsEnabled) return;
                var canvas = GetViewportCanvas();
                if (canvas == null) return;

                if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
                {
                    if (DataContext is MainWindowViewModel mainVm)
                    {
                        mainVm.IsInteractivelyDragging = true;
                        if (dlgComp.OwnerObject != null && mainVm.HierarchyViewModel != null)
                        {
                            mainVm.HierarchyViewModel.SelectedObject = dlgComp.OwnerObject;
                        }
                    }

                    _isDraggingDialogueBox = true;
                    _draggedDialogueComponent = dlgComp;
                    _dragStartPointerCanvasPos = e.GetPosition(canvas);
                    _dragStartStartX = dlgComp.X;
                    _dragStartStartY = dlgComp.Y;
                    e.Pointer.Capture(this);
                    e.Handled = true;
                }
            }
        }

        public void OnDialogueHandleTLPointerPressed(object? sender, PointerPressedEventArgs e) =>
            StartDialogueResize(sender, e, ResizeCorner.TopLeft);

        public void OnDialogueHandleTRPointerPressed(object? sender, PointerPressedEventArgs e) =>
            StartDialogueResize(sender, e, ResizeCorner.TopRight);

        public void OnDialogueHandleBLPointerPressed(object? sender, PointerPressedEventArgs e) =>
            StartDialogueResize(sender, e, ResizeCorner.BottomLeft);

        public void OnDialogueHandleBRPointerPressed(object? sender, PointerPressedEventArgs e) =>
            StartDialogueResize(sender, e, ResizeCorner.BottomRight);

        private void StartDialogueResize(object? sender, PointerPressedEventArgs e, ResizeCorner corner)
        {
            if (HasActiveOperation) return;
            if (sender is Control ctrl && ctrl.DataContext is DialogueComponentViewModel dlgComp)
            {
                if (!dlgComp.IsEnabled) return;
                var canvas = GetViewportCanvas();
                if (canvas == null) return;

                if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
                {
                    _isResizingDialogue = true;
                    _resizingDialogueComponent = dlgComp;
                    _dialogueResizeCorner = corner;
                    _dragStartPointerCanvasPos = e.GetPosition(canvas);
                    _dragStartStartX = dlgComp.X;
                    _dragStartStartY = dlgComp.Y;
                    _dragStartStartWidth = dlgComp.Width;
                    _dragStartStartHeight = dlgComp.Height;

                    if (DataContext is MainWindowViewModel mainVm)
                    {
                        mainVm.IsInteractivelyDragging = true;
                        if (dlgComp.OwnerObject != null && mainVm.HierarchyViewModel != null)
                        {
                            mainVm.HierarchyViewModel.SelectedObject = dlgComp.OwnerObject;
                        }
                    }

                    e.Pointer.Capture(this);
                    e.Handled = true;
                }
            }
        }

        private void OnDialogueBoxPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (HasActiveOperation) return;
            if (DataContext is not MainWindowViewModel mainVm || mainVm.SelectedNode == null) return;
            var canvas = GetViewportCanvas();
            if (canvas == null) return;

            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                var dlgComp = mainVm.SelectedNode.PrimaryDialogueComponent;
                if (dlgComp?.OwnerObject != null && mainVm.HierarchyViewModel != null)
                {
                    mainVm.HierarchyViewModel.SelectedObject = dlgComp.OwnerObject;
                }

                _isDraggingDialogueBox = true;
                _draggedDialogueComponent = dlgComp;
                mainVm.IsInteractivelyDragging = true;
                _dragStartPointerCanvasPos = e.GetPosition(canvas);
                _dragStartStartX = dlgComp?.X ?? mainVm.SelectedNode.DialogueBoxX;
                _dragStartStartY = dlgComp?.Y ?? mainVm.SelectedNode.DialogueBoxY;
                e.Pointer.Capture(this);
                e.Handled = true;
            }
        }
    }
}
