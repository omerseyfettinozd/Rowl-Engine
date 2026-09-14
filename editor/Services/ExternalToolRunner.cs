using System;
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
    public static async Task<ExternalToolResult> RunAsync(
        ProcessStartInfo startInfo,
        Action<ExternalToolLine>? output = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(startInfo);
        cancellationToken.ThrowIfCancellationRequested();
        startInfo.UseShellExecute = false;
        startInfo.RedirectStandardOutput = true;
        startInfo.RedirectStandardError = true;
        startInfo.CreateNoWindow = true;

        using var process = new Process { StartInfo = startInfo };
        if (!process.Start())
            throw new InvalidOperationException($"Could not start external tool '{startInfo.FileName}'.");

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
            await process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            await Task.WhenAll(stdoutTask, stderrTask).ConfigureAwait(false);
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
    }
}
