using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

internal sealed class EditorInteractionBenchmark
{
    private readonly Dictionary<string, double> _metrics = new(StringComparer.Ordinal);

    public void Record(string name, double milliseconds)
    {
        if (milliseconds < 0 || double.IsNaN(milliseconds) || double.IsInfinity(milliseconds))
            throw new ArgumentOutOfRangeException(nameof(milliseconds));
        _metrics[name] = milliseconds;
    }

    public void Write(string path)
    {
        var report = new
        {
            schema_version = 1,
            fixture_id = "editor-headless-default-v1",
            build = new
            {
                type = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_BUILD_TYPE") ?? "Debug",
                id = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_BUILD_ID") ?? "local"
            },
            environment = new
            {
                os = RuntimeInformation.OSDescription,
                machine = Environment.MachineName,
                cpu_count = Environment.ProcessorCount
            },
            metrics = _metrics
        };

        string? directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        File.WriteAllText(path, JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
    }
}
