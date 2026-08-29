using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor
{
    public partial class App : Application
    {
        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                string[]? args = desktop.Args;
                if (args != null && args.Length > 0 && Directory.Exists(args[0]))
                {
                    desktop.MainWindow = new MainWindow(args[0]);
                }
                else
                {
                    var hubVm = new ProjectHubViewModel();
                    var hubWin = new ProjectHubWindow(hubVm);

                    hubVm.ProjectOpened += (projectPath) =>
                    {
                        var mainWin = new MainWindow(projectPath);
                        desktop.MainWindow = mainWin;
                        mainWin.Show();
                        hubWin.Close();
                    };

                    desktop.MainWindow = hubWin;
                }
            }

            base.OnFrameworkInitializationCompleted();
        }
    }
}
