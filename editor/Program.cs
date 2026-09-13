using Avalonia;
using Avalonia.Headless;
using System;
using System.Linq;
using System.Threading.Tasks;

namespace RowlEngine.Editor
{
    internal class Program
    {
        [STAThread]
        public static int Main(string[] args)
        {
            TaskScheduler.UnobservedTaskException += (sender, e) =>
            {
                if (e.Exception?.InnerException is TaskCanceledException || e.Exception?.InnerExceptions?.Any(x => x is TaskCanceledException) == true)
                {
                    e.SetObserved();
                }
            };

            AppDomain.CurrentDomain.UnhandledException += (sender, e) =>
            {
                if (e.ExceptionObject is TaskCanceledException || (e.ExceptionObject is Exception ex && ex.InnerException is TaskCanceledException))
                {
                    return;
                }
            };

            try
            {
                if (args != null && args.Any(a => string.Equals(a, "--headless-test", StringComparison.OrdinalIgnoreCase)))
                    return HeadlessSmoke.Run(Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_JSON"));

                BuildAvaloniaApp().StartWithClassicDesktopLifetime(args ?? Array.Empty<string>());
                return 0;
            }
            catch (TaskCanceledException)
            {
                // Normal cancellation on shutdown
                return 0;
            }
        }

        public static AppBuilder BuildAvaloniaApp()
            => AppBuilder.Configure<App>()
                .UsePlatformDetect()
                .WithInterFont()
                .LogToTrace();

        /// <summary>
        /// Display-free platform for headless runs (CI/sandbox without X).
        /// The desktop builder above is untouched and remains the only path
        /// used for interactive sessions.
        /// </summary>
        public static AppBuilder BuildAvaloniaAppHeadless()
            => AppBuilder.Configure<App>()
                .UseHeadless(new AvaloniaHeadlessPlatformOptions())
                .WithInterFont()
                .LogToTrace();
    }
}
