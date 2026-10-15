using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace RowlEngine.Editor.Tools.RowlEye;

/// <summary>
/// Headless Skia renderer for Avalonia windows and controls.
/// </summary>
public static class RowlEyeRenderer
{
    public static void RenderWindow(Window window, double scale, string destinationPngPath)
    {
        ArgumentNullException.ThrowIfNull(window);
        ArgumentException.ThrowIfNullOrWhiteSpace(destinationPngPath);

        window.Measure(new Size(window.Width, window.Height));
        window.Arrange(new Rect(0, 0, window.Width, window.Height));
        Dispatcher.UIThread.RunJobs();

        int pixelW = Math.Max(1, (int)Math.Round(window.Width * scale));
        int pixelH = Math.Max(1, (int)Math.Round(window.Height * scale));
        var pixelSize = new PixelSize(pixelW, pixelH);
        var dpi = new Vector(96.0 * scale, 96.0 * scale);

        string? dir = Path.GetDirectoryName(destinationPngPath);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);

        if (Math.Abs(scale - 1.0) < 0.001)
        {
            var frame = Avalonia.Headless.HeadlessWindowExtensions.CaptureRenderedFrame(window);
            if (frame != null)
            {
                using var fs = File.Create(destinationPngPath);
                frame.Save(fs);
                fs.Flush();
                Console.WriteLine($"[Rowl.Eye.Renderer] 1x frame captured via HeadlessWindowExtensions ({new FileInfo(destinationPngPath).Length} bytes)");
                return;
            }
        }

        try
        {
            using var rtb = new RenderTargetBitmap(pixelSize, dpi);
            rtb.Render(window);
            using var fs = File.Create(destinationPngPath);
            rtb.Save(fs);
            fs.Flush();
            Console.WriteLine($"[Rowl.Eye.Renderer] Scaled {scale}x frame rendered via RenderTargetBitmap ({new FileInfo(destinationPngPath).Length} bytes, {pixelSize})");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[Rowl.Eye.Renderer] RenderTargetBitmap failed: {ex.Message}, falling back to frame capture");
            var frame = Avalonia.Headless.HeadlessWindowExtensions.CaptureRenderedFrame(window);
            if (frame != null)
            {
                using var fs = File.Create(destinationPngPath);
                frame.Save(fs);
                fs.Flush();
            }
        }

        Console.WriteLine($"[Rowl.Eye.Renderer] File created? {File.Exists(destinationPngPath)}, size: {(File.Exists(destinationPngPath) ? new FileInfo(destinationPngPath).Length : 0)} bytes");
    }

    public static byte[] RenderWindowToBytes(Window window, double scale)
    {
        ArgumentNullException.ThrowIfNull(window);

        window.Measure(new Size(window.Width, window.Height));
        window.Arrange(new Rect(0, 0, window.Width, window.Height));
        Dispatcher.UIThread.RunJobs();

        int pixelW = Math.Max(1, (int)Math.Round(window.Width * scale));
        int pixelH = Math.Max(1, (int)Math.Round(window.Height * scale));
        var pixelSize = new PixelSize(pixelW, pixelH);
        var dpi = new Vector(96.0 * scale, 96.0 * scale);

        using var ms = new MemoryStream();

        if (Math.Abs(scale - 1.0) < 0.001)
        {
            var frame = Avalonia.Headless.HeadlessWindowExtensions.CaptureRenderedFrame(window);
            if (frame != null)
            {
                frame.Save(ms);
                return ms.ToArray();
            }
        }

        try
        {
            using var rtb = new RenderTargetBitmap(pixelSize, dpi);
            rtb.Render(window);
            rtb.Save(ms);
            return ms.ToArray();
        }
        catch
        {
            var frame = Avalonia.Headless.HeadlessWindowExtensions.CaptureRenderedFrame(window);
            if (frame != null)
            {
                frame.Save(ms);
                return ms.ToArray();
            }
            throw;
        }
    }
}
