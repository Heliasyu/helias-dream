using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;

namespace HeliasDream.Core;

public enum CheckState { Ok, Note, Problem }

public sealed record CheckItem(string Label, string Detail, CheckState State);

/// <summary>What the Play page checks each time it opens.</summary>
public static class ReadyCheck
{
    public static List<CheckItem> Local(string? gameDir, HeliasToml cfg, OptionsFile opts)
    {
        var items = new List<CheckItem>();

        items.Add(gameDir != null
            ? new("Engine found", Path.GetFileName(gameDir.TrimEnd('/', '\\')), CheckState.Ok)
            : new("Engine not found", "Choose the Helias Dream folder in More", CheckState.Problem));

        var app0 = cfg.App0;
        bool dump = app0 != null && File.Exists(Path.Combine(app0, "eboot.bin"));
        var version = dump ? GameVersion(app0!) : null;
        items.Add(dump
            ? new("Game files found", $"CUSA03173{(version != null ? " · " + version : "")}",
                  version == null || version == "1.09" ? CheckState.Ok : CheckState.Note)
            : new("Game files missing", "Set the game folder in Setup", CheckState.Problem));

        bool eboot = cfg.Eboot != null && File.Exists(cfg.Eboot);
        items.Add(eboot ? new("Decrypted eboot", "eboot.elf", CheckState.Ok)
                        : new("Decrypted eboot missing", "Needed to start the game", CheckState.Problem));

        var upscaler = opts.Get("upscaler", "FSR 1");
        if (upscaler == "FSR 4")
            items.Add(Fsr4Assets.Present ? new("FSR 4 ready", "Shaders downloaded", CheckState.Ok)
                                         : new("FSR 4 not downloaded", "Runs as FSR 3.1 until downloaded", CheckState.Note));
        if (upscaler == "FSR 4.1.1")
        {
            bool built = Directory.Exists(Path.Combine(Paths.Fsr4Assets, "t2160_m0")) || Directory.Exists(Paths.Fsr4Assets);
            items.Add(built ? new("FSR 4.1.1 ready", "Assets built", CheckState.Ok)
                            : new("FSR 4.1.1 not built", "Runs as FSR 3.1 until built", CheckState.Note));
        }
        else items.Add(new("Upscaler", upscaler, CheckState.Ok));

        items.Add(DualSenseOnUsb()
            ? new("DualSense on USB", "HD haptics", CheckState.Ok)
            : new("No DualSense on USB", "Classic rumble", CheckState.Note));

        items.Add(opts.SignedIn
            ? new("Signed in", opts.AccountName ?? "The Hunter's Dream", CheckState.Ok)
            : new("Not signed in", "Online needs an account (F10 in game)", CheckState.Note));
        return items;
    }

    /// The server's round trip, or null when it does not answer within a few seconds.
    public static async Task<int?> PingServer(string host)
    {
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
            var sw = Stopwatch.StartNew();
            using var req = new HttpRequestMessage(HttpMethod.Head, $"https://{host}/");
            using var resp = await http.SendAsync(req);
            return (int)sw.ElapsedMilliseconds;
        }
        catch (Exception) { return null; }
    }

    /// APP_VER from the update's param.sfo (or the game's), "1.09".
    static string? GameVersion(string app0)
    {
        foreach (var dir in new[] { app0 + "-patch", app0 + "-UPDATE", app0 })
        {
            var sfo = Path.Combine(dir, "sce_sys", "param.sfo");
            if (!File.Exists(sfo)) continue;
            var v = SfoString(File.ReadAllBytes(sfo), "APP_VER");
            if (v != null) return v.Length > 2 && v[0] == '0' && v[1] != '.' ? v[1..] : v;
        }
        return null;
    }

    static string? SfoString(byte[] d, string key)
    {
        if (d.Length < 20 || d[0] != 0 || d[1] != (byte)'P') return null;
        int keys = BitConverter.ToInt32(d, 8), data = BitConverter.ToInt32(d, 12), n = BitConverter.ToInt32(d, 16);
        for (int i = 0; i < n; i++)
        {
            int e = 20 + i * 16;
            int ko = BitConverter.ToUInt16(d, e), len = BitConverter.ToInt32(d, e + 4), off = BitConverter.ToInt32(d, e + 12);
            int end = Array.IndexOf(d, (byte)0, keys + ko);
            if (end < 0) continue;
            if (Encoding.ASCII.GetString(d, keys + ko, end - keys - ko) == key)
                return Encoding.UTF8.GetString(d, data + off, len).TrimEnd('\0');
        }
        return null;
    }

    /// A DualSense (054c:0ce6) or DualSense Edge (054c:0df2) on the USB bus.
    public static bool DualSenseOnUsb()
    {
        if (!OperatingSystem.IsLinux()) return false;
        try
        {
            foreach (var dev in Directory.EnumerateDirectories("/sys/bus/usb/devices"))
            {
                var v = Path.Combine(dev, "idVendor");
                if (!File.Exists(v) || File.ReadAllText(v).Trim() != "054c") continue;
                var p = File.ReadAllText(Path.Combine(dev, "idProduct")).Trim();
                if (p is "0ce6" or "0df2") return true;
            }
        }
        catch (Exception) { }
        return false;
    }
}

/// <summary>KDE's HDR switch for a monitor (kscreen-doctor): true, false, or null when unknown.</summary>
public static class DesktopHdr
{
    public static bool? IsOn(string? connector)
    {
        if (!OperatingSystem.IsLinux()) return null;
        try
        {
            var psi = new ProcessStartInfo("kscreen-doctor", "-o") { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
            using var p = Process.Start(psi);
            if (p == null) return null;
            var text = System.Text.RegularExpressions.Regex.Replace(p.StandardOutput.ReadToEnd(), @"\x1b\[[0-9;]*m", "");
            p.WaitForExit(2000);
            string? current = null;
            bool? primaryHdr = null, found = null;
            bool currentPrimary = false;
            foreach (var raw in text.Split('\n'))
            {
                var line = raw.Trim();
                if (line.StartsWith("Output:"))
                {
                    var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    current = parts.Length > 2 ? parts[2] : null;
                    currentPrimary = false;
                }
                else if (line.StartsWith("priority 1")) currentPrimary = true;
                else if (line.StartsWith("HDR:"))
                {
                    var on = line.Contains("enabled");
                    if (current == connector) found = on;
                    if (currentPrimary) primaryHdr = on;
                }
            }
            return string.IsNullOrEmpty(connector) ? primaryHdr : found;
        }
        catch (Exception) { return null; }
    }
}

/// <summary>
/// Starts the game and tells when it ends. Its output (the engine's log) goes line by line to
/// `onLine` - the Console page - and to logs/HeliasDream-&lt;date&gt;.log in the game folder.
/// </summary>
public static class GameRunner
{
    public static string? LogFile { get; private set; }
    static Process? _running;

    public static Process? Start(string gameDir, string presentMode, string? monitor, string? profileConfig, string? gamepad,
                                 Action<string>? onLine, Action onExit)
    {
        var engine = Engine.Find(gameDir);
        if (engine == null) return null;
        var psi = new ProcessStartInfo(engine)
        {
            // No reader (the launcher closes as the game starts): the output goes where it always did.
            WorkingDirectory = gameDir, UseShellExecute = false, RedirectStandardOutput = onLine != null, RedirectStandardError = onLine != null,
            // The Windows engine is a console program: its log goes to the Console page, not a window of its own.
            CreateNoWindow = OperatingSystem.IsWindows() && onLine != null,
        };
        Engine.Prepare(psi, gameDir);
        if (profileConfig != null) { psi.ArgumentList.Add("--config"); psi.ArgumentList.Add(profileConfig); }
        // Native Wayland, the display's scale applied (as the old bbhost wrapper script set them).
        if (OperatingSystem.IsLinux() && !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WAYLAND_DISPLAY")) &&
            string.IsNullOrEmpty(Environment.GetEnvironmentVariable("SDL_VIDEO_DRIVER")))
        {
            psi.Environment["SDL_VIDEO_DRIVER"] = "wayland";
            psi.Environment["SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY"] = "1";
        }
        // The launcher has already shown everything the engine's own setup window would.
        psi.Environment["HELIAS_SETUP_WINDOW"] = "0";
        if (!string.IsNullOrEmpty(monitor)) psi.Environment["HELIAS_DISPLAY"] = monitor;
        if (presentMode is "mailbox" or "fifo" or "relaxed" or "immediate" or "latest")
            psi.Environment["HELIAS_PRESENT_MODE"] = presentMode;
        if (!string.IsNullOrEmpty(gamepad) && gamepad != "auto") psi.Environment["HELIAS_GAMEPAD"] = gamepad;
        StreamWriter? log = null;
        if (onLine != null) try
        {
            var dir = Path.Combine(gameDir, "logs");
            Directory.CreateDirectory(dir);
            LogFile = Path.Combine(dir, $"HeliasDream-{DateTime.Now:yyyyMMdd-HHmmss}.log");
            log = new StreamWriter(LogFile) { AutoFlush = true };
        }
        catch (Exception) { LogFile = null; }
        var gate = new object();
        void Line(string? l)
        {
            if (l == null) return;
            lock (gate) { try { log?.WriteLine(l); } catch (Exception) { } }
            onLine?.Invoke(l);
        }
        Process? p;
        try { p = Process.Start(psi); }
        catch (Exception e) { log?.Dispose(); onLine?.Invoke("Could not start the game: " + e.Message); return null; }
        if (p == null) { log?.Dispose(); return null; }
        p.OutputDataReceived += (_, e) => Line(e.Data);
        p.ErrorDataReceived += (_, e) => Line(e.Data);
        p.EnableRaisingEvents = true;
        p.Exited += (_, _) =>
        {
            try { p.WaitForExit(); } catch (Exception) { }  // the last lines of output first
            lock (gate) { log?.Dispose(); log = null; }
            _running = null;
            onExit();
        };
        if (onLine != null)
        {
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();
        }
        _running = p;
        return p;
    }

    /// Ends the game the launcher started (the Console page's Stop): asked to close first, as
    /// the window's close button does.
    public static void Stop()
    {
        var p = _running;
        if (p == null) return;
        try
        {
            if (OperatingSystem.IsLinux()) Process.Start("kill", ["-TERM", p.Id.ToString()])?.WaitForExit(2000);
            else p.Kill(true);
        }
        catch (Exception) { }
    }

    public static bool Running() =>
        Process.GetProcessesByName("HeliasDream.bin").Any() || Process.GetProcessesByName("HeliasDream-engine").Any() ||
        Process.GetProcessesByName("heliasdream").Any() ||
        Process.GetProcessesByName("bbhost.bin").Any() || Process.GetProcessesByName("bbhost").Any();
}
