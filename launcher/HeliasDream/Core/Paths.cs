using System;
using System.Collections.Generic;
using System.IO;

namespace HeliasDream.Core;

/// <summary>
/// Where things are: the engine's per-user files (the same ones the engine reads), the game folder
/// with the engine in it, and this launcher's own settings.
/// </summary>
public static class Paths
{
    public static bool IsWindows => OperatingSystem.IsWindows();

    /// ~/.config/HeliasDream (Linux), %APPDATA%\HeliasDream (Windows): HeliasDream.toml,
    /// HeliasDream-options.toml. Copied once from the engine's old bbhost folders (Migrate).
    public static string HeliasConfigDir =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "HeliasDream")
            : Path.Combine(XdgConfigHome, "HeliasDream");

    /// ~/.local/share/HeliasDream (Linux), %LOCALAPPDATA%\HeliasDream (Windows): data, FSR 4 assets.
    public static string HeliasDataDir =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HeliasDream")
            : Path.Combine(XdgDataHome, "HeliasDream");

    public static string OptionsFile => Path.Combine(HeliasConfigDir, "HeliasDream-options.toml");
    public static string HeliasToml => Path.Combine(HeliasConfigDir, "HeliasDream.toml");
    public static string Fsr4Assets => Path.Combine(HeliasDataDir, "fsr4_411");

    /// This launcher's own settings (the game folder, motion).
    public static string LauncherSettingsFile =>
        IsWindows
            ? Path.Combine(HeliasConfigDir, "launcher.json")
            : Path.Combine(HeliasConfigDir, "launcher.json");

    static string Home => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
    static string XdgConfigHome => Env("XDG_CONFIG_HOME") ?? Path.Combine(Home, ".config");
    static string XdgDataHome => Env("XDG_DATA_HOME") ?? Path.Combine(Home, ".local", "share");
    static string? Env(string name) => Environment.GetEnvironmentVariable(name) is { Length: > 0 } v ? v : null;

    /// The engine in a folder: HeliasDream.bin (Linux), HeliasDream-engine.exe (Windows), the
    /// build's own heliasdream / heliasdream.exe, or an older package's bbhost / bbhost.bin / bbhost.exe.
    public static string? EngineIn(string? dir)
    {
        if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return null;
        foreach (var name in IsWindows ? new[] { "HeliasDream-engine.exe", "heliasdream.exe", "bbhost.exe" } : new[] { "HeliasDream.bin", "heliasdream", "bbhost", "bbhost.bin" })
        {
            var p = Path.Combine(dir, name);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    /// The game folder: the saved one, else beside this launcher, else the usual places. A
    /// launcher that carries the engine (Engine.Embedded) is its own install: its folder.
    public static string? FindGameDir(string? saved)
    {
        if (Engine.Embedded && Env("HELIAS_GAME_DIR") == null)
            return Path.GetFullPath(AppContext.BaseDirectory);
        var tries = new List<string?>
        {
            Env("HELIAS_GAME_DIR"),
            saved,
            AppContext.BaseDirectory,
            Path.GetDirectoryName(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)),
            Path.Combine(Home, "BloodBorne PC", "HeliasDream"),
            Path.Combine(Home, "BloodBorne PC", "bbhost-linux-v0.2.18"),
        };
        foreach (var d in tries)
            if (EngineIn(d) != null) return Path.GetFullPath(d!);
        return null;
    }

    /// Once: the engine's old bbhost folders copied to HeliasDream ones (the config files
    /// renamed, as the engine does it: src/core/config.cpp), and the launcher's own settings
    /// from ~/.config/helias-dream. The old folders are kept.
    public static void Migrate()
    {
        try
        {
            var oldCfg = IsWindows ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "bbhost") : Path.Combine(XdgConfigHome, "bbhost");
            if (Directory.Exists(oldCfg) && !Directory.Exists(HeliasConfigDir))
            {
                CopyDir(oldCfg, HeliasConfigDir);
                foreach (var f in Directory.GetFiles(HeliasConfigDir))
                {
                    var n = Path.GetFileName(f);
                    if (n == "bbhost.toml") File.Move(f, Path.Combine(HeliasConfigDir, "HeliasDream.toml"));
                    else if (n.StartsWith("bbhost-options")) File.Move(f, Path.Combine(HeliasConfigDir, "HeliasDream" + n[6..]));
                }
            }
            var oldData = IsWindows ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "bbhost") : Path.Combine(XdgDataHome, "bbhost");
            if (Directory.Exists(oldData) && !Directory.Exists(HeliasDataDir)) CopyDir(oldData, HeliasDataDir);
            var oldLauncher = IsWindows ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "helias-dream", "launcher.json")
                                        : Path.Combine(XdgConfigHome, "helias-dream", "launcher.json");
            if (File.Exists(oldLauncher) && !File.Exists(LauncherSettingsFile))
            {
                Directory.CreateDirectory(HeliasConfigDir);
                File.Copy(oldLauncher, LauncherSettingsFile);
            }
        }
        catch (Exception) { }
    }

    static void CopyDir(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(from)) CopyDir(d, Path.Combine(to, Path.GetFileName(d)));
    }
}
