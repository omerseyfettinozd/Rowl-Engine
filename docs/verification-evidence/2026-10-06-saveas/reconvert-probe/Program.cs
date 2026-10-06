using System.Diagnostics;
using RowlEngine.Editor.Services;

string repo = Path.GetFullPath(args[0]);
string tool = Path.Combine(repo, "build", "bin", OperatingSystem.IsWindows() ? "rowl_webp2png.exe" : "rowl_webp2png");
if (!File.Exists(tool)) throw new Exception("Build the real rowl_webp2png tool first: " + tool);
string parent = Path.Combine(Path.GetTempPath(), "RowlRealSaveAs_" + Guid.NewGuid().ToString("N"));
string source = Path.Combine(parent, "source");
string target = Path.Combine(parent, "copy");
try
{
    Directory.CreateDirectory(Path.Combine(source, "Assets", "json"));
    File.WriteAllText(Path.Combine(source, "Assets", "json", "full_story_graph.json"),
        "{\"start_node_id\":101,\"nodes\":[{\"id\":101}],\"connections\":[]}");
    string raw = Path.Combine(source, "SourceAssets", "章", "hero.webp");
    Directory.CreateDirectory(Path.GetDirectoryName(raw)!);
    var info = new ProcessStartInfo("ffmpeg") { UseShellExecute = false };
    foreach (string argument in new[] { "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=16x16",
                 "-frames:v", "1", "-c:v", "libwebp", "-lossless", "1", raw })
        info.ArgumentList.Add(argument);
    using (var process = Process.Start(info) ?? throw new Exception("ffmpeg could not start"))
    {
        await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30));
        if (process.ExitCode != 0) throw new Exception("ffmpeg fixture failed");
    }
    var options = new MediaConverterService.ConverterOptions(WebpToolPath: tool);
    var converted = await MediaConverterService.ImportConvertedSourceAssetsAsync(
        Path.Combine(source, "SourceAssets"), Path.Combine(source, "Assets"), options);
    if (converted.Count != 1 || converted[0].Outcome != MediaConverterService.ConversionOutcome.Converted)
        throw new Exception("Initial real conversion failed: " + string.Join("; ", converted.Select(r => r.Message)));
    string outputRelative = Path.Combine("Assets", "images", "章", "hero.png");
    byte[] outputBytes = File.ReadAllBytes(Path.Combine(source, outputRelative));
    byte[] sidecarBytes = File.ReadAllBytes(Path.Combine(source, outputRelative) + MediaConverterService.SidecarSuffix);

    var copy = ProjectSaveAsCoordinator.SaveProjectCopy(source, target, 1, 101, () => true);
    if (!copy.Succeeded) throw new Exception(copy.Message);
    if (!outputBytes.SequenceEqual(File.ReadAllBytes(Path.Combine(target, outputRelative))) ||
        !sidecarBytes.SequenceEqual(File.ReadAllBytes(Path.Combine(target, outputRelative) + MediaConverterService.SidecarSuffix)))
        throw new Exception("Converted content/provenance was not preserved byte-for-byte");
    Directory.Delete(source, true);
    File.Delete(Path.Combine(target, outputRelative));
    File.Delete(Path.Combine(target, outputRelative) + MediaConverterService.SidecarSuffix);
    var reconverted = await MediaConverterService.ImportConvertedSourceAssetsAsync(
        Path.Combine(target, "SourceAssets"), Path.Combine(target, "Assets"), options);
    if (reconverted.Count != 1 || reconverted[0].Outcome != MediaConverterService.ConversionOutcome.Converted)
        throw new Exception("Reconversion from copied source failed: " + string.Join("; ", reconverted.Select(r => r.Message)));
    if (!outputBytes.SequenceEqual(File.ReadAllBytes(Path.Combine(target, outputRelative))))
        throw new Exception("Real reconversion changed the PNG bytes");
    var provenance = MediaConversionProvenance.TryReadFile(Path.Combine(target, outputRelative) + MediaConverterService.SidecarSuffix);
    if (provenance?.SourcePath != "章/hero.webp") throw new Exception("Relative provenance source path lost");
    Console.WriteLine("REAL_WEBP_CONVERSION=Passed");
    Console.WriteLine("SAVEAS_MEDIA_AND_PROVENANCE_BYTES=Preserved");
    Console.WriteLine("ORIGINAL_PROJECT_REMOVED=True");
    Console.WriteLine("RECONVERT_FROM_COPIED_RAW_SOURCE=Passed");
    Console.WriteLine("RECONVERTED_PNG_BYTES=Identical");
    Console.WriteLine("RELATIVE_SOURCE_PATH=" + provenance.SourcePath);
}
finally
{
    if (Directory.Exists(parent)) Directory.Delete(parent, true);
}
