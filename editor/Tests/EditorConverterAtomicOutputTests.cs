using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C04 — dönüştürücü çıktısı atomik (part dosyası → doğrula → taşı) ve
/// içe aktarma sonucu başarısızlıkları kullanıcıya taşır.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorConverterAtomicOutputTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "RowlConvAtomic_" + Guid.NewGuid().ToString("N"));

    public EditorConverterAtomicOutputTests() => Directory.CreateDirectory(_root);

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch (Exception) { }
    }

    // Yarım çıktı yazıp hata kodu ile döner (disk dolu / öldürülme benzetimi).
    private static Task<ExternalToolResult> TruncatingFailingRunner(ProcessStartInfo startInfo, CancellationToken _)
    {
        string name = Path.GetFileName(startInfo.FileName);
        var argv = startInfo.ArgumentList.ToArray();
        if (name.Contains("ffmpeg", StringComparison.OrdinalIgnoreCase))
        {
            File.WriteAllBytes(argv[^1], new byte[] { 1, 2, 3, 4 });
            return Task.FromResult(new ExternalToolResult(0, string.Empty, string.Empty));
        }
        int o = Array.IndexOf(argv, "-o");
        int s = Array.IndexOf(argv, "--sidecar");
        File.WriteAllBytes(argv[o + 1], new byte[] { 9, 9 });
        File.WriteAllText(argv[s + 1], "{ truncated");
        return Task.FromResult(new ExternalToolResult(1, string.Empty, "disk full"));
    }

    private static Task<ExternalToolResult> MissingFfmpegRunner(ProcessStartInfo startInfo, CancellationToken _)
        => throw new Win32Exception(2, "No such file or directory");

    [Theory]
    [InlineData("x.webp", "x.png", MediaConverterService.WebpToolName, "images")]
    [InlineData("x.mp3", "x.ogg", MediaConverterService.OggToolName, "audio")]
    public async Task FailedConversion_KeepsExistingGoodTarget_AndLeavesNoPart(
        string srcName, string outName, string tool, string dir)
    {
        string src = Path.Combine(_root, srcName);
        File.WriteAllBytes(src, new byte[] { 5, 6, 7 });
        string outDir = Path.Combine(_root, "Assets", dir);
        Directory.CreateDirectory(outDir);
        string target = Path.Combine(outDir, outName);
        string sidecar = MediaConverterService.SidecarPathForOutput(target);
        byte[] good = { 0xAA, 0xBB, 0xCC };
        File.WriteAllBytes(target, good);
        File.WriteAllText(sidecar, "good-sidecar");

        var options = new MediaConverterService.ConverterOptions("oggenc", "webp2png", "ffmpeg", null, TruncatingFailingRunner);
        var result = await MediaConverterService.ConvertFileAsync(src, target, tool, options);

        Assert.Equal(MediaConverterService.ConversionOutcome.Failed, result.Outcome);
        Assert.Equal(good, File.ReadAllBytes(target));
        Assert.Equal("good-sidecar", File.ReadAllText(sidecar));
        Assert.DoesNotContain(Directory.GetFiles(outDir), f => f.Contains(".part", StringComparison.Ordinal));
        Assert.Equal(2, Directory.GetFiles(outDir).Length);
    }

    // Part dosyasını yazıp iptal gelene dek bekler (ortada iptal benzetimi).
    private static Func<ProcessStartInfo, CancellationToken, Task<ExternalToolResult>> BlockingRunner(
        SemaphoreSlim started)
        => async (startInfo, token) =>
        {
            string name = Path.GetFileName(startInfo.FileName);
            var argv = startInfo.ArgumentList.ToArray();
            if (name.Contains("ffmpeg", StringComparison.OrdinalIgnoreCase))
            {
                File.WriteAllBytes(argv[^1], new byte[] { 1, 2, 3, 4 });
                return new ExternalToolResult(0, string.Empty, string.Empty);
            }
            int o = Array.IndexOf(argv, "-o");
            int s = Array.IndexOf(argv, "--sidecar");
            File.WriteAllBytes(argv[o + 1], new byte[] { 9, 9 });
            File.WriteAllText(argv[s + 1], "{ half");
            started.Release();
            await Task.Delay(Timeout.Infinite, token); // iptal gelene dek
            return new ExternalToolResult(0, string.Empty, string.Empty);
        };

    [Theory]
    [InlineData("x.webp", "x.png", MediaConverterService.WebpToolName, "images")]
    [InlineData("x.mp3", "x.ogg", MediaConverterService.OggToolName, "audio")]
    public async Task CancelMidConversion_KeepsExistingGoodTarget_AndLeavesNoPart(
        string srcName, string outName, string tool, string dir)
    {
        string src = Path.Combine(_root, srcName);
        File.WriteAllBytes(src, new byte[] { 5, 6, 7 });
        string outDir = Path.Combine(_root, "Assets", dir);
        Directory.CreateDirectory(outDir);
        string target = Path.Combine(outDir, outName);
        string sidecar = MediaConverterService.SidecarPathForOutput(target);
        byte[] good = { 0xAA, 0xBB, 0xCC };
        File.WriteAllBytes(target, good);
        File.WriteAllText(sidecar, "good-sidecar");

        using var started = new SemaphoreSlim(0);
        using var cts = new CancellationTokenSource();
        var options = new MediaConverterService.ConverterOptions(
            "oggenc", "webp2png", "ffmpeg", null, BlockingRunner(started));
        var task = MediaConverterService.ConvertFileAsync(src, target, tool, options, null, cts.Token);
        Assert.True(await started.WaitAsync(TimeSpan.FromSeconds(10)), "araç hiç başlamadı");
        cts.Cancel();
        var result = await task.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(MediaConverterService.ConversionOutcome.Failed, result.Outcome);
        Assert.Contains("iptal", result.Message);
        Assert.Equal(good, File.ReadAllBytes(target));
        Assert.Equal("good-sidecar", File.ReadAllText(sidecar));
        Assert.DoesNotContain(Directory.GetFiles(outDir), f => f.Contains(".part", StringComparison.Ordinal));
        Assert.Equal(2, Directory.GetFiles(outDir).Length);
    }

    [Fact]
    public async Task CancelMidImport_NewFile_LeavesNoOutputNoPart_AndThrowsCancelled()
    {
        string src = Path.Combine(_root, "x.webp");
        File.WriteAllBytes(src, new byte[] { 5, 6, 7 });
        string assets = Path.Combine(_root, "Assets");
        using var started = new SemaphoreSlim(0);
        using var cts = new CancellationTokenSource();
        var options = new MediaConverterService.ConverterOptions(
            "oggenc", "webp2png", "ffmpeg", null, BlockingRunner(started));

        var task = EditorAssetImportService.ImportAssetFilesAsync(
            new[] { src }, assets, options, null, cts.Token, null, null, new List<ImportFailure>());
        Assert.True(await started.WaitAsync(TimeSpan.FromSeconds(10)));
        cts.Cancel();
        try { await task.WaitAsync(TimeSpan.FromSeconds(10)); } catch (OperationCanceledException) { }

        string[] left = Directory.Exists(assets)
            ? Directory.GetFiles(assets, "*", SearchOption.AllDirectories) : Array.Empty<string>();
        Assert.Empty(left);
    }

    [Fact]
    public async Task MissingFfmpeg_ImportReportsFailure_WithReason()
    {
        string src = Path.Combine(_root, "x.mp3");
        File.WriteAllBytes(src, new byte[] { 1, 2, 3 });
        string assets = Path.Combine(_root, "Assets");
        var failures = new List<ImportFailure>();

        var options = new MediaConverterService.ConverterOptions("oggenc", "webp2png", "ffmpeg", null, MissingFfmpegRunner);
        var imported = await EditorAssetImportService.ImportAssetFilesAsync(
            new[] { src }, assets, options, null, default, null, null, failures);

        Assert.Empty(imported);
        var failure = Assert.Single(failures);
        Assert.Equal("x.mp3", failure.FileName);
        Assert.Contains("ffmpeg", failure.Reason);
        Assert.Contains("bulunamad", failure.Reason);
    }

    [Fact]
    public async Task ViewModel_SummaryNamesFailedFile_NotJustImportedCount()
    {
        string good = Path.Combine(_root, "a.png");
        File.WriteAllBytes(good, new byte[] { 1 });
        string bad = Path.Combine(_root, "x.mp3");
        File.WriteAllBytes(bad, new byte[] { 1, 2, 3 });
        var vm = new AssetImportViewModel
        {
            ConverterOptions = new MediaConverterService.ConverterOptions("oggenc", "webp2png", "ffmpeg", null, MissingFfmpegRunner),
        };

        var result = await vm.RunAsync(new[] { good, bad }, Path.Combine(_root, "Assets"), null);

        Assert.Single(result);
        Assert.NotNull(vm.LastResultText);
        Assert.Contains("1", vm.LastResultText);
        Assert.Contains("x.mp3", vm.LastResultText);
        Assert.Contains("ffmpeg", vm.LastResultText);
        Assert.Single(vm.LastFailures);
    }

    [Fact]
    public void AssetBrowser_HidesPartFiles()
    {
        Assert.True(AssetBrowserViewModel.IsTransientFileName("a.ogg.1234.part"));
        Assert.True(AssetBrowserViewModel.IsTransientFileName("a.ogg.1234.part.rowlconv.json"));
        Assert.False(AssetBrowserViewModel.IsTransientFileName("a.ogg"));
        Assert.False(AssetBrowserViewModel.IsTransientFileName("a.ogg.rowlconv.json"));
    }
}
