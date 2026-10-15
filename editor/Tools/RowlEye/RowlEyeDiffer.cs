using System;
using System.Collections.Generic;
using System.IO;
using SkiaSharp;

namespace RowlEngine.Editor.Tools.RowlEye;

/// <summary>
/// Result of a visual comparison between reference and actual snapshots.
/// </summary>
public sealed record RowlEyeDiffResult(
    bool Passed,
    double MismatchPercentage,
    long MismatchedPixels,
    long TotalPixels,
    string DiffImagePath,
    string Message
);

/// <summary>
/// Pixel-accurate visual diff engine producing heatmap overlays.
/// </summary>
public static class RowlEyeDiffer
{
    public static RowlEyeDiffResult Compare(
        string referencePath,
        string actualPath,
        string? diffOutputPath = null,
        byte tolerance = 8,
        double failThresholdPercent = 0.5,
        IEnumerable<Goldens.RowlEyeIgnoreRegion>? ignoreRegions = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(referencePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(actualPath);

        if (!File.Exists(referencePath))
            return new RowlEyeDiffResult(false, 100.0, 0, 0, string.Empty, $"Reference image not found: '{referencePath}'");

        if (!File.Exists(actualPath))
            return new RowlEyeDiffResult(false, 100.0, 0, 0, string.Empty, $"Actual image not found: '{actualPath}'");

        using var refBitmap = SKBitmap.Decode(referencePath);
        using var actBitmap = SKBitmap.Decode(actualPath);

        if (refBitmap == null || actBitmap == null)
            return new RowlEyeDiffResult(false, 100.0, 0, 0, string.Empty, "Failed to decode one of the snapshot images.");

        int width = actBitmap.Width;
        int height = actBitmap.Height;

        if (refBitmap.Width != width || refBitmap.Height != height)
        {
            return new RowlEyeDiffResult(
                false,
                100.0,
                width * height,
                width * height,
                string.Empty,
                $"Dimension mismatch: Reference is {refBitmap.Width}x{refBitmap.Height}, Actual is {width}x{height}."
            );
        }

        using var diffBitmap = new SKBitmap(width, height, SKColorType.Rgba8888, SKAlphaType.Premul);
        long mismatchedPixels = 0;
        long totalPixels = (long)width * height;
        var maskList = ignoreRegions != null ? new List<Goldens.RowlEyeIgnoreRegion>(ignoreRegions) : null;

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                // Check if (x, y) falls inside an ignore region
                if (maskList != null)
                {
                    bool isIgnored = false;
                    for (int m = 0; m < maskList.Count; m++)
                    {
                        if (maskList[m].Contains(x, y))
                        {
                            isIgnored = true;
                            break;
                        }
                    }

                    if (isIgnored)
                    {
                        // Paint ignored region as soft cyan mask without counting mismatch
                        diffBitmap.SetPixel(x, y, new SKColor(0, 150, 200, 180));
                        continue;
                    }
                }

                var refPixel = refBitmap.GetPixel(x, y);
                var actPixel = actBitmap.GetPixel(x, y);

                int dr = Math.Abs(refPixel.Red - actPixel.Red);
                int dg = Math.Abs(refPixel.Green - actPixel.Green);
                int db = Math.Abs(refPixel.Blue - actPixel.Blue);
                int da = Math.Abs(refPixel.Alpha - actPixel.Alpha);

                int maxDiff = Math.Max(dr, Math.Max(dg, db));

                if (maxDiff <= tolerance && da <= tolerance)
                {
                    // Match: Dimmed grayscale of actual pixel for context
                    byte gray = (byte)((actPixel.Red * 0.299 + actPixel.Green * 0.587 + actPixel.Blue * 0.114) * 0.35);
                    diffBitmap.SetPixel(x, y, new SKColor(gray, gray, gray, 255));
                }
                else
                {
                    // Mismatch: Intensity lerp between Magenta (#FF00FF) and Red (#FF0000)
                    mismatchedPixels++;
                    double intensity = Math.Min(1.0, Math.Sqrt(dr * dr + dg * dg + db * db) / 441.6);
                    byte r = 255;
                    byte g = 0;
                    byte b = (byte)(255 * (1.0 - intensity));
                    diffBitmap.SetPixel(x, y, new SKColor(r, g, b, 255));
                }
            }
        }

        double mismatchPercent = totalPixels > 0 ? (mismatchedPixels * 100.0 / totalPixels) : 0.0;
        bool passed = mismatchPercent <= failThresholdPercent;

        if (!string.IsNullOrWhiteSpace(diffOutputPath))
        {
            string? dir = Path.GetDirectoryName(diffOutputPath);
            if (!string.IsNullOrEmpty(dir))
                Directory.CreateDirectory(dir);

            using var image = SKImage.FromBitmap(diffBitmap);
            using var data = image.Encode(SKEncodedImageFormat.Png, 100);
            using var stream = File.Create(diffOutputPath);
            data.SaveTo(stream);
            stream.Flush();
        }

        string msg = passed
            ? $"PASS: Mismatch is {mismatchPercent:F3}% ({mismatchedPixels}/{totalPixels} px), within threshold of {failThresholdPercent}%"
            : $"FAIL: Mismatch is {mismatchPercent:F3}% ({mismatchedPixels}/{totalPixels} px), exceeds threshold of {failThresholdPercent}%";

        return new RowlEyeDiffResult(passed, mismatchPercent, mismatchedPixels, totalPixels, diffOutputPath, msg);
    }
}
