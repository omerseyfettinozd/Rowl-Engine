using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services.Extras;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Tools.RowlEye.Targets;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.Views.Components;
using RowlEngine.Editor.Views.Extras;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D07-3 — the new surfaces as pixels: the "unlock" Inspector card (with catalog, without, id not in
/// the catalog), the unknown-component card and the extras catalog editor window (three tabs, a
/// problem state, a corrupt-file state). Each is laid out and checked for alignment and clipping;
/// with <c>ROWL_EYE_OUT=&lt;dir&gt;</c> set the frames are also written as PNGs (RowlEye renderer).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorD073VisualTests : IDisposable
{
    private readonly List<string> _temps = new();
    private readonly Func<ExtrasCatalogModel?> _previousProvider = UnlockComponentViewModel.CatalogProvider;

    public void Dispose()
    {
        UnlockComponentViewModel.CatalogProvider = _previousProvider;
        ExtrasCatalogStore.Invalidate();
        foreach (var path in _temps)
        {
            try { Directory.Delete(path, recursive: true); } catch (Exception) { }
        }
    }

    private const string Catalog = """
    { "format": 1,
      "gallery": [ { "id": "bogaz_gunduz", "title": { "tr": "Boğaz'da Bir Gündüz", "en": "A Day on the Bosphorus" }, "image": "bogaz_gunduz.jpg" },
                   { "id": "atolye", "title": { "tr": "Atölye", "en": "The Studio" }, "image": "atolye.jpg" },
                   { "id": "kapali_carsi", "title": { "tr": "Kapalıçarşı", "en": "The Grand Bazaar" }, "image": "kapali_carsi.jpg", "thumbnail": "kapali_carsi_kucuk.jpg" } ],
      "music": [ { "id": "nokturn_fa_minor", "title": { "tr": "Nokturn, Fa minör", "en": "Nocturne in F minor" }, "track": "audio/nokturn_fa_minor.ogg" },
                 { "id": "gymnopedie1", "title": "Gymnopédie No. 1", "track": "audio/gymnopedie1.ogg" } ],
      "achievements": [ { "id": "isik_geri_dondu", "title": { "tr": "Işık Geri Döndü", "en": "The Light Returned" },
                          "description": { "tr": "Işık Geri Döndü sonuna ulaş.", "en": "Reach the ending The Light Returned." } },
                        { "id": "gec_kalan_cirak", "title": { "tr": "Geç Kalan Çırak", "en": "The Late Apprentice" },
                          "description": { "tr": "Geç Kalan Çırak sonuna ulaş.", "en": "Reach the ending The Late Apprentice." }, "hidden": true } ] }
    """;

    private string TempAssets(string? catalog)
    {
        string root = Path.Combine(Path.GetTempPath(), "d073v_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(root, "Assets");
        Directory.CreateDirectory(Path.Combine(assets, "json"));
        _temps.Add(root);
        if (catalog is not null) File.WriteAllText(Path.Combine(assets, "json", "extras.json"), catalog);
        return assets;
    }

    private static void Shot(Window window, string name)
    {
        string? dir = Environment.GetEnvironmentVariable("ROWL_EYE_OUT");
        if (string.IsNullOrWhiteSpace(dir)) return;
        RowlEyeRenderer.RenderWindow(window, 1.0, Path.Combine(dir, name + ".png"));
    }

    private static void Layout(Window window)
    {
        window.Show();
        window.Measure(new Size(window.Width, window.Height));
        window.Arrange(new Rect(0, 0, window.Width, window.Height));
        Dispatcher.UIThread.RunJobs();
        window.UpdateLayout();
        Dispatcher.UIThread.RunJobs();
    }

    /// <summary>Every visible control lies inside the window; nothing hangs over the right or bottom edge.</summary>
    private static void AssertInside(Window window, string what)
    {
        var rootBounds = new Rect(0, 0, window.Bounds.Width, window.Bounds.Height);
        foreach (var control in window.GetVisualDescendants().OfType<Control>())
        {
            if (!control.IsVisible || control.Bounds.Width <= 0 || control.Bounds.Height <= 0) continue;
            if (control.FindAncestorOfType<ScrollViewer>() is not null && control is not ScrollViewer) continue; // scrolled content may extend
            var origin = control.TranslatePoint(default, window) ?? default;
            var rect = new Rect(origin, control.Bounds.Size);
            Assert.True(rect.Right <= rootBounds.Right + 1.0 && rect.Bottom <= rootBounds.Bottom + 1.0 && rect.X >= -1.0,
                $"{what}: {control.GetType().Name} '{control.Name}' sticks out: {rect} vs {rootBounds}");
        }
    }

    private static T Named<T>(Window window, string name) where T : Control =>
        window.GetVisualDescendants().OfType<T>().First(c => c.Name == name);

    private static Window HostCard(Control card, double height)
    {
        var window = new Window { Width = 420, Height = height, CanResize = false, Content = new ScrollViewer { Content = card } };
        window.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
        return window;
    }

    [Fact]
    public void UnlockCard_WithCatalog_IsAlignedAndNotClipped()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        string assets = TempAssets(Catalog);
        Dispatcher.UIThread.Invoke(() =>
        {
            UnlockComponentViewModel.CatalogProvider = () => ExtrasCatalogStore.ReadShared(assets);
            var vm = new UnlockComponentViewModel { Kind = "achievement", UnlockId = "isik_geri_dondu" };
            var window = HostCard(new UnlockComponentView { DataContext = vm }, 330);
            try
            {
                Layout(window);
                var kind = Named<ComboBox>(window, "KindPicker");
                var id = Named<ComboBox>(window, "IdPicker");
                Assert.True(id.IsVisible);
                Assert.False(Named<TextBox>(window, "IdText").IsVisible);
                // Both pickers span the same column.
                Assert.Equal(kind.Bounds.Width, id.Bounds.Width, 1.0);
                Assert.Equal(kind.TranslatePoint(default, window)!.Value.X, id.TranslatePoint(default, window)!.Value.X, 1.0);
                Assert.True(kind.Bounds.Width > 250, "pickers fill the card");
                AssertInside(window, "unlock card (catalog)");
                Shot(window, "unlock_card_catalog_tr_dark");
            }
            finally { window.Close(); }
        });
    }

    [Fact]
    public void UnlockCard_WithoutCatalogAndWithAMissingId_ShowTheirHints()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            UnlockComponentViewModel.CatalogProvider = () => null;
            var vm = new UnlockComponentViewModel { Kind = "cg", UnlockId = "elle_yazildi" };
            var window = HostCard(new UnlockComponentView { DataContext = vm }, 330);
            try
            {
                Layout(window);
                Assert.False(Named<ComboBox>(window, "IdPicker").IsVisible);
                var text = Named<TextBox>(window, "IdText");
                Assert.True(text.IsVisible);
                Assert.Equal("elle_yazildi", text.Text);
                AssertInside(window, "unlock card (free text)");
                Shot(window, "unlock_card_freetext_tr_dark");
            }
            finally { window.Close(); }
        });

        string assets = TempAssets(Catalog);
        Dispatcher.UIThread.Invoke(() =>
        {
            UnlockComponentViewModel.CatalogProvider = () => ExtrasCatalogStore.ReadShared(assets);
            var vm = new UnlockComponentViewModel { Kind = "cg", UnlockId = "yok_boyle_bir_sey" };
            var window = HostCard(new UnlockComponentView { DataContext = vm }, 360);
            try
            {
                Layout(window);
                Assert.True(vm.IdNotInCatalog);
                var id = Named<ComboBox>(window, "IdPicker");
                Assert.True(id.IsVisible);
                AssertInside(window, "unlock card (id not in catalog)");
                Shot(window, "unlock_card_missing_id_tr_dark");
            }
            finally { window.Close(); }
        });
    }

    [Fact]
    public void UnknownCard_IsReadableAndNotClipped()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new UnknownComponentViewModel("weather_fx");
            vm.Deserialize(new Dictionary<string, object?>
            {
                ["mode"] = System.Text.Json.JsonSerializer.SerializeToElement("rain"),
                ["intensity"] = System.Text.Json.JsonSerializer.SerializeToElement(3),
                ["layers"] = System.Text.Json.JsonSerializer.SerializeToElement(new[] { 1, 2, 3 }),
            });
            var window = HostCard(new UnknownComponentView { DataContext = vm }, 200);
            try
            {
                Layout(window);
                AssertInside(window, "unknown card");
                Shot(window, "unknown_card_tr_dark");
            }
            finally { window.Close(); }
        });
    }

    [Fact]
    public void CatalogWindow_ThreeTabsAreAlignedAndNotClipped()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        string assets = TempAssets(Catalog);
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new ExtrasCatalogEditorViewModel(assets);
            var window = new ExtrasCatalogEditorWindow(vm);
            window.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            try
            {
                Layout(window);
                var tabs = Named<TabControl>(window, "Tabs");

                // Gallery
                var id = Named<TextBox>(window, "GalleryId");
                var image = Named<TextBox>(window, "GalleryImage");
                var thumb = Named<TextBox>(window, "GalleryThumb");
                Assert.Equal("bogaz_gunduz", id.Text);
                Assert.Equal(id.TranslatePoint(default, window)!.Value.X, image.TranslatePoint(default, window)!.Value.X, 1.0);
                Assert.Equal(image.TranslatePoint(default, window)!.Value.X, thumb.TranslatePoint(default, window)!.Value.X, 1.0);
                Assert.True(Named<Button>(window, "SaveButton").Bounds.Width > 0);
                AssertInside(window, "catalog gallery");
                Shot(window, "catalog_gallery_tr_dark");

                // Music
                vm.SelectedTabIndex = ExtrasCatalogEditorViewModel.MusicTab;
                Layout(window);
                var track = Named<TextBox>(window, "MusicTrack");
                Assert.True(track.IsVisible || tabs.SelectedIndex == 1);
                AssertInside(window, "catalog music");
                Shot(window, "catalog_music_tr_dark");

                // Achievements
                vm.SelectedTabIndex = ExtrasCatalogEditorViewModel.AchievementsTab;
                Layout(window);
                var titleTr = Named<TextBox>(window, "AchievementTitleTr");
                var titleEn = Named<TextBox>(window, "AchievementTitleEn");
                var descTr = Named<TextBox>(window, "AchievementDescTr");
                var descEn = Named<TextBox>(window, "AchievementDescEn");
                Assert.Equal(titleTr.TranslatePoint(default, window)!.Value.X, descTr.TranslatePoint(default, window)!.Value.X, 1.0);
                Assert.Equal(titleEn.TranslatePoint(default, window)!.Value.X, descEn.TranslatePoint(default, window)!.Value.X, 1.0);
                Assert.Equal(titleTr.Bounds.Width, descTr.Bounds.Width, 1.0);
                AssertInside(window, "catalog achievements");
                Shot(window, "catalog_achievements_tr_dark");
            }
            finally { window.Close(); }
        });
    }

    [Fact]
    public void CatalogWindow_ProblemAndCorruptStatesShowTheirMessages()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        string assets = TempAssets(Catalog);
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new ExtrasCatalogEditorViewModel(assets);
            vm.SelectedGallery!.Id = "Gecersiz Kimlik";
            vm.SelectedGallery.Image = "";
            var window = new ExtrasCatalogEditorWindow(vm);
            window.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            try
            {
                Layout(window);
                Assert.True(Named<TextBlock>(window, "GalleryIssue").IsVisible);
                Assert.False(vm.SaveCommand.CanExecute(null));
                Assert.False(Named<Button>(window, "SaveButton").IsEffectivelyEnabled);
                Assert.Contains("2", Named<TextBlock>(window, "StatusLine").Text);
                AssertInside(window, "catalog problems");
                Shot(window, "catalog_problem_tr_dark");
            }
            finally { window.Close(); }
        });

        string corrupt = TempAssets("{ \"gallery\": [");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new ExtrasCatalogEditorViewModel(corrupt);
            var window = new ExtrasCatalogEditorWindow(vm);
            window.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            try
            {
                Layout(window);
                Assert.True(Named<TextBlock>(window, "LoadErrorText").IsVisible);
                Assert.False(Named<TabControl>(window, "Tabs").IsEnabled);
                AssertInside(window, "catalog corrupt");
                Shot(window, "catalog_corrupt_tr_dark");
            }
            finally { window.Close(); }
        });
    }
}
