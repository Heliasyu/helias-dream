using System;
using System.Linq;
using System.Threading;
using Avalonia;
using Avalonia.Headless;
using Avalonia.Threading;

namespace HeliasDream;

/// <summary>
/// `HeliasDream --shot out.png [page]`: draws the window off-screen and saves it, for checking a
/// change to the look without opening anything on the desktop.
/// </summary>
internal static class Shot
{
    public static void Run(string[] args)
    {
        var path = args.Length > 1 ? args[1] : "shot.png";
        int page = args.Length > 2 && int.TryParse(args[2], out var p) ? p : 0;
        AppBuilder.Configure<App>().UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
            .SetupWithoutStarting();
        var w = new MainWindow();
        w.Show();
        Pump(20);
        if (page != 0) { w.ShowPage(page); Pump(120); }
        if (args.Length > 3 && double.TryParse(args[3], out var scroll))
        {
            foreach (var sv in Avalonia.VisualTree.VisualExtensions.GetVisualDescendants(w).OfType<Avalonia.Controls.ScrollViewer>())
                sv.Offset = new Avalonia.Vector(0, scroll);
            Pump(60);
        }
        Pump(40);
        w.CaptureRenderedFrame()?.Save(path);
        w.Close();
    }

    static void Pump(int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            Dispatcher.UIThread.RunJobs();
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            Thread.Sleep(16);
        }
        Dispatcher.UIThread.RunJobs();
    }
}
