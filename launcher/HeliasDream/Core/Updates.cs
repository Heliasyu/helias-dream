using System;
using System.Diagnostics;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HeliasDream.Core;

/// <summary>
/// The engine's version (HeliasDream.bin --version) against the newest release on GitHub
/// (Heliasyu/helias-dream), for the header's line.
/// </summary>
public static class Updates
{
    public const string Repo = "Heliasyu/helias-dream";

    public static string? EngineVersion(string? gameDir)
    {
        var engine = Paths.EngineIn(gameDir);
        if (engine == null) return null;
        try
        {
            var psi = new ProcessStartInfo(engine, "--version") { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
            if (gameDir != null) psi.WorkingDirectory = gameDir;
            using var p = Process.Start(psi);
            if (p == null) return null;
            var line = p.StandardOutput.ReadLine();
            if (!p.WaitForExit(3000)) { try { p.Kill(); } catch (Exception) { } return null; }
            var v = line?.Trim().Split(' ')[0];
            return string.IsNullOrEmpty(v) || !v.StartsWith('v') ? null : v;
        }
        catch (Exception) { return null; }
    }

    public sealed record Release(string Tag, string Url);

    public static async Task<Release?> Latest()
    {
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(6) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("helias-dream-launcher");
            http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
            var json = await http.GetStringAsync($"https://api.github.com/repos/{Repo}/releases/latest");
            using var doc = JsonDocument.Parse(json);
            var tag = doc.RootElement.GetProperty("tag_name").GetString();
            var url = doc.RootElement.GetProperty("html_url").GetString();
            return tag == null || url == null ? null : new Release(tag, url);
        }
        catch (Exception) { return null; }
    }

    /// The release a version is built from or after: v0.2.18-vk.2-22-gabc -> (0,2,18,2).
    static int[]? Parts(string v)
    {
        var m = Regex.Match(v, @"^v(\d+)\.(\d+)\.(\d+)(?:-vk\.(\d+))?");
        if (!m.Success) return null;
        return new[] { int.Parse(m.Groups[1].Value), int.Parse(m.Groups[2].Value), int.Parse(m.Groups[3].Value),
                       m.Groups[4].Success ? int.Parse(m.Groups[4].Value) : 0 };
    }

    /// Whether `latest` is newer than the release `current` is built from.
    public static bool Newer(string latest, string current)
    {
        var a = Parts(latest);
        var b = Parts(current);
        if (a == null || b == null) return false;
        for (int i = 0; i < 4; i++)
            if (a[i] != b[i]) return a[i] > b[i];
        return false;
    }
}
