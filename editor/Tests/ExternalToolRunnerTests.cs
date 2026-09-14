using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

public sealed class ExternalToolRunnerTests
{
    [Fact]
    public async Task Runner_StreamsStdoutAndStderrLive_AndCancelsProcessTree()
    {
        string script = Path.Combine(Path.GetTempPath(), $"rowl_tool_{Guid.NewGuid():N}.py");
        File.WriteAllText(script,
            "import sys, time\n" +
            "print('stdout-ready', flush=True)\n" +
            "print('stderr-ready', file=sys.stderr, flush=True)\n" +
            "time.sleep(30)\n");

        try
        {
            var stdoutSeen = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var stderrSeen = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            var startInfo = new ProcessStartInfo(OperatingSystem.IsWindows() ? "python" : "python3");
            startInfo.ArgumentList.Add(script);

            Task<ExternalToolResult> run = ExternalToolRunner.RunAsync(
                startInfo,
                line =>
                {
                    if (line.Stream == ExternalToolStream.StandardOutput && line.Text == "stdout-ready")
                        stdoutSeen.TrySetResult();
                    if (line.Stream == ExternalToolStream.StandardError && line.Text == "stderr-ready")
                        stderrSeen.TrySetResult();
                },
                cancellation.Token);

            await Task.WhenAll(stdoutSeen.Task, stderrSeen.Task).WaitAsync(TimeSpan.FromSeconds(5));
            if (run.IsCompleted)
                throw new Exception("External tool output was buffered until process exit.");

            cancellation.Cancel();
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => run);
        }
        finally
        {
            try { File.Delete(script); } catch { }
        }
    }
}
