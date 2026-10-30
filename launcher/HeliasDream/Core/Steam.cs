using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace HeliasDream.Core;

/// <summary>
/// "Add to Steam": a non-Steam shortcut in shortcuts.vdf (Steam's binary key-values) for each
/// Steam account on this PC, with the player's own art in its grid folder (portrait, wide,
/// hero, logo, icon). Steam rewrites the file when it quits, so it must be closed; the file is
/// backed up first. An existing "Bloodborne" shortcut is updated rather than doubled.
/// </summary>
public static class Steam
{
    public const string AppName = "Bloodborne";

    // ---- Binary VDF: 0x00 a map, 0x01 a string, 0x02 an int32, 0x08 the map's end. ----
    public sealed class Node
    {
        public readonly List<(string Key, object Value)> Items = new();  // string, int or Node
        public object? this[string key]
        {
            get => Items.FirstOrDefault(i => i.Key == key).Value;
            set
            {
                int i = Items.FindIndex(x => x.Key == key);
                if (i >= 0) Items[i] = (key, value!);
                else Items.Add((key, value!));
            }
        }
    }

    static Node Read(BinaryReader r)
    {
        var n = new Node();
        while (true)
        {
            byte t = r.ReadByte();
            if (t == 0x08) return n;
            string key = CString(r);
            object v = t switch
            {
                0x00 => Read(r),
                0x01 => CString(r),
                0x02 => r.ReadInt32(),
                _ => throw new InvalidDataException($"vdf type {t}"),
            };
            n.Items.Add((key, v));
        }
    }

    static string CString(BinaryReader r)
    {
        var bytes = new List<byte>();
        for (byte b; (b = r.ReadByte()) != 0;) bytes.Add(b);
        return Encoding.UTF8.GetString(bytes.ToArray());
    }

    static void Write(BinaryWriter w, Node n)
    {
        foreach (var (k, v) in n.Items)
        {
            switch (v)
            {
                case Node c: w.Write((byte)0); W(w, k); Write(w, c); break;
                case string s: w.Write((byte)1); W(w, k); W(w, s); break;
                case int i: w.Write((byte)2); W(w, k); w.Write(i); break;
            }
        }
        w.Write((byte)8);
    }

    static void W(BinaryWriter w, string s) { w.Write(Encoding.UTF8.GetBytes(s)); w.Write((byte)0); }

    static uint Crc32(byte[] data)
    {
        uint c = 0xFFFFFFFFu;
        foreach (var b in data)
        {
            c ^= b;
            for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        }
        return ~c;
    }

    // ---- Where Steam is ----
    public static IEnumerable<string> UserConfigDirs()
    {
        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        var roots = OperatingSystem.IsWindows()
            ? new[] { @"C:\Program Files (x86)\Steam" }
            : new[] { Path.Combine(home, ".local/share/Steam"), Path.Combine(home, ".steam/steam"),
                      Path.Combine(home, ".var/app/com.valvesoftware.Steam/.local/share/Steam") };
        var seen = new HashSet<string>();
        foreach (var r0 in roots)
        {
            // ~/.steam/steam is usually a link to ~/.local/share/Steam: one install, once.
            var root = r0;
            try { root = new DirectoryInfo(r0).ResolveLinkTarget(true)?.FullName ?? r0; } catch (Exception) { }
            if (!seen.Add(Path.GetFullPath(root).TrimEnd('/'))) continue;
            var ud = Path.Combine(root, "userdata");
            if (!Directory.Exists(ud)) continue;
            foreach (var u in Directory.EnumerateDirectories(ud))
            {
                if (!uint.TryParse(Path.GetFileName(u), out var id) || id == 0) continue;
                var cfg = Path.Combine(u, "config");
                var real = Path.GetFullPath(cfg);
                try { real = new DirectoryInfo(cfg).ResolveLinkTarget(true)?.FullName ?? real; } catch (Exception) { }
                if (Directory.Exists(cfg) && seen.Add(real)) yield return cfg;
            }
        }
    }

    /// A check (HeliasDream --steam-check in out): reads a shortcuts file and writes it again
    /// unchanged, so the reader and writer can be compared with the original byte for byte.
    public static void RoundTrip(string from, string to)
    {
        using var r = new BinaryReader(File.OpenRead(from));
        r.ReadByte();
        CString(r);
        var list = Read(r);
        using var w = new BinaryWriter(File.Create(to));
        w.Write((byte)0);
        W(w, "shortcuts");
        Write(w, list);
        w.Write((byte)8);
    }

    public static bool Running() =>
        Process.GetProcessesByName("steam").Any() || Process.GetProcessesByName("steamwebhelper").Any();

    /// Adds (or updates) the shortcut: `exe` started in `startDir`, with the art in `artDir`
    /// (bbp.png portrait, bb.png wide, bb_hero.jpg/png, bb_logo.png, bb_icon.png - any missing
    /// are left out). Returns what happened, for the page to show.
    public static (bool Ok, string Message) Add(string exe, string startDir, string? artDir)
    {
        if (Running()) return (false, "Close Steam first: it rewrites its shortcuts when it quits.");
        var dirs = UserConfigDirs().ToList();
        if (dirs.Count == 0) return (false, "No Steam account found on this PC.");
        string quotedExe = $"\"{exe}\"";
        uint uid = Crc32(Encoding.UTF8.GetBytes(quotedExe + AppName)) | 0x80000000u;
        int added = 0, updated = 0;
        foreach (var cfg in dirs)
        {
            var file = Path.Combine(cfg, "shortcuts.vdf");
            Node root;
            try
            {
                if (File.Exists(file))
                {
                    using var r = new BinaryReader(File.OpenRead(file));
                    if (r.ReadByte() != 0x00) throw new InvalidDataException("not a shortcuts file");
                    CString(r);
                    root = new Node();
                    root.Items.Add(("shortcuts", Read(r)));
                }
                else
                {
                    root = new Node();
                    root.Items.Add(("shortcuts", new Node()));
                }
            }
            catch (Exception e) { return (false, $"Could not read {file}: {e.Message}"); }
            var list = (Node)root["shortcuts"]!;
            var mine = list.Items.Select(i => i.Value).OfType<Node>()
                .FirstOrDefault(n => string.Equals(n["AppName"] as string ?? n["appname"] as string, AppName, StringComparison.OrdinalIgnoreCase));
            if (mine == null)
            {
                mine = new Node();
                list.Items.Add((list.Items.Count.ToString(), mine));
                added++;
            }
            else updated++;
            string icon = artDir != null && File.Exists(Path.Combine(artDir, "bb_icon.png")) ? Path.Combine(artDir, "bb_icon.png") : "";
            mine["appid"] = unchecked((int)uid);
            mine["AppName"] = AppName;
            mine["Exe"] = quotedExe;
            mine["StartDir"] = $"\"{startDir}\"";
            mine["icon"] = icon;
            mine["ShortcutPath"] = mine["ShortcutPath"] ?? "";
            mine["LaunchOptions"] = mine["LaunchOptions"] ?? "";
            mine["IsHidden"] = 0;
            mine["AllowDesktopConfig"] = 1;
            mine["AllowOverlay"] = 1;
            mine["OpenVR"] = 0;
            mine["Devkit"] = 0;
            mine["DevkitGameID"] = "";
            mine["DevkitOverrideAppID"] = 0;
            mine["LastPlayTime"] = mine["LastPlayTime"] ?? 0;
            mine["FlatpakAppID"] = "";
            mine["sortas"] = "";
            if (mine["tags"] is not Node) mine["tags"] = new Node();
            try
            {
                if (File.Exists(file)) File.Copy(file, file + ".bak-helias-dream", true);
                using (var w = new BinaryWriter(File.Create(file + ".tmp")))
                {
                    w.Write((byte)0);
                    W(w, "shortcuts");
                    Write(w, list);
                    w.Write((byte)8);
                }
                File.Move(file + ".tmp", file, true);
            }
            catch (Exception e) { return (false, $"Could not write {file}: {e.Message}"); }
            if (artDir != null && Directory.Exists(artDir))
            {
                var grid = Path.Combine(cfg, "grid");
                Directory.CreateDirectory(grid);
                foreach (var (src, dst) in new[] { ("bbp.png", "p.png"), ("bb.png", ".png"), ("bb_hero.jpg", "_hero.jpg"), ("bb_hero.png", "_hero.png"),
                                                   ("bb_logo.png", "_logo.png"), ("bb_icon.png", "_icon.png") })
                {
                    var from = Path.Combine(artDir, src);
                    if (File.Exists(from)) File.Copy(from, Path.Combine(grid, uid + dst), true);
                }
            }
        }
        return (true, $"{(added > 0 ? "Added to" : "Updated in")} Steam ({dirs.Count} account{(dirs.Count == 1 ? "" : "s")}). Start Steam to see it with its art.");
    }
}
