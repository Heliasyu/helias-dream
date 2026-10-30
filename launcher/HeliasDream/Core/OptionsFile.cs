using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace HeliasDream.Core;

/// <summary>
/// HeliasDream-options.toml, the file the engine's F10 menu writes: one section [options] of
/// `key = "value"` lines, then [keys] and [account]. Only [options] lines are read and changed;
/// every other line (key bindings, the account's sign-in) is written back exactly as it was.
/// </summary>
public sealed class OptionsFile
{
    string _path;
    readonly List<string> _lines = new();

    public OptionsFile(string path) { _path = path; Reload(); }

    public string FilePath => _path;

    /// Points at another file (a player profile's) and reads it.
    public void Retarget(string path) { _path = path; Reload(); }

    public void Reload()
    {
        _lines.Clear();
        if (File.Exists(_path)) _lines.AddRange(File.ReadAllLines(_path));
    }

    public string? Get(string key) => GetIn("options", key);

    public string? GetIn(string section, string key)
    {
        var (i, _) = Find(section, key);
        return i < 0 ? null : ValueOf(_lines[i]);
    }

    public string Get(string key, string fallback) => Get(key) ?? fallback;

    public void Set(string key, string value) => SetIn("options", key, value);

    /// A value written as it is (true, false, a number), not quoted.
    public void SetRawIn(string section, string key, string raw) => SetLine(section, key, $"{key} = {raw}");

    public void Remove(string section, string key)
    {
        var (i, _) = Find(section, key);
        if (i < 0) return;
        _lines.RemoveAt(i);
        Save();
    }

    public void SetIn(string section, string key, string value) =>
        SetLine(section, key, $"{key} = \"{value.Replace("\"", "")}\"");

    void SetLine(string section, string key, string line)
    {
        var (i, end) = Find(section, key);
        if (i >= 0) _lines[i] = line;
        else if (end >= 0) _lines.Insert(end, line);
        else
        {
            if (_lines.Count == 0) _lines.Add("# Helia's Dream options. Written by the options screen (F10).");
            _lines.Add($"[{section}]");
            _lines.Add(line);
        }
        Save();
    }

    /// Whether the [account] section holds a sign-in. The token itself is never read out.
    public bool SignedIn
    {
        get
        {
            bool inAccount = false;
            foreach (var l in _lines)
            {
                var t = l.Trim();
                if (t.StartsWith('[')) { inAccount = t == "[account]"; continue; }
                if (inAccount && t.StartsWith("token") && (ValueOf(t)?.Length ?? 0) > 0) return true;
            }
            return false;
        }
    }

    public string? AccountName
    {
        get
        {
            bool inAccount = false;
            foreach (var l in _lines)
            {
                var t = l.Trim();
                if (t.StartsWith('[')) { inAccount = t == "[account]"; continue; }
                if (inAccount && KeyOf(t) == "name") return ValueOf(t);
            }
            return null;
        }
    }

    /// The line of `key` in [options] (or -1), and where a new one would go (the end of the
    /// section's lines, before its trailing blank lines), or -1 without the section.
    (int line, int end) Find(string section, string key)
    {
        int start = -1;
        for (int i = 0; i < _lines.Count; i++)
        {
            var t = _lines[i].Trim();
            if (t == $"[{section}]") { start = i; continue; }
            if (start < 0) continue;
            if (t.StartsWith('['))
            {
                int end = i;
                while (end - 1 > start && _lines[end - 1].Trim().Length == 0) end--;
                return (-1, end);
            }
            if (KeyOf(t) == key) return (i, -1);
        }
        return (-1, start < 0 ? -1 : _lines.Count);
    }

    static string? KeyOf(string line)
    {
        int eq = line.IndexOf('=');
        return eq <= 0 || line.StartsWith('#') ? null : line[..eq].Trim();
    }

    static string? ValueOf(string line)
    {
        int eq = line.IndexOf('=');
        if (eq < 0) return null;
        var v = line[(eq + 1)..].Trim();
        if (v.Length >= 2 && v[0] == '"')
        {
            int close = v.IndexOf('"', 1);
            return close > 0 ? v[1..close] : v[1..];
        }
        int hash = v.IndexOf('#');
        return (hash >= 0 ? v[..hash] : v).Trim();
    }

    void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
        var tmp = _path + ".tmp";
        File.WriteAllText(tmp, string.Join('\n', _lines) + "\n", new UTF8Encoding(false));
        File.Move(tmp, _path, overwrite: true);
    }
}

/// <summary>HeliasDream.toml, read only: the game's paths and the online server.</summary>
public sealed class HeliasToml
{
    readonly Dictionary<string, string> _kv = new();

    /// The files layered as the engine does: later ones (a profile's --config) win.
    public HeliasToml(params string?[] paths)
    {
        foreach (var path in paths)
            if (path != null && File.Exists(path)) Layer(path);
    }

    void Layer(string path)
    {
        string section = "";
        foreach (var raw in File.ReadAllLines(path))
        {
            var t = raw.Trim();
            if (t.Length == 0 || t.StartsWith('#')) continue;
            if (t.StartsWith('[') && t.EndsWith(']')) { section = t[1..^1].Trim(); continue; }
            int eq = t.IndexOf('=');
            if (eq <= 0) continue;
            var v = t[(eq + 1)..].Trim();
            if (v.StartsWith('"')) { int c = v.IndexOf('"', 1); v = c > 0 ? v[1..c] : v[1..]; }
            else { int h = v.IndexOf('#'); if (h >= 0) v = v[..h].Trim(); }
            _kv[$"{section}.{t[..eq].Trim()}"] = v;
        }
    }

    public string? this[string key] => _kv.TryGetValue(key, out var v) ? v : null;
    public string? App0 => this["paths.app0"]?.TrimEnd('/', '\\');
    public string? Eboot => this["paths.eboot"];
    public string DataDir => this["paths.data"] ?? Path.Combine(Paths.HeliasDataDir, "data");
    public string OnlineHost => this["online.host"] ?? "thehuntersdream.com";
    /// The account server (the website), as the engine's [online] auth_server.
    public string AuthServer => this["online.auth_server"] ?? $"https://{OnlineHost}";
}
