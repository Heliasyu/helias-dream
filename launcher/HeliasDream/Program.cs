using Avalonia;

namespace HeliasDream;

internal static class Program
{
    [System.STAThread]
    public static void Main(string[] args)
    {
        // The engine's old bbhost folders and the launcher's old settings, copied over once.
        HeliasDream.Core.Paths.Migrate();
        if (args.Length > 0 && args[0] == "--shot") { Shot.Run(args); return; }
        if (args.Length == 3 && args[0] == "--steam-check") { HeliasDream.Core.Steam.RoundTrip(args[1], args[2]); return; }
        // --add-to-steam [art folder]: what Game setup's Add does, from a terminal.
        if (args.Length >= 1 && args[0] == "--add-to-steam")
        {
            var exe = System.Environment.ProcessPath ?? "";
            var (ok, msg) = HeliasDream.Core.Steam.Add(exe, System.IO.Path.GetDirectoryName(exe) ?? "", args.Length > 1 ? args[1] : null);
            System.Console.WriteLine(msg);
            System.Environment.Exit(ok ? 0 : 1);
        }
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
}
