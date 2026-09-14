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
        string pidFile = Path.Combine(Path.GetTempPath(), $"rowl_tool_pid_{Guid.NewGuid():N}.txt");
        File.WriteAllText(script,
            "import os, sys, time\n" +
            "open(sys.argv[1], 'w').write(str(os.getpid()))\n" +
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
            startInfo.ArgumentList.Add(pidFile);

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

            int childPid = await WaitForChildPidAsync(pidFile, TimeSpan.FromSeconds(5));
            DateTime childStart;
            try
            {
                using Process live = Process.GetProcessById(childPid);
                childStart = live.StartTime;
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
            {
                throw new Exception($"Test child process (PID {childPid}) vanished before cancellation; cannot verify teardown.");
            }

            cancellation.Cancel();
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => run);
            await AssertChildDeadAsync(childPid, childStart, TimeSpan.FromSeconds(10));
        }
        finally
        {
            try { File.Delete(script); } catch { }
            try { File.Delete(pidFile); } catch { }
        }
    }

    private static async Task<int> WaitForChildPidAsync(string pidFile, TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (File.Exists(pidFile) && int.TryParse(File.ReadAllText(pidFile).Trim(), out int pid))
                return pid;
            await Task.Delay(100);
        }
        throw new Exception("Test child process never reported its PID; cannot verify teardown.");
    }

    private static async Task AssertChildDeadAsync(int pid, DateTime startTime, TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            bool alive;
            try
            {
                using Process candidate = Process.GetProcessById(pid);
                alive = !candidate.HasExited && candidate.StartTime == startTime;
            }
            catch (ArgumentException)
            {
                return; // No such PID: the child is dead.
            }
            catch (InvalidOperationException)
            {
                return; // Exited mid-check: the child is dead.
            }
            if (!alive)
                return;
            await Task.Delay(100);
        }
        throw new Exception($"Cancelled external tool process (PID {pid}) is still alive {timeout.TotalSeconds:F0}s after cancellation; process tree teardown failed.");
    }
}
