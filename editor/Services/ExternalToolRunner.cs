using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Services;

public enum ExternalToolStream
{
    StandardOutput,
    StandardError
}

public sealed record ExternalToolLine(ExternalToolStream Stream, string Text);

public sealed record ExternalToolResult(int ExitCode, string StandardOutput, string StandardError);

/// <summary>
/// Runs command-line packaging tools without blocking on either redirected
/// stream. Output is delivered live and cancellation terminates the process
/// tree before the method completes.
/// </summary>
public static class ExternalToolRunner
{
    /// <summary>Upper bound for post-kill settle and stream drain waits.</summary>
    private static readonly TimeSpan KillSettleTimeout = TimeSpan.FromMilliseconds(3000);

    public static async Task<ExternalToolResult> RunAsync(
        ProcessStartInfo startInfo,
        Action<ExternalToolLine>? output = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(startInfo);
        cancellationToken.ThrowIfCancellationRequested();
        // Clone instead of mutating the caller's instance: the runner requires
        // redirected streams, and applying that to shared state is surprising.
        ProcessStartInfo launchInfo = CloneStartInfo(startInfo);
        launchInfo.UseShellExecute = false;
        launchInfo.RedirectStandardOutput = true;
        launchInfo.RedirectStandardError = true;
        launchInfo.CreateNoWindow = true;

        using var process = new Process { StartInfo = launchInfo };
        if (!process.Start())
            throw new InvalidOperationException($"Could not start external tool '{launchInfo.FileName}'.");

        var stdout = new StringBuilder();
        var stderr = new StringBuilder();
        Task stdoutTask = PumpAsync(
            process.StandardOutput,
            ExternalToolStream.StandardOutput,
            stdout,
            output);
        Task stderrTask = PumpAsync(
            process.StandardError,
            ExternalToolStream.StandardError,
            stderr,
            output);

        try
        {
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            TryKillProcessTree(process);
            using var settleTimeout = new CancellationTokenSource(KillSettleTimeout);
            try
            {
                await process.WaitForExitAsync(settleTimeout.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // The tree did not settle in time; fall through to a bounded
                // drain so a stuck child cannot block the pipeline forever.
            }
            await DrainWithTimeoutAsync(stdoutTask, stderrTask).ConfigureAwait(false);
            throw;
        }

        await Task.WhenAll(stdoutTask, stderrTask).ConfigureAwait(false);
        return new ExternalToolResult(process.ExitCode, stdout.ToString(), stderr.ToString());
    }

    private static async Task PumpAsync(
        System.IO.StreamReader reader,
        ExternalToolStream stream,
        StringBuilder capture,
        Action<ExternalToolLine>? output)
    {
        while (await reader.ReadLineAsync().ConfigureAwait(false) is { } line)
        {
            capture.AppendLine(line);
            output?.Invoke(new ExternalToolLine(stream, line));
        }
    }

    private static void TryKillProcessTree(Process process)
    {
        try
        {
            if (!process.HasExited)
                process.Kill(entireProcessTree: true);
        }
        catch (InvalidOperationException)
        {
            // The process exited between HasExited and Kill.
        }
        catch (System.ComponentModel.Win32Exception)
        {
            // Access denied or otherwise unkillable; the bounded settle wait
            // still prevents an unbounded block below.
        }
        catch (Exception)
        {
            // Best effort: never let a kill failure mask the caller's cancellation.
        }
    }

    private static ProcessStartInfo CloneStartInfo(ProcessStartInfo source)
    {
        var clone = new ProcessStartInfo
        {
            FileName = source.FileName,
            Arguments = source.Arguments,
            WorkingDirectory = source.WorkingDirectory,
            UseShellExecute = source.UseShellExecute,
            RedirectStandardOutput = source.RedirectStandardOutput,
            RedirectStandardError = source.RedirectStandardError,
            RedirectStandardInput = source.RedirectStandardInput,
            CreateNoWindow = source.CreateNoWindow,
            StandardOutputEncoding = source.StandardOutputEncoding,
            StandardErrorEncoding = source.StandardErrorEncoding,
            StandardInputEncoding = source.StandardInputEncoding,
            WindowStyle = source.WindowStyle,
            ErrorDialog = source.ErrorDialog,
            Verb = source.Verb,
        };
        foreach (string argument in source.ArgumentList)
            clone.ArgumentList.Add(argument);
        foreach (KeyValuePair<string, string?> variable in source.Environment)
        {
            if (variable.Value is string value)
                clone.Environment[variable.Key] = value;
        }
        return clone;
    }

    private static async Task DrainWithTimeoutAsync(Task stdoutTask, Task stderrTask)
    {
        Task drain = Task.WhenAll(stdoutTask, stderrTask);
        Task completed = await Task.WhenAny(drain, Task.Delay(KillSettleTimeout)).ConfigureAwait(false);
        if (ReferenceEquals(completed, drain))
        {
            await drain.ConfigureAwait(false);
            return;
        }
        // Leave the pumps to finish in the background once the pipes close;
        // awaiting them here would reintroduce the unbounded block.
        _ = drain.ContinueWith(static task => _ = task.Exception, TaskScheduler.Default);
    }
}
