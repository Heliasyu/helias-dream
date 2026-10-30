using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace HeliasDream.Core;

/// <summary>
/// Player profiles: each its own settings and account (the engine's HeliasDream-options-&lt;name&gt;.toml,
/// which a --config file selects, src/host/options.cpp) and its own saves and trophies (that
/// config's [paths] data). The game's paths stay the shared HeliasDream.toml's. "Default" is no
/// --config at all: the files as they always were.
/// </summary>
public static class Profiles
{
    public const string Default = "Default";

    static string Dir => Path.Combine(Paths.HeliasConfigDir, "profiles");

    public static string Stem(string name)
    {
        var sb = new StringBuilder();
        foreach (var ch in name.Trim()) sb.Append(char.IsLetterOrDigit(ch) || ch is '-' or '_' ? ch : '_');
        return sb.ToString();
    }

    public static List<string> List()
    {
        var list = new List<string> { Default };
        if (Directory.Exists(Dir))
            list.AddRange(Directory.EnumerateFiles(Dir, "*.toml").Select(f => Path.GetFileNameWithoutExtension(f)).OrderBy(n => n));
        return list;
    }

    /// The --config file a profile starts the game with (null for Default).
    public static string? ConfigFile(string profile) =>
        profile == Default ? null : Path.Combine(Dir, Stem(profile) + ".toml");

    /// The options file the engine reads for a profile.
    public static string OptionsFile(string profile) =>
        profile == Default ? Paths.OptionsFile : Path.Combine(Paths.HeliasConfigDir, $"HeliasDream-options-{Stem(profile)}.toml");

    public static string DataDir(string profile) =>
        Path.Combine(Paths.HeliasDataDir, "profiles", Stem(profile), "data");

    /// Makes a profile: its config (its own save folder, the player's name), and its settings
    /// copied from the Default's without the account, so it starts signed out.
    public static string? Create(string name)
    {
        var stem = Stem(name);
        if (stem.Length == 0 || stem == Default) return null;
        Directory.CreateDirectory(Dir);
        var cfg = Path.Combine(Dir, stem + ".toml");
        if (File.Exists(cfg)) return stem;
        var data = DataDir(stem);
        Directory.CreateDirectory(data);
        File.WriteAllText(cfg,
            $"# Helia's Dream profile \"{stem}\": its own saves, settings and account (HeliasDream-options-{stem}.toml).\n" +
            "# The game's paths are HeliasDream.toml's.\n" +
            $"[paths]\ndata = \"{data.Replace("\\", "/")}\"\n\n[player]\nname = \"{name.Trim().Replace("\"", "")}\"\n");
        var opts = OptionsFile(stem);
        if (!File.Exists(opts) && File.Exists(Paths.OptionsFile))
        {
            var lines = new List<string>();
            bool account = false;
            foreach (var l in File.ReadAllLines(Paths.OptionsFile))
            {
                var t = l.Trim();
                if (t.StartsWith('[')) account = t == "[account]";
                if (!account) lines.Add(l);
            }
            File.WriteAllLines(opts, lines);
        }
        return stem;
    }

    /// Takes a profile off the list. Its saves stay where they are (DataDir), so nothing is lost.
    public static void Remove(string profile)
    {
        var cfg = ConfigFile(profile);
        if (cfg != null && File.Exists(cfg)) File.Move(cfg, cfg + ".removed", true);
    }
}
