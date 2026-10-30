using System;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace HeliasDream.Core;

/// <summary>
/// The online account, as the F10 menu's Account section has it (src/net/account.cpp, the same
/// calls to the same server): link this PC with an account signed into on the website with
/// Discord (a device code), or sign out. The name and token go to the profile's options file,
/// [account], where the game reads them.
/// </summary>
public static class Account
{
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(10) };

    public sealed record Link(string DeviceCode, string UserCode, string Uri, int Interval, int ExpiresIn);

    static string DeviceName => $"Helia's Dream on {Environment.MachineName}";

    static async Task<JsonElement?> Call(string server, string path, object body, CancellationToken ct = default)
    {
        using var r = await Http.PostAsJsonAsync(server.TrimEnd('/') + path, body, ct);
        var text = await r.Content.ReadAsStringAsync(ct);
        try
        {
            var doc = JsonDocument.Parse(text).RootElement;
            if (doc.ValueKind != JsonValueKind.Object || !doc.TryGetProperty("ResKind", out var kind))
                throw new InvalidOperationException($"{server} has no account service (HTTP {(int)r.StatusCode})");
            if (kind.GetInt32() != 0)
                throw new InvalidOperationException(doc.TryGetProperty("Message", out var m) ? m.GetString() ?? "refused" : "refused");
            return doc;
        }
        catch (JsonException) { throw new InvalidOperationException($"HTTP {(int)r.StatusCode}"); }
    }

    static string Str(JsonElement e, string k) => e.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";
    static int Int(JsonElement e, string k, int d) => e.TryGetProperty(k, out var v) && v.TryGetInt32(out var i) ? i : d;

    public static async Task<Link> Start(string server)
    {
        var r = (await Call(server, "/auth/device/start", new { DeviceName }))!.Value;
        var link = new Link(Str(r, "DeviceCode"), Str(r, "UserCode"), Str(r, "VerificationUri"), Math.Max(1, Int(r, "Interval", 5)), Int(r, "ExpiresIn", 600));
        if (link.DeviceCode.Length == 0 || link.UserCode.Length == 0) throw new InvalidOperationException("no code in the reply");
        return link;
    }

    /// Waits for the code to be approved on the website. The signed-in name, or an exception
    /// saying why not (denied, expired, cancelled).
    public static async Task<string> Wait(string server, Link link, OptionsFile options, CancellationToken ct)
    {
        int interval = link.Interval;
        for (int waited = 0; waited < link.ExpiresIn;)
        {
            await Task.Delay(TimeSpan.FromSeconds(interval), ct);
            waited += interval;
            var r = (await Call(server, "/auth/device/poll", new { link.DeviceCode }, ct))!.Value;
            switch (Str(r, "Status"))
            {
                case "pending": continue;
                case "slow_down": interval++; continue;
                case "approved":
                    var token = Str(r, "Token");
                    var id = Str(r, "OnlineId");
                    if (token.Length == 0 || id.Length == 0) throw new InvalidOperationException("no token in the reply");
                    options.SetIn("account", "name", id);
                    options.SetIn("account", "token", token);
                    return id;
                case "denied": throw new InvalidOperationException("The link was denied on the website.");
                default: throw new InvalidOperationException("The code expired; link again.");
            }
        }
        throw new InvalidOperationException("The code expired; link again.");
    }

    /// Tells the server to forget this PC's token, and clears it here.
    public static async Task SignOut(string server, OptionsFile options)
    {
        var token = options.GetIn("account", "token");
        options.Remove("account", "token");
        options.Remove("account", "name");
        if (string.IsNullOrEmpty(token)) return;
        try { await Call(server, "/auth/logout", new { Token = token }); } catch (Exception) { }
    }
}

/// <summary>The server's public numbers (thehuntersdream.com/api/stats), for the Online page.</summary>
public sealed record CommunityStats(long Hunters, long Summons, long Messages, long Deaths, long BossClears, int CoopSigns, int InvadeSigns)
{
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(8) };

    public static async Task<CommunityStats?> Fetch(string host)
    {
        try
        {
            var root = JsonDocument.Parse(await Http.GetStringAsync($"https://{host}/api/stats")).RootElement.GetProperty("stats");
            var o = root.GetProperty("overview");
            long L(JsonElement e, string k) => e.TryGetProperty(k, out var v) && v.TryGetInt64(out var n) ? n : 0;
            int I(JsonElement e, string k) => (int)L(e, k);
            var act = root.TryGetProperty("activity", out var a) ? a : default;
            var hunt = root.TryGetProperty("hunt", out var h) ? h : default;
            return new CommunityStats(L(o, "total_users"), L(o, "total_summons"), L(o, "total_messages"), L(o, "total_deaths"),
                hunt.ValueKind == JsonValueKind.Object ? L(hunt, "boss_clears") : 0,
                act.ValueKind == JsonValueKind.Object ? I(act, "summon_coop") : 0, act.ValueKind == JsonValueKind.Object ? I(act, "summon_invade") : 0);
        }
        catch (Exception) { return null; }
    }
}
