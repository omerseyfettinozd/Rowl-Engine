using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

internal sealed class EditorInteractionBenchmark
{
    private readonly Dictionary<string, double> _metrics = new(StringComparer.Ordinal);
    private readonly string _fixtureId;

    public EditorInteractionBenchmark(string fixtureId = "editor-headless-default-v3")
    {
        _fixtureId = string.IsNullOrWhiteSpace(fixtureId)
            ? throw new ArgumentException("Fixture ID is required.", nameof(fixtureId))
            : fixtureId;
    }

    public void Record(string name, double value)
    {
        if (value < 0 || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(nameof(value));
        _metrics[name] = value;
    }

    public void Write(string path)
    {
        var report = new
        {
            schema_version = 2,
            fixture_id = _fixtureId,
            build = new
            {
                type = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_BUILD_TYPE") ?? "Debug",
                id = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_BUILD_ID") ?? "local"
            },
            environment = new
            {
                os = RuntimeInformation.OSDescription,
                architecture = NormalizeArchitecture(RuntimeInformation.ProcessArchitecture),
                cpu_model = ResolveCpuModel(),
                power_profile = ResolvePowerProfile(),
                machine = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_MACHINE") ?? Environment.MachineName,
                cpu_count = Environment.ProcessorCount
            },
            metrics = _metrics
        };

        string? directory = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
        File.WriteAllText(path, JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static string NormalizeArchitecture(Architecture architecture) => architecture switch
    {
        Architecture.X64 => "x86_64",
        Architecture.Arm64 => "arm64",
        Architecture.X86 => "x86",
        Architecture.Arm => "arm",
        _ => architecture.ToString().ToLowerInvariant()
    };

    /// <summary>
    /// C03: güç profili kıyaslama anahtarındadır (aynı CPU, profile göre katlarca
    /// farklı ölçer). Linux: cpufreq governor + energy_performance_preference.
    /// </summary>
    private static string ResolvePowerProfile()
    {
        string? configured = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_POWER_PROFILE");
        if (!string.IsNullOrWhiteSpace(configured)) return configured.Trim();
        if (!OperatingSystem.IsLinux()) return "unknown";
        static string FirstLine(string path)
        {
            try { return File.Exists(path) ? (File.ReadLines(path).FirstOrDefault() ?? string.Empty).Trim() : string.Empty; }
            catch (IOException) { return string.Empty; }
            catch (UnauthorizedAccessException) { return string.Empty; }
        }
        string governor = FirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        string preference = FirstLine("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference");
        if (governor.Length == 0) return "unknown";
        return preference.Length == 0 ? governor : governor + "/" + preference;
    }

    private static string ResolveCpuModel()
    {
        string? configured = Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_CPU_MODEL");
        if (!string.IsNullOrWhiteSpace(configured)) return configured.Trim();

        string? windowsModel = Environment.GetEnvironmentVariable("PROCESSOR_IDENTIFIER");
        if (!string.IsNullOrWhiteSpace(windowsModel)) return windowsModel.Trim();

        const string cpuInfoPath = "/proc/cpuinfo";
        if (File.Exists(cpuInfoPath))
        {
            foreach (string line in File.ReadLines(cpuInfoPath))
            {
                int separator = line.IndexOf(':');
                if (separator < 0) continue;
                string key = line[..separator].Trim();
                if (key is not ("model name" or "Hardware" or "Processor")) continue;
                string model = line[(separator + 1)..].Trim();
                if (model.Length > 0) return model;
            }
        }

        return "unknown";
    }
}
