using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace HeliasDream.Core;

/// <summary>
/// The game's save and its backups. The engine keeps the save in &lt;data&gt;/saves/SPRJ0005 and
/// copies it to &lt;data&gt;/save-backups/SPRJ0005/&lt;yyyyMMdd-HHmmss&gt;/ as the game writes it
/// (src/hle/system.cpp, back_up_save: at most every 5 minutes, the newest 8 kept). This lists
/// those, makes one on request, and puts one back - after backing up the save it replaces.
/// </summary>
public sealed class Saves
{
    const string Dir = "SPRJ0005";
    readonly string _data;

    public Saves(string dataDir) { _data = dataDir; }

    public string SaveDir => Path.Combine(_data, "saves", Dir);
    public string BackupRoot => Path.Combine(_data, "save-backups", Dir);
    public bool HasSave => Directory.Exists(SaveDir) && Directory.EnumerateFileSystemEntries(SaveDir).Any();

    public sealed record Backup(string Path, DateTime When, long Bytes);

    public List<Backup> List()
    {
        var list = new List<Backup>();
        if (!Directory.Exists(BackupRoot)) return list;
        foreach (var d in Directory.EnumerateDirectories(BackupRoot))
        {
            var name = System.IO.Path.GetFileName(d);
            if (!DateTime.TryParseExact(name, "yyyyMMdd-HHmmss", CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out var when)) continue;
            long bytes = 0;
            try { bytes = new DirectoryInfo(d).EnumerateFiles("*", SearchOption.AllDirectories).Sum(f => f.Length); } catch (Exception) { }
            list.Add(new Backup(d, when, bytes));
        }
        return list.OrderByDescending(b => b.When).ToList();
    }

    /// A backup of the save as it is now. Returns its folder, or null (no save, or it failed).
    public string? BackUpNow()
    {
        if (!HasSave) return null;
        var dest = Path.Combine(BackupRoot, DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture));
        if (Directory.Exists(dest)) return dest;
        try
        {
            Copy(SaveDir, dest);
            return dest;
        }
        catch (Exception) { return null; }
    }

    /// Puts a backup back as the save. The save it replaces is backed up first. The game
    /// must not be running.
    public bool Restore(Backup b, out string message)
    {
        if (GameRunner.Running()) { message = "Quit the game first."; return false; }
        if (!Directory.Exists(b.Path)) { message = "That backup is gone."; return false; }
        var safety = HasSave ? BackUpNow() : null;
        if (HasSave && safety == null) { message = "Could not back up the current save first; nothing changed."; return false; }
        try
        {
            var staging = SaveDir + ".restoring";
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
            Copy(b.Path, staging);
            if (Directory.Exists(SaveDir)) Directory.Delete(SaveDir, true);
            Directory.Move(staging, SaveDir);
            message = $"Restored the save from {b.When:MMM d, HH:mm}." + (safety != null ? " The one it replaced is backed up." : "");
            return true;
        }
        catch (Exception e)
        {
            message = "Restore failed: " + e.Message;
            return false;
        }
    }

    /// The save in a folder the player picked (a PS4 save from Apollo Save Tool or a backup, a
    /// shadPS4 or Lantern Engine save folder, another PC's): the folder that holds userdata0000,
    /// searched a few levels down. Null when there is none.
    public static string? FindSave(string folder)
    {
        if (!Directory.Exists(folder)) return null;
        if (File.Exists(Path.Combine(folder, "userdata0000"))) return folder;
        try
        {
            var found = Directory.EnumerateFiles(folder, "userdata0000", new EnumerationOptions { RecurseSubdirectories = true, MaxRecursionDepth = 6, IgnoreInaccessible = true })
                .Select(Path.GetDirectoryName).Where(d => d != null).Cast<string>().ToList();
            return found.FirstOrDefault(d => Path.GetFileName(d) == Dir) ?? found.FirstOrDefault();
        }
        catch (Exception) { return null; }
    }

    /// Makes a save found by FindSave this game's: its save files (userdata*, backup*,
    /// sce_param.bin, icon0.png, sce_sys) replace the current ones, which are backed up first.
    public bool Import(string from, out string message)
    {
        if (GameRunner.Running()) { message = "Quit the game first."; return false; }
        var src = FindSave(from);
        if (src == null) { message = "No Bloodborne save there (a folder with userdata0000 in it)."; return false; }
        var safety = HasSave ? BackUpNow() : null;
        if (HasSave && safety == null) { message = "Could not back up the current save first; nothing changed."; return false; }
        try
        {
            Directory.CreateDirectory(SaveDir);
            foreach (var f in Directory.EnumerateFiles(SaveDir))
            {
                var n = Path.GetFileName(f);
                if (n.StartsWith("userdata") || n.StartsWith("backup")) File.Delete(f);
            }
            int files = 0;
            foreach (var f in Directory.EnumerateFiles(src))
            {
                var n = Path.GetFileName(f);
                if (n.StartsWith("userdata") || n.StartsWith("backup") || n is "sce_param.bin" or "icon0.png")
                {
                    File.Copy(f, Path.Combine(SaveDir, n), true);
                    files++;
                }
            }
            var sys = Path.Combine(src, "sce_sys");
            if (Directory.Exists(sys)) Copy(sys, Path.Combine(SaveDir, "sce_sys"));
            message = $"Imported {files} save files from {src}." + (safety != null ? " The save it replaced is backed up." : "");
            return true;
        }
        catch (Exception e)
        {
            message = "Import failed: " + e.Message;
            return false;
        }
    }

    static void Copy(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var f in Directory.EnumerateFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
        foreach (var d in Directory.EnumerateDirectories(from)) Copy(d, Path.Combine(to, Path.GetFileName(d)));
    }
}

/// <summary>What this PC is, for the recommended settings.</summary>
public sealed record HardwareInfo(string Gpu, uint Vendor, uint Device, long VramBytes, int Cores, int ScreenW, int ScreenH)
{
    public static HardwareInfo Detect(int screenW, int screenH)
    {
        uint vendor = 0, device = 0;
        long vram = 0;
        string name = "Unknown GPU";
        if (OperatingSystem.IsLinux())
        {
            try
            {
                foreach (var card in Directory.EnumerateDirectories("/sys/class/drm", "card*"))
                {
                    var dev = Path.Combine(card, "device");
                    if (!File.Exists(Path.Combine(dev, "vendor")) || Path.GetFileName(card).Contains('-')) continue;
                    vendor = Convert.ToUInt32(File.ReadAllText(Path.Combine(dev, "vendor")).Trim(), 16);
                    device = Convert.ToUInt32(File.ReadAllText(Path.Combine(dev, "device")).Trim(), 16);
                    var v = Path.Combine(dev, "mem_info_vram_total");
                    if (File.Exists(v)) long.TryParse(File.ReadAllText(v).Trim(), out vram);
                    if (vendor != 0) break;
                }
            }
            catch (Exception) { }
            name = vendor switch
            {
                0x1002 => device is >= 0x7550 and <= 0x75ff ? "AMD Radeon RX 9000 series (RDNA 4)"
                          : device == 0x163f || device == 0x1435 ? "Steam Deck (AMD Van Gogh)" : "AMD Radeon",
                0x10de => "NVIDIA GeForce",
                0x8086 => "Intel graphics",
                _ => name,
            };
        }
        return new HardwareInfo(name, vendor, device, vram, Environment.ProcessorCount, screenW, screenH);
    }

    public bool Rdna4 => Vendor == 0x1002 && Device is >= 0x7550 and <= 0x75ff;
    public bool SteamDeck => Vendor == 0x1002 && (Device == 0x163f || Device == 0x1435);

    public string Summary =>
        $"{Gpu}{(VramBytes > 0 ? $" · {VramBytes / (1L << 30)} GB" : "")} · {Cores} threads · {ScreenW}×{ScreenH} screen";

    /// The settings this PC suits: (key, value) pairs of HeliasDream-options.toml, and why.
    public (List<(string Key, string Value)> Settings, string Why) Recommend()
    {
        var s = new List<(string, string)>();
        string res = ScreenW >= 3840 ? "3840x2160" : ScreenW >= 2560 ? "2560x1440" : ScreenW >= 1920 ? "1920x1080" : "1280x720";
        if (SteamDeck) res = "1280x800";
        s.Add(("resolution", res));
        s.Add(("window_mode", "Fullscreen"));
        string why;
        if (Rdna4)
        {
            s.Add(("upscaler", "FSR 4.1.1"));
            s.Add(("upscale_quality", ScreenW >= 3840 ? "Performance" : ScreenW >= 2560 ? "Balanced" : "Quality"));
            s.Add(("frame_cap", "120"));
            why = "RDNA 4 runs FSR 4.1.1 at full speed: the sharpest upscaler, at 120 fps.";
        }
        else if (SteamDeck)
        {
            s.Add(("upscaler", "FSR 1"));
            s.Add(("upscale_quality", "Native AA"));
            s.Add(("frame_cap", "30"));
            s.Add(("ssao", "Off"));
            s.Add(("model_detail", "Low"));
            why = "The Steam Deck holds the game's own 30 fps at its screen's size, with the heavier effects eased.";
        }
        else
        {
            bool strong = VramBytes >= 8L << 30 || Vendor == 0x10de;
            s.Add(("upscaler", "FSR 3.1"));
            s.Add(("upscale_quality", ScreenW >= 3840 ? "Performance" : ScreenW >= 2560 ? "Quality" : "Native AA"));
            s.Add(("frame_cap", strong ? "60" : "30"));
            why = strong ? "FSR 3.1 runs on any GPU; 60 fps with the upscaler's help." : "FSR 3.1 with the game's own 30 fps, the safe choice for this GPU.";
        }
        s.Add(("vsync", "On"));
        return (s, why);
    }
}
