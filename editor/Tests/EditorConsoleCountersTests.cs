using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Unity kromu Dilim E kilidi: konsol araç satırı (Temizle + Hata/Uyarı/Bilgi
/// rozetleri). Sayaçlar LogOutput taramasından gelir (öncelik: hata > uyarı >
/// bilgi; boş satırlar sayılmaz); Temizle çekirdek satıra döndürür.
/// OutputLogView bir UserControl olduğu için pencere platformu gerekmez.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorConsoleCountersTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorConsoleCountersTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlConsole_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("ConsoleTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
        // KÖK GERİ VERİLMEZ: bkz. EditorAssetGridVirtualizationTests.
    }

    public void Dispose()
    {
        try { _vm.Dispose(); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); }
        catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    [Fact]
    public void Counters_ClassifySeedAndAppendsThenClearResets()
    {
        // Açılış satırları ortama göre değişir (örn. ses aygıtı bildirimi);
        // bu yüzden mutlak değil delta kilitlenir.
        int e0 = _vm.LogErrorCount, w0 = _vm.LogWarningCount, i0 = _vm.LogInfoCount;

        _vm.AppendLog("proje kaydedildi");
        _vm.AppendLog("WARNING: bellek düşük");
        _vm.AppendLog("ERROR: dosya bulunamadı");
        _vm.AppendLog("kayıt hatası oluştu");
        _vm.AppendLog("işlem başarısız oldu");
        // Öncelik: hem hata hem uyarı geçiyorsa hata kazanır.
        _vm.AppendLog("error içeren warning satırı");

        Assert.Equal(e0 + 4, _vm.LogErrorCount);
        Assert.Equal(w0 + 1, _vm.LogWarningCount);
        Assert.Equal(i0 + 1, _vm.LogInfoCount);

        _vm.ClearLogCommand.Execute(null);
        Assert.Equal(MainWindowViewModel.LogSeedLine, _vm.LogOutput);
        Assert.Equal(0, _vm.LogErrorCount);
        Assert.Equal(0, _vm.LogWarningCount);
        Assert.Equal(1, _vm.LogInfoCount);
    }

    [Fact]
    public void Toolbar_HostsClearAndThreeBadges()
    {
        var view = new OutputLogView { DataContext = _vm };

        var clear = view.GetLogicalDescendants().OfType<Button>()
            .FirstOrDefault(b => (b.Content as string) == "Temizle")
            ?? throw new Exception("Console Temizle button not found");
        if (!ReferenceEquals(_vm.ClearLogCommand, clear.Command))
            throw new Exception("Temizle button must be wired to ClearLogCommand");

        // Rozetler kuruluşta bağlı metin taşır (zoom rozeti deyimiyle aynı);
        // açılış satırları ortama göre değiştiği için etiket + sayı denetlenir.
        var badges = view.GetLogicalDescendants().OfType<TextBlock>()
            .Where(t => (ToolTip.GetTip(t) as string)?.EndsWith("sayısı") == true)
            .ToList();
        string?[] labels = { "Hata: ", "Uyarı: ", "Bilgi: " };
        foreach (string label in labels)
        {
            var badge = badges.FirstOrDefault(b =>
                (ToolTip.GetTip(b) as string)?.StartsWith(label.TrimEnd(' ', ':')) == true)
                ?? throw new Exception($"Console badge missing for '{label}'");
            if (badge.Text is null || !badge.Text.StartsWith(label, StringComparison.Ordinal))
                throw new Exception($"Console badge not bound: '{badge.Text}'");
            if (!int.TryParse(badge.Text[label.Length..], out int n) || n < 0)
                throw new Exception($"Console badge count unreadable: '{badge.Text}'");
        }

        // Günlük gövdesi satır listesidir (Unity kromu H4; TextBox yok):
        // ilk satır çekirdek satır, probe satırı listede görünür.
        _vm.AppendLog("H4 probe satırı");
        var logList = view.GetLogicalDescendants().OfType<ListBox>().FirstOrDefault()
            ?? throw new Exception("Console log ListBox not found");
        var rows = (logList.ItemsSource as string[])?.ToList()
            ?? logList.Items.OfType<string>().ToList();
        if (rows.Count == 0 || !rows[0].StartsWith("[System]", StringComparison.Ordinal))
            throw new Exception("Console log ListBox lost its LogOutput binding");
        if (!rows.Any(r => r.EndsWith("H4 probe satırı", StringComparison.Ordinal)))
            throw new Exception("Console log probe satırı görünmüyor");
    }
}
