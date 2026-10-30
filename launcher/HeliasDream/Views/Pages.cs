using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.Controls.Primitives;
using Path = Avalonia.Controls.Shapes.Path;
using HeliasDream.Core;

namespace HeliasDream.Views;

/// <summary>What every page gets: the files, and a way to ask the window for things.</summary>
public sealed class Ctx
{
    public required OptionsFile Options { get; init; }
    public required LauncherSettings Settings { get; init; }
    public required Func<HeliasToml> Config { get; init; }
    public required Func<string?> GameDir { get; init; }
    public required Action<int> GoTo { get; init; }
    public required Action Changed { get; init; }
    public required Func<string?, System.Threading.Tasks.Task<string?>> PickFolder { get; init; }
    /// The monitors: connector name and a label; the primary first.
    public required Func<IReadOnlyList<(string Name, string Label, bool Primary)>> Monitors { get; init; }
    /// The primary monitor's size in pixels.
    public required Func<(int W, int H)> ScreenSize { get; init; }
    /// The next key (mouse false) or mouse button / wheel (mouse true) pressed: its SDL name or
    /// mouse index to `done`; Escape cancels (null, 0).
    public required Action<bool, Action<string?, int>> Capture { get; init; }
    public required Func<string, string?, System.Threading.Tasks.Task<string?>> PickFile { get; init; }
    /// Makes another player profile the one the launcher and the game use.
    public required Action<string> SwitchProfile { get; init; }
    /// The Console page's view: the game's log while it runs, or a tool's.
    public required ConsoleView Console { get; init; }
}

public static class Pages
{
    public const int PlayTab = 0, TrophiesTab = 6, SetupPage = 8, ConsolePage = 9;

    public static readonly string[] Names = { "Play", "Display", "Upscaling", "Graphics", "Controls", "Online", "Trophies", "More" };

    public static Control Build(int index, Ctx ctx) => index switch
    {
        0 => Play(ctx),
        1 => Display(ctx),
        2 => Upscaling(ctx),
        3 => Graphics(ctx),
        4 => Controls(ctx),
        5 => Online(ctx),
        6 => TrophiesPage(ctx),
        SetupPage => Setup(ctx),
        ConsolePage => ctx.Console,
        _ => More(ctx),
    };

    // ---- Play ----

    static Control Play(Ctx ctx)
    {
        var cfg = ctx.Config();
        var checks = ReadyCheck.Local(ctx.GameDir(), cfg, ctx.Options);
        var problems = checks.Count(c => c.State == CheckState.Problem);

        var list = Ui.Col(8);
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Margin = new Thickness(6, 0, 6, 6) };
        head.Children.Add(Ui.Eyebrow("Ready check"));
        var summary = Ui.Text(problems == 0 ? $"{checks.Count} of {checks.Count} ready" : $"{problems} to fix", 13);
        summary.Foreground = Brush.Parse(problems == 0 ? "#9FD4A8" : "#F0A0A0");
        Grid.SetColumn(summary, 1);
        head.Children.Add(summary);
        list.Children.Add(head);
        foreach (var c in checks) list.Children.Add(CheckRow(c));
        var server = CheckRow(new CheckItem("Server", "checking…", CheckState.Note));
        list.Children.Add(server);
        _ = ReadyCheck.PingServer(cfg.OnlineHost).ContinueWith(t => Dispatcher.UIThread.Post(() =>
        {
            var ms = t.Result;
            var row = CheckRow(ms != null ? new CheckItem("Server reachable", $"{cfg.OnlineHost} · {ms} ms", CheckState.Ok)
                                          : new CheckItem("Server not answering", cfg.OnlineHost, CheckState.Note));
            int i = list.Children.IndexOf(server);
            if (i >= 0) list.Children[i] = row;
        }));

        var left = Ui.Col(18,
            Ui.Col(10, Ui.Title("Bloodborne Launcher", 54), Ui.Text("Everything checks out. Press Play, or Enter.", 15, "soft")),
            ProfileRow(ctx),
            Ui.Glass(list, 18));
        left.Children[0].Margin = new Thickness(28, 26, 28, 8);
        if (problems > 0) ((TextBlock)((StackPanel)left.Children[0]).Children[1]).Text = "Fix the marked lines before playing.";

        // This hunt
        var res = ctx.Options.Get("resolution", "1920x1080");
        var upscaler = ctx.Options.Get("upscaler", "FSR 1");
        var quality = ctx.Options.Get("upscale_quality", "Native AA");
        var scene = SceneSize(res, upscaler, quality);
        var facts = new UniformGrid { Columns = 2 };
        foreach (var (k, v) in new[]
                 {
                     ("Output", $"{res.Replace("x", "×")} {ctx.Options.Get("window_mode", "Windowed").ToLowerInvariant()}"),
                     ("Scene renders at", scene),
                     ("Upscaler", upscaler == "Off" ? "Off" : $"{upscaler}"),
                     ("Frame cap", ctx.Options.Get("frame_cap", "30") is var f && f == "Uncapped" ? "Uncapped" : $"{f} fps"),
                 })
        {
            var w = Ui.Well(Ui.Col(4, Ui.Text(k, 12, "muted"), Ui.Text(v, 15, null, FontWeight.SemiBold)));
            w.Margin = new Thickness(0, 0, 10, 10);
            facts.Children.Add(w);
        }
        var quick = Ui.Col(10,
            Ui.Tip(QuickRow("Frame rate", Ui.Segmented(OptionSchema.FrameCap.Values, ctx.Options.Get("frame_cap", "30"), v => { ctx.Options.Set("frame_cap", v); ctx.Changed(); })), OptionSchema.Tips["frame_cap"]),
            Ui.Tip(QuickRow("Upscaler", Ui.Segmented(OptionSchema.Upscaler.Values, upscaler, v => { ctx.Options.Set("upscaler", v); ctx.Changed(); },
                label: v => v switch { "Off" => "Native", "Native TAA" => "TAA", _ => v }, fontSize: 12)), OptionSchema.Tips["upscaler"]),
            Ui.Tip(QuickRow("Quality", Ui.Segmented(OptionSchema.Quality.Values, quality, v => { ctx.Options.Set("upscale_quality", v); ctx.Changed(); },
                label: v => v switch { "Native AA" => "Native", "Performance" => "Perf.", "Ultra Performance" => "Ultra", _ => v }, fontSize: 12)), OptionSchema.Tips["upscale_quality"]));
        if (upscaler == "FSR 4.1.1") quick.Children.Add(Fsr4Note(ctx, compact: true));
        if (upscaler == "FSR 4") quick.Children.Add(Fsr4V07Note(ctx, compact: true));
        var hunt = Ui.Glass(Ui.Col(14, Ui.Eyebrow("This hunt"), facts, quick), 22);

        var trophies = Trophies.Load(cfg.DataDir);
        var earned = trophies.Count(t => t.Unlocked);
        var latest = trophies.Where(t => t.Unlocked).OrderByDescending(t => t.Earned).FirstOrDefault();
        var bar = new Grid { Height = 8 };
        bar.Children.Add(new Border { Background = Brush.Parse("#59000000"), CornerRadius = new CornerRadius(4) });
        bar.Children.Add(new Border
        {
            HorizontalAlignment = HorizontalAlignment.Left, CornerRadius = new CornerRadius(4),
            Width = 300.0 * earned / trophies.Count,
            Background = new LinearGradientBrush
            {
                StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative),
                GradientStops = { new(Color.Parse("#A9BFE3"), 0), new(Color.Parse("#E9EEFA"), 1) },
            },
        });
        var cup = new Border
        {
            Width = 58, Height = 58, CornerRadius = new CornerRadius(18), Background = Ui.Medal(Grade.Gold),
            Child = new Path
            {
                Data = Geometry.Parse("M7,4 L17,4 L17,8 A5,5 0 0 1 7,8 Z M7,6 L4,6 A3,3 0 0 0 7,10 M17,6 L20,6 A3,3 0 0 1 17,10 M12,13 L12,17 M8,20 L16,20"),
                Stroke = Brush.Parse("#2B2010"), StrokeThickness = 1.7, Width = 24, Height = 24, Stretch = Stretch.None,
                HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            },
        };
        var tg = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 18 };
        tg.Children.Add(cup);
        var mid = Ui.Col(8, Ui.Row(10, Ui.Text("Trophies", 15, null, FontWeight.SemiBold),
            Ui.Text($"{earned} of {trophies.Count}{(latest != null ? " · latest: " + latest.Trophy.Name : "")}", 13, "muted")), bar);
        mid.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(mid, 1); tg.Children.Add(mid);
        var pct = Ui.Text($"{earned * 100 / trophies.Count}%", 34); pct.Classes.Add("display"); pct.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(pct, 2); tg.Children.Add(pct);
        var trophyCard = new Button { Classes = { "flat" }, Padding = new Thickness(0), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Content = Ui.Glass(tg, 18), Background = Brushes.Transparent };
        trophyCard.Click += (_, _) => ctx.GoTo(TrophiesTab);

        var right = Ui.Col(18, hunt, trophyCard);
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("1.15*,*"), ColumnSpacing = 18 };
        g.Children.Add(left);
        Grid.SetColumn(right, 1);
        g.Children.Add(right);
        return g;
    }

    // Player profiles (Core/Profiles.cs): who is playing - their settings, account, saves
    // and trophies.
    static Control ProfileRow(Ctx ctx)
    {
        var names = Profiles.List();
        var combo = new ComboBox { ItemsSource = names, SelectedItem = ctx.Settings.Profile, MinWidth = 170, VerticalAlignment = VerticalAlignment.Center };
        combo.SelectionChanged += (_, _) =>
        {
            if (combo.SelectedItem is string p && p != ctx.Settings.Profile) ctx.SwitchProfile(p);
        };
        var name = new TextBox { Watermark = "New profile's name", Width = 170, IsVisible = false, VerticalAlignment = VerticalAlignment.Center };
        Button? remove = null;
        var add = Ui.Ghost("New", () =>
        {
            if (!name.IsVisible) { name.IsVisible = true; name.Focus(); return; }
            var made = string.IsNullOrWhiteSpace(name.Text) ? null : Profiles.Create(name.Text);
            if (made != null) ctx.SwitchProfile(made);
        });
        name.KeyDown += (_, e) =>
        {
            if (e.Key == Avalonia.Input.Key.Enter)
            {
                e.Handled = true;
                var made = string.IsNullOrWhiteSpace(name.Text) ? null : Profiles.Create(name.Text);
                if (made != null) ctx.SwitchProfile(made);
            }
        };
        bool armed = false;
        remove = Ui.Ghost("Remove", () =>
        {
            if (ctx.Settings.Profile == Profiles.Default) return;
            if (!armed) { armed = true; remove!.Content = "Click again"; return; }
            Profiles.Remove(ctx.Settings.Profile);
            ctx.SwitchProfile(Profiles.Default);
        });
        remove.IsVisible = ctx.Settings.Profile != Profiles.Default;
        foreach (var b in new[] { add, remove }) { b.Height = 38; b.FontSize = 13; }
        var row = Ui.Row(8, Ui.Text("Profile", 14, "soft"), combo, name, add, remove);
        row.Children[0].VerticalAlignment = VerticalAlignment.Center;
        row.Margin = new Thickness(28, 0, 0, 0);
        return Ui.Tip(row, "Who is playing. Each profile has its own settings, online account, saves and trophies; the game's files are shared. Default is the one you have always had. Remove takes a profile off the list and keeps its saves.");
    }

    static string SceneSize(string res, string upscaler, string quality)
    {
        var p = res.Split('x');
        if (p.Length != 2 || !int.TryParse(p[0], out var w) || !int.TryParse(p[1], out var h)) return res;
        var s = upscaler is "FSR 3.1" or "FSR 4.1.1" or "FSR 4" or "DLSS" ? OptionSchema.ScaleOf(quality) : 1.0;  // Native TAA: the full size
        return $"{(int)Math.Round(w / s)}×{(int)Math.Round(h / s)}";
    }

    static Control CheckRow(CheckItem c)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 12 };
        g.Children.Add(Ui.StatusDot(c.State));
        var l = Ui.Text(c.Label, 14, null, FontWeight.Medium); l.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(l, 1); g.Children.Add(l);
        var d = Ui.Text(c.Detail, 12, "muted"); d.VerticalAlignment = VerticalAlignment.Center; d.TextWrapping = TextWrapping.NoWrap;
        Grid.SetColumn(d, 2); g.Children.Add(d);
        return Ui.Well(g);
    }

    static Control QuickRow(string label, Control control)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        var t = Ui.Text(label, 14); t.VerticalAlignment = VerticalAlignment.Center;
        g.Children.Add(t);
        Grid.SetColumn(control, 1); g.Children.Add(control);
        return g;
    }

    // ---- Settings pages ----

    static Control Header(string title, string sub) =>
        Ui.Col(8, Ui.Title(title, 44), Ui.Text(sub, 14, "soft"));

    static Control Settings(string title, string sub, OptionGroup[] groups, Ctx ctx)
    {
        var col = Ui.Col(18, Header(title, sub));
        col.Children[0].Margin = new Thickness(8, 6, 8, 0);
        // Two columns, each a stack: the groups go to whichever is shorter so far.
        var left = Ui.Col(18);
        var right = Ui.Col(18);
        int lrows = 0, rrows = 0;
        foreach (var gr in groups)
        {
            var g = Ui.Group(gr, ctx.Options, ctx.Changed);
            if (groups.Length == 1 || lrows <= rrows) { left.Children.Add(g); lrows += gr.Options.Length + 2; }
            else { right.Children.Add(g); rrows += gr.Options.Length + 2; }
        }
        col.Children.Add(Columns(left, right));
        return col;
    }

    static Grid Columns(Control left, Control right)
    {
        var two = new Grid { ColumnDefinitions = new ColumnDefinitions("*,*"), ColumnSpacing = 18 };
        left.VerticalAlignment = right.VerticalAlignment = VerticalAlignment.Top;
        two.Children.Add(left);
        Grid.SetColumn(right, 1);
        two.Children.Add(right);
        return two;
    }

    public static readonly (string Value, string Label, string Help)[] PresentModes =
    {
        ("mailbox", "Mailbox", "No tearing, low latency: a finished frame replaces the waiting one. Recommended."),
        ("fifo", "FIFO (V-Sync)", "Classic vertical sync: every frame waits for the display. No tearing, a little more latency."),
        ("relaxed", "FIFO Relaxed", "V-Sync that tears instead of stuttering when a frame is late."),
        ("immediate", "Immediate", "Lowest latency; frames are shown at once and can tear."),
        ("auto", "Auto", "Follows the V-Sync setting: Mailbox with it on, Immediate with it off."),
    };

    static Control Display(Ctx ctx)
    {
        var o = ctx.Options;
        var st = ctx.Settings;

        // Screen: the monitor, the resolution and window, and under Advanced the
        // live resolution change and how frames are delivered.
        var monitors = ctx.Monitors();
        var labels = new List<string> { "Primary monitor" };
        labels.AddRange(monitors.Select(m => m.Label));
        var monitor = new ComboBox { ItemsSource = labels, MinWidth = 220, VerticalAlignment = VerticalAlignment.Center };
        int at = monitors.ToList().FindIndex(m => m.Name == st.Monitor);
        monitor.SelectedIndex = string.IsNullOrEmpty(st.Monitor) || at < 0 ? 0 : at + 1;
        monitor.SelectionChanged += (_, _) =>
        {
            st.Monitor = monitor.SelectedIndex <= 0 ? "" : monitors[monitor.SelectedIndex - 1].Name;
            st.Save(); ctx.Changed();
        };
        var screen = Ui.Col(8, Ui.Eyebrow("Screen"),
            Ui.Tip(Ui.Row2("Monitor", "The game opens here, fullscreen included.", monitor),
                "The monitor the game opens on, fullscreen included. Primary is the one your desktop marks as main (KDE: priority 1). Applies when the game starts."));
        foreach (var d in OptionSchema.Screen) screen.Children.Add(Ui.OptionRow(d, o, ctx.Changed));
        screen.Children.Add(Ui.Tip(CheckRow(new CheckItem("High-resolution UI", "Always on: menus, HUD and text are drawn at the full resolution", CheckState.Ok)),
            "Helia's Dream draws the game's menus, HUD and text at the output resolution itself (each Scaleform movie's viewport is the Resolution setting), so they stay sharp at 1440p and 4K. Nothing to switch on."));
        screen.Children.Add(Ui.OptionRow(OptionSchema.MenuKey, o, ctx.Changed));

        var current = PresentModes.FirstOrDefault(m => m.Value == st.PresentMode);
        if (current.Value == null) current = PresentModes[0];
        var help = Ui.Text(current.Help, 12, "muted");
        var combo = new ComboBox { ItemsSource = PresentModes.Select(m => m.Label).ToArray(), SelectedItem = current.Label, MinWidth = 200, VerticalAlignment = VerticalAlignment.Center };
        combo.SelectionChanged += (_, _) =>
        {
            var m = PresentModes.FirstOrDefault(x => x.Label == (string?)combo.SelectedItem);
            if (m.Value == null) return;
            st.PresentMode = m.Value; st.Save(); help.Text = m.Help; ctx.Changed();
        };
        var pg = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
        pg.Children.Add(Ui.Col(2, Ui.Text("Presentation mode", 14, null, FontWeight.Medium), help));
        Grid.SetColumn(combo, 1); pg.Children.Add(combo);
        screen.Children.Add(Ui.Advanced("Advanced",
            Ui.OptionRow(OptionSchema.LiveResolution, o, ctx.Changed),
            Ui.Tip(Ui.Well(pg),
                "How finished frames reach the screen. Mailbox: never tears, and a newer frame replaces one still waiting, so latency stays low (recommended). FIFO: classic V-Sync, every frame waits its turn. FIFO Relaxed: V-Sync, but a late frame tears instead of stuttering. Immediate: lowest latency, can tear. Auto: Mailbox with V-Sync on, Immediate with it off. Applies when the game starts.")));

        var frame = Ui.Col(8, Ui.Eyebrow("Frame rate"), Ui.OptionRow(OptionSchema.FrameCap, o, ctx.Changed));

        // FPS counter: on, where, and the building-shaders note; its look and what it shows
        // folded under Advanced.
        var fps = Ui.Col(8, Ui.Eyebrow("FPS counter"), Ui.OptionRow(OptionSchema.FpsShow, o, ctx.Changed),
            Ui.OptionRow(OptionSchema.FpsPosition, o, ctx.Changed), Ui.OptionRow(OptionSchema.CompileNote, o, ctx.Changed));
        var fpsMore = OptionSchema.FpsLook.Select(d => Ui.OptionRow(d, o, ctx.Changed)).ToList();
        fpsMore.Add(Ui.Eyebrow("What it shows"));
        fpsMore[^1].Margin = new Thickness(4, 8, 0, 2);
        fpsMore.AddRange(OptionSchema.FpsParts.Select(d => Ui.OptionRow(d, o, ctx.Changed)));
        fps.Children.Add(Ui.Advanced("Advanced", fpsMore.ToArray()));

        // Language: the game's own (its text and voices from the disc's message bundles).
        var lang = Ui.Col(8, Ui.Eyebrow("Language"), Ui.OptionRow(OptionSchema.MenuLanguage, o, ctx.Changed), Ui.OptionRow(OptionSchema.GameLanguage, o, ctx.Changed));

        // HDR, with what the desktop says about the monitor.
        string? connector = string.IsNullOrEmpty(st.Monitor) ? monitors.FirstOrDefault(m => m.Primary).Name : st.Monitor;
        var desk = DesktopHdr.IsOn(connector);
        var mon = string.IsNullOrEmpty(connector) ? "your monitor" : connector;
        var hint = Ui.Text(desk == true ? $"HDR is on for {mon} in your desktop's settings: ready."
                           : desk == false ? $"HDR is off for {mon} in your desktop's settings. Turn it on there (KDE: System Settings > Display Configuration > {mon} > HDR) for HDR output to work."
                           : "Turn HDR on for your monitor in your desktop's display settings first.", 12, desk == true ? "soft" : "muted");
        hint.Margin = new Thickness(4, 0, 4, 4);
        var hdr = Ui.Col(8, Ui.Eyebrow("HDR"), hint);
        foreach (var d in OptionSchema.Hdr) hdr.Children.Add(Ui.OptionRow(d, o, ctx.Changed));

        var page = Ui.Col(18, Header("Display", "Monitor, resolution, frame rate, HDR, the FPS counter and language."));
        page.Children[0].Margin = new Thickness(8, 6, 8, 0);
        page.Children.Add(Columns(Ui.Col(18, Ui.Glass(screen, 18), Ui.Glass(frame, 18), Ui.Glass(lang, 18)), Ui.Col(18, Ui.Glass(hdr, 18), Ui.Glass(fps, 18))));
        return page;
    }

    static Control Upscaling(Ctx ctx)
    {
        var o = ctx.Options;
        var res = o.Get("resolution", "1920x1080");
        var sceneText = Ui.Text("", 13, "soft");
        void Refresh() => sceneText.Text = $"Scene {SceneSize(res, o.Get("upscaler", "FSR 1"), o.Get("upscale_quality", "Native AA"))} → {res.Replace("x", "×")}";
        Refresh();

        var upscaler = Ui.Tip(Ui.Segmented(OptionSchema.Upscaler.Values, o.Get("upscaler", "FSR 1"), v => { o.Set("upscaler", v); Refresh(); ctx.Changed(); }, fontSize: 14, stretch: true), OptionSchema.Tips["upscaler"]);

        var tiles = new UniformGrid { Columns = 5 };
        var tileButtons = new List<Button>();
        foreach (var q in OptionSchema.Quality.Values)
        {
            var pct = q switch { "Quality" => "67%", "Balanced" => "58%", "Performance" => "50%", "Ultra Performance" => "33%", _ => "100%" };
            var b = new Button
            {
                Classes = { "flat", "tile" }, Margin = new Thickness(0, 0, 8, 0), HorizontalAlignment = HorizontalAlignment.Stretch,
                Content = Ui.Col(4, Ui.Text(q == "Ultra Performance" ? "Ultra Perf." : q, 14, null, FontWeight.Bold), Ui.Text(pct, 12)),
                Tag = q,
            };
            b.Classes.Set("selected", q == o.Get("upscale_quality", "Native AA"));
            b.Click += (_, _) =>
            {
                o.Set("upscale_quality", q); Refresh(); ctx.Changed();
                foreach (var x in tileButtons) x.Classes.Set("selected", ReferenceEquals(x, b));
            };
            tileButtons.Add(b);
            tiles.Children.Add(b);
        }

        Ui.Tip(tiles, OptionSchema.Tips["upscale_quality"]);
        var sharpRow = Ui.OptionRow(OptionSchema.Sharpness, o, ctx.Changed);
        var left = Ui.Glass(Ui.Col(22,
            Header("Upscaling", "Changes apply live in the game too (F10)."),
            Ui.Col(10, Ui.Eyebrow("Upscaler"), upscaler),
            Ui.Col(10, QuickRowText(Ui.Eyebrow("Quality"), sceneText), tiles),
            sharpRow,
            Ui.Col(8, OptionSchema.UpscalingExtras[0].Options.Select(d => Ui.OptionRow(d, o, ctx.Changed)).ToArray())), 26);

        bool built = Directory.Exists(Paths.Fsr4Assets);
        var badge = new Border
        {
            CornerRadius = new CornerRadius(999), Padding = new Thickness(12, 5), HorizontalAlignment = HorizontalAlignment.Right,
            Background = Brush.Parse(built ? "#2978C88C" : "#29E0B060"), BorderBrush = Brush.Parse(built ? "#5978C88C" : "#59E0B060"), BorderThickness = new Thickness(1),
            Child = Ui.Text(built ? "Ready" : "Not built", 12, null, FontWeight.SemiBold),
        };
        var fsrHead = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        fsrHead.Children.Add(Ui.Text("FSR 4.1.1", 16, null, FontWeight.SemiBold));
        Grid.SetColumn(badge, 1); fsrHead.Children.Add(badge);
        var fsr = Ui.Glass(Ui.Col(14, fsrHead,
            Ui.Text("Built once from your own copy of AMD's DLL with build-fsr4.sh in the release folder. Nothing from AMD ships with Helia's Dream.", 13, "soft"),
            Ui.Well(Ui.Col(4, Ui.Text("Assets", 12, "muted"), Ui.Text(built ? Paths.Fsr4Assets : "Not found", 13, null, FontWeight.SemiBold))),
            Fsr4Note(ctx, compact: false)), 22);
        var fsr4v07 = Ui.Glass(Ui.Col(14, Ui.Text("FSR 4", 16, null, FontWeight.SemiBold),
            Ui.Text("AMD's FSR 4 INT8 model, from the source AMD published under MIT, as SPIR-V that FireBurn's Q2RTX port builds from it. Downloaded once; nothing from AMD ships with Helia's Dream.", 13, "soft"),
            Fsr4V07Note(ctx, compact: false)), 22);

        var what = Ui.Glass(Ui.Col(14, Ui.Text("What you get", 16, null, FontWeight.SemiBold),
            Ui.Text("The HUD and menus always render at the full resolution; only the 3D scene is upscaled.", 13, "soft")), 22);

        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("1.3*,*"), ColumnSpacing = 18 };
        g.Children.Add(left);
        var right = Ui.Col(18, fsr, fsr4v07, what);
        Grid.SetColumn(right, 1); g.Children.Add(right);
        return g;
    }

    static Control QuickRowText(Control a, Control b)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        g.Children.Add(a);
        b.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(b, 1); g.Children.Add(b);
        return g;
    }

    const string Website = "https://thehuntersdream.com", DiscordInvite = "https://discord.gg/ZTrJ28aX7K";

    static void OpenUrl(string url)
    {
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(OperatingSystem.IsWindows() ? "explorer" : "xdg-open", url) { UseShellExecute = false }); }
        catch (Exception) { }
    }

    static Control Online(Ctx ctx)
    {
        var cfg = ctx.Config();
        var o = ctx.Options;

        // Account: signed in or not, linking this PC with Discord through the website, signing out.
        var status = Ui.Text("", 15, null, FontWeight.SemiBold);
        var detail = Ui.Text("", 12, "muted");
        var code = Ui.Text("", 30, "display");
        code.IsVisible = false;
        code.LetterSpacing = 4;
        System.Threading.CancellationTokenSource? waiting = null;
        Button link = null!, signOut = null!, openSite = null!, cancel = null!;
        void Show()
        {
            o.Reload();
            bool signed = o.SignedIn;
            status.Text = signed ? $"Signed in as {o.AccountName}" : "Not signed in";
            detail.Text = signed ? $"This profile ({ctx.Settings.Profile}) plays online as {o.AccountName}. The game reads it when it starts."
                                 : "The live server plays only with signed-in hunters. Link this PC with your Discord account on the website: no password.";
            link.IsVisible = !signed && waiting == null;
            signOut.IsVisible = signed && waiting == null;
            cancel.IsVisible = waiting != null;
            ctx.Changed();
        }
        link = Ui.Ghost("Link with Discord", async () =>
        {
            waiting = new System.Threading.CancellationTokenSource();
            Show();
            status.Text = "Asking the server for a code…";
            try
            {
                var l = await Account.Start(cfg.AuthServer);
                code.Text = l.UserCode; code.IsVisible = true;
                status.Text = "Approve this code on the website";
                detail.Text = $"Your browser opened {l.Uri}: sign in there with Discord and approve the code. It is good for {l.ExpiresIn / 60} minutes.";
                openSite.Tag = $"{l.Uri}?code={l.UserCode}";
                openSite.IsVisible = true;
                OpenUrl((string)openSite.Tag);
                var name = await Account.Wait(cfg.AuthServer, l, o, waiting.Token);
                waiting = null; code.IsVisible = false; openSite.IsVisible = false;
                Show();
                detail.Text = $"Linked. You play online as {name}" + (GameRunner.Running() ? " from the next start of the game." : ".");
            }
            catch (Exception e)
            {
                waiting = null; code.IsVisible = false; openSite.IsVisible = false;
                Show();
                detail.Text = e is OperationCanceledException ? "Cancelled." : e.Message;
            }
        });
        cancel = Ui.Ghost("Cancel", () => waiting?.Cancel());
        openSite = Ui.Ghost("Open the website again", () => { if (openSite.Tag is string u) OpenUrl(u); });
        openSite.IsVisible = false;
        bool armed = false;
        signOut = Ui.Ghost("Sign out", async () =>
        {
            if (!armed) { armed = true; signOut.Content = "Click again to sign out"; return; }
            armed = false; signOut.Content = "Sign out";
            await Account.SignOut(cfg.AuthServer, o);
            Show();
        });
        Show();
        var account = Ui.Glass(Ui.Col(12, Ui.Eyebrow("Account"), Ui.Well(Ui.Col(6, status, detail, code)), Ui.Row(8, link, openSite, cancel, signOut)), 18);
        Ui.Tip(account, "Your account on the Hunter's Dream server, the same one the in-game menu (F10, Online) links. Linking shows a code; approve it on the website signed in with Discord, and this PC gets a sign-in of its own (no password is stored). Each player profile has its own account. Sign out tells the server to forget this PC.");

        // The server: which one, and whether it answers.
        var ping = Ui.Text("checking…", 12, "muted");
        _ = ReadyCheck.PingServer(cfg.OnlineHost).ContinueWith(t => Dispatcher.UIThread.Post(() =>
            ping.Text = t.Result is int ms ? $"Answering · {ms} ms" : "Not answering right now"));
        bool live = cfg.OnlineHost == "thehuntersdream.com";
        var server = Ui.Glass(Ui.Col(8, Ui.Eyebrow("Server"),
            Ui.Row2(live ? "The Hunter's Dream (live)" : cfg.OnlineHost, live ? "thehuntersdream.com · co-op, invasions, messages, bloodstains" : "A server set in HeliasDream.toml", ping)), 18);

        // The community: the server's own numbers, and where to find people.
        var nums = new UniformGrid { Columns = 3 };
        void Stat(string k, string v)
        {
            var w = Ui.Well(Ui.Col(4, Ui.Text(k, 12, "muted"), Ui.Text(v, 17, null, FontWeight.SemiBold)));
            w.Margin = new Thickness(0, 0, 10, 10);
            nums.Children.Add(w);
        }
        var statsNote = Ui.Text("Loading the server's numbers…", 12, "muted");
        if (live)
            _ = CommunityStats.Fetch(cfg.OnlineHost).ContinueWith(t => Dispatcher.UIThread.Post(() =>
            {
                var c = t.Result;
                if (c == null) { statsNote.Text = "The server's numbers are not available right now."; return; }
                statsNote.Text = "From thehuntersdream.com, live.";
                Stat("Co-op signs down", c.CoopSigns.ToString("N0"));
                Stat("Invaders waiting", c.InvadeSigns.ToString("N0"));
                Stat("Hunters", c.Hunters.ToString("N0"));
                Stat("Summons", c.Summons.ToString("N0"));
                Stat("Messages", c.Messages.ToString("N0"));
                Stat("Bosses felled", c.BossClears.ToString("N0"));
            }));
        else statsNote.Text = "Numbers are shown for the live server.";
        var links = new WrapPanel { ItemSpacing = 8, LineSpacing = 8 };
        foreach (var b in new[] {
            Ui.Ghost("Website", () => OpenUrl(Website)),
            Ui.Ghost("Your account", () => OpenUrl(Website + "/account")),
            Ui.Ghost("Messages map", () => OpenUrl(Website + "/map")),
            Ui.Ghost("Leaderboards", () => OpenUrl(Website + "/leaderboards")),
            Ui.Ghost("Discord", () => OpenUrl(DiscordInvite)) }) links.Children.Add(b);
        var community = Ui.Glass(Ui.Col(10, Ui.Eyebrow("The Hunter's Dream"),
            Ui.Text("The community server for Bloodborne on PC: summon and be summoned, invade, leave and rate messages, see bloodstains and ghosts of other hunters. Its website has the map of every message, the leaderboards and your own hunter's page; its Discord is where players meet up and the server's news comes first.", 13, "soft"),
            nums, statsNote, links), 18);

        // Discord itself: what linking does, step by step.
        var steps = Ui.Glass(Ui.Col(8, Ui.Eyebrow("Setting up with Discord"),
            StepRow("1", "Press Link with Discord", "Your browser opens the website's link page with a code."),
            StepRow("2", "Sign in with Discord", "On thehuntersdream.com: only your Discord name is used, to make your hunter's name."),
            StepRow("3", "Approve the code", "The same code as here. This PC is then signed in; nothing else to type."),
            StepRow("4", "Join the Discord", "Optional: discord.gg/ZTrJ28aX7K, for co-op partners and the server's news.")), 18);

        var col = Ui.Col(18, Header("Online", "Co-op, invasions and messages on The Hunter's Dream community server."));
        col.Children[0].Margin = new Thickness(8, 6, 8, 0);
        col.Children.Add(Columns(Ui.Col(18, account, server, steps), community));
        return col;
    }

    static Control StepRow(string n, string title, string help)
    {
        var dot = new Border { Width = 28, Height = 28, CornerRadius = new CornerRadius(14), Background = Brush.Parse("#2AC9D6EE"), Child = Ui.Text(n, 13, null, FontWeight.Bold) };
        ((TextBlock)dot.Child).HorizontalAlignment = HorizontalAlignment.Center;
        ((TextBlock)dot.Child).VerticalAlignment = VerticalAlignment.Center;
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*"), ColumnSpacing = 14 };
        g.Children.Add(dot);
        var t = Ui.Col(2, Ui.Text(title, 14, null, FontWeight.Medium), Ui.Text(help, 12, "muted"));
        Grid.SetColumn(t, 1); g.Children.Add(t);
        return Ui.Well(g);
    }

    // FSR 4's assets (Core/Fsr4Assets.cs): there or not, and the download, its lines on the
    // Console page.
    static Control Fsr4V07Note(Ctx ctx, bool compact)
    {
        bool have = Fsr4Assets.Present;
        var note = Ui.Text(have ? "FSR 4's shaders are downloaded." + (compact ? "" : $" ({Fsr4Assets.Dir})")
                                : "FSR 4 needs its shaders downloaded once (about 33 MB, MIT-licensed, from FireBurn's Q2RTX). Until then it runs as FSR 3.1.", 12, have ? "muted" : "soft");
        var go = Ui.Ghost(have ? "Check again" : "Download", () =>
        {
            ctx.Console.RunTask("Downloading FSR 4", (say, ct) => Fsr4Assets.Download(say, ct),
                ok => ctx.Console.Add(ok ? "FSR 4 is ready from the next start of the game: choose it as the upscaler." : "The download did not finish; try again (it resumes)."));
            ctx.GoTo(ConsolePage);
        });
        Ui.Tip(go, "Downloads the FSR 4 INT8 model's shaders and weights (AMD's MIT-licensed FSR 4 source, built to SPIR-V by FireBurn's Q2RTX port) into " + Fsr4Assets.Dir + ". The game picks them up when it next starts. Runs on any GPU with INT8 dot products: Radeon RX 6000 or newer, GeForce RTX 20 or newer, Intel Arc.");
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 14 };
        g.Children.Add(note);
        go.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(go, 1); g.Children.Add(go);
        return compact ? Ui.Well(g) : g;
    }

    // FSR 4.1.1's assets: whether they are built, what the GPU makes of it, and the build
    // itself (build-fsr4.sh from the release folder on the player's own AMD DLL, its output
    // on the Console page).
    static Control Fsr4Note(Ctx ctx, bool compact)
    {
        bool built = Directory.Exists(Paths.Fsr4Assets) && Directory.EnumerateFileSystemEntries(Paths.Fsr4Assets).Any();
        var (w, h) = ctx.ScreenSize();
        var hw = HardwareInfo.Detect(w, h);
        var gpu = hw.Rdna4 ? "" : " On this GPU it may run slower than FSR 3.1, or fall back to it where the GPU lacks what its passes need; it is at its best on a Radeon RX 9000 (RDNA 4).";
        var text = built
            ? "FSR 4.1.1's assets are built." + (compact ? "" : " Build them again after a new AMD DLL.") + gpu
            : "FSR 4.1.1 needs assets built once from your own copy of AMD's upscaler DLL (amd_fidelityfx_upscaler_dx12.dll, 4.1.x, e.g. from OptiScaler's release). Until then it runs as FSR 3.1." + gpu;
        var note = Ui.Text(text, 12, built ? "muted" : "soft");
        var dir = ctx.GameDir();
        var script = dir == null ? null : System.IO.Path.Combine(dir, "build-fsr4.sh");
        var go = Ui.Ghost(built ? "Rebuild…" : "Set up…", async () =>
        {
            if (script == null || !File.Exists(script)) { note.Text = "build-fsr4.sh is not in the game folder (it comes with the release)."; return; }
            var dll = await ctx.PickFile("Your AMD upscaler DLL (amd_fidelityfx_upscaler_dx12.dll, 4.1.x)", null);
            if (dll == null) return;
            if (!System.IO.Path.GetFileName(dll).EndsWith(".dll", StringComparison.OrdinalIgnoreCase)) { note.Text = "That is not a DLL: choose amd_fidelityfx_upscaler_dx12.dll."; return; }
            ctx.Console.RunTool("Building FSR 4.1.1", "bash", new[] { script, dll }, dir!, code =>
                ctx.Console.Add(code == 0 ? "FSR 4.1.1 is ready: choose it as the upscaler (Play or Upscaling)." : "The build did not finish; the lines above say why (README-TESTING.txt lists what it needs)."));
            ctx.GoTo(ConsolePage);
        });
        Ui.Tip(go, "Choose your own amd_fidelityfx_upscaler_dx12.dll (4.1.x): build-fsr4.sh turns AMD's passes and weights into Vulkan shaders in " + Paths.Fsr4Assets + ", once. It needs Proton (GE-Proton 10+, Proton-CachyOS or Experimental), python3 and the SPIR-V tools; the Console page shows its progress. Nothing from AMD ships with Helia's Dream.");
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 14 };
        g.Children.Add(note);
        go.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(go, 1); g.Children.Add(go);
        return compact ? Ui.Well(g) : g;
    }

    static Control More(Ctx ctx)
    {
        var s = ctx.Settings;
        var dirText = Ui.Text(ctx.GameDir() ?? "Not found", 12, "muted");
        var choose = Ui.Ghost("Choose…", async () =>
        {
            var d = await ctx.PickFolder(ctx.GameDir());
            if (d == null) return;
            if (Paths.EngineIn(d) == null) { dirText.Text = "No Helia's Dream engine in that folder"; return; }
            s.GameDir = d; s.Save(); dirText.Text = d; ctx.Changed();
        });
        var folderRow = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
        folderRow.Children.Add(Ui.Col(2, Ui.Text("Game folder", 14, null, FontWeight.Medium), dirText));
        Ui.Tip(folderRow, "The Helia's Dream folder, with the engine (HeliasDream.bin) in it. Found by itself when the launcher sits in it or next to it.");
        Grid.SetColumn(choose, 1); folderRow.Children.Add(choose);

        Control Toggle(string label, string help, bool value, Action<bool> set, string? tip = null)
        {
            var sw = new ToggleSwitch { IsChecked = value };
            sw.IsCheckedChanged += (_, _) => { set(sw.IsChecked == true); s.Save(); ctx.Changed(); };
            var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
            g.Children.Add(Ui.Col(2, Ui.Text(label, 14, null, FontWeight.Medium), Ui.Text(help, 12, "muted")));
            Grid.SetColumn(sw, 1); g.Children.Add(sw);
            return Ui.Tip(Ui.Well(g), tip ?? help);
        }

        var launcher = Ui.Glass(Ui.Col(8, Ui.Eyebrow("Launcher"), Ui.Well(folderRow),
            Toggle("Reduce motion", "No parallax or sliding pages.", s.ReduceMotion, v => s.ReduceMotion = v,
                "Turns off the background's parallax and the pages' slide and fade, for anyone who finds motion uncomfortable. Pages then switch at once."),
            Toggle("Game art background", "Your game's key art, blurred behind the glass (read from your dump).", s.GameArtBackground, v => s.GameArtBackground = v,
                "Shows the key art from your own game files (sce_sys/pic1.png), blurred and darkened behind the glass panes. Nothing of the game ships with the launcher. Applies the next time the launcher opens."),
            Toggle("Close when the game starts", "Otherwise the launcher waits and comes back when you quit.", s.CloseOnPlay, v => s.CloseOnPlay = v,
                "Off: the launcher minimizes while you play, comes back when the game quits, and shows any trophies you earned. On: it closes as the game starts.")), 18);
        launcher.VerticalAlignment = VerticalAlignment.Top;

        var col = Ui.Col(18, Header("More", "Game options, debug switches and the launcher itself."));
        col.Children[0].Margin = new Thickness(8, 6, 8, 0);
        var g2 = new Grid { ColumnDefinitions = new ColumnDefinitions("*,*"), ColumnSpacing = 18 };
        var left = Ui.Col(18, OptionSchema.More.Select(gr => Ui.Group(gr, ctx.Options, ctx.Changed)).ToArray());
        var exp = (Border)Ui.Group(OptionSchema.Experimental, ctx.Options, ctx.Changed);
        var warn = Ui.Text("Experimental: not measured enough to be on by default. Off is the safe choice; turn a switch off again if the game stutters or flashes.", 12);
        warn.Foreground = Brush.Parse("#E8C27A"); warn.TextWrapping = TextWrapping.Wrap; warn.Margin = new Thickness(4, 0, 4, 4);
        ((StackPanel)exp.Child!).Children.Insert(1, warn);
        left.Children.Add(exp);
        g2.Children.Add(left);
        var right = Ui.Col(18, launcher, SavesCard(ctx));
        right.VerticalAlignment = VerticalAlignment.Top;
        Grid.SetColumn(right, 1); g2.Children.Add(right);
        col.Children.Add(g2);
        return col;
    }

    // ---- Game setup (the footer's Settings): the game's files, the DLC, saves, Steam ----

    static Control PathRow(string label, string? value, string empty, string tip, params Control[] buttons)
    {
        var text = Ui.Text(string.IsNullOrEmpty(value) ? empty : value, 12, "muted");
        text.TextWrapping = TextWrapping.Wrap;
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 14 };
        g.Children.Add(Ui.Col(3, Ui.Text(label, 14, null, FontWeight.Medium), text));
        var row = Ui.Row(8, buttons);
        row.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(row, 1); g.Children.Add(row);
        return Ui.Tip(Ui.Well(g), tip);
    }

    static Control Setup(Ctx ctx)
    {
        var toml = new OptionsFile(Paths.HeliasToml);
        var cfg = ctx.Config();
        var status = Ui.Text("", 12, "soft");
        void Saved(string what) { status.Text = what; }

        string? app0 = toml.GetIn("paths", "app0")?.TrimEnd('/', '\\');
        bool dump = app0 != null && File.Exists(System.IO.Path.Combine(app0, "eboot.bin"));
        string? update = app0 == null ? null : new[] { app0 + "-patch", app0 + "-UPDATE" }.FirstOrDefault(Directory.Exists);
        var files = Ui.Col(8, Ui.Eyebrow("Game files"),
            PathRow("Game folder", app0, "Not set", "The folder of your own Bloodborne dump (CUSA03173): the one with eboot.bin and sce_sys in it.",
                Ui.Ghost("Choose…", async () =>
                {
                    var d = await ctx.PickFolder(app0);
                    if (d == null) return;
                    if (!File.Exists(System.IO.Path.Combine(d, "eboot.bin"))) { Saved("No eboot.bin in that folder: choose the dump's own folder (CUSA03173)."); return; }
                    toml.SetIn("paths", "app0", d);
                    var elf = System.IO.Path.Combine(d, "eboot.elf");
                    if (File.Exists(elf)) toml.SetIn("paths", "eboot", elf);
                    ctx.GoTo(SetupPage);
                })),
            PathRow("Update 1.09", update, dump ? "No update folder beside the game (CUSA03173-patch)" : "—",
                "The 1.09 update is read from a folder beside the game's, named like it with -patch or -UPDATE (CUSA03173-patch), as shadPS4 keeps it. Nothing to set: it is found by itself."),
            PathRow("Decrypted eboot", toml.GetIn("paths", "eboot"), "Not set",
                "The 1.09 update's eboot, decrypted (eboot.elf). The game cannot start without it.",
                Ui.Ghost("Choose…", async () =>
                {
                    var f = await ctx.PickFile("The decrypted eboot (eboot.elf)", app0);
                    if (f == null) return;
                    toml.SetIn("paths", "eboot", f);
                    ctx.GoTo(SetupPage);
                })),
            PathRow("Mods folder", toml.GetIn("paths", "mods"), "None (the default, <saves>/mods)",
                "A folder of replaced game files (its own dvdroot_ps4): they load ahead of the dump's, which is never written to.",
                Ui.Ghost("Choose…", async () =>
                {
                    var d = await ctx.PickFolder(app0 != null ? System.IO.Path.GetDirectoryName(app0) : null);
                    if (d == null) return;
                    toml.SetIn("paths", "mods", d);
                    ctx.GoTo(SetupPage);
                }),
                Ui.Ghost("Clear", () => { toml.Remove("paths", "mods"); ctx.GoTo(SetupPage); })),
            PathRow("Save data folder", cfg.DataDir, "Default",
                "Where saves, trophies and the game's own data live. Each player profile has its own (Play page).",
                Ui.Ghost("Choose…", async () =>
                {
                    if (ctx.Settings.Profile != Profiles.Default) { Saved("A profile keeps its own save folder; switch to Default to move the shared one."); return; }
                    var d = await ctx.PickFolder(cfg.DataDir);
                    if (d == null) return;
                    toml.SetIn("paths", "data", d);
                    ctx.GoTo(SetupPage);
                })));

        var ohOn = (toml.GetIn("patches", "old-hunters") ?? "true") != "false";
        var oh = new ToggleSwitch { IsChecked = ohOn, VerticalAlignment = VerticalAlignment.Center };
        oh.IsCheckedChanged += (_, _) =>
        {
            if (oh.IsChecked == true) toml.Remove("patches", "old-hunters");
            else toml.SetRawIn("patches", "old-hunters", "false");
            Saved(oh.IsChecked == true ? "The Old Hunters: on at the next start." : "The Old Hunters: off at the next start.");
        };
        var dlc = Ui.Col(8, Ui.Eyebrow("DLC: The Old Hunters"),
            Ui.Tip(Ui.Row2("The Old Hunters", "Unlocked by a patch: no DLC folder or download needed.", oh),
                "The 1.09 dump already carries The Old Hunters' content; a patch makes the game the Old Hunters edition (Kyo's \"bb 00900\"), as the Game of the Year release is. There is no separate DLC package to point at. Off is the base game alone. Applies at the next start."));

        // Add to Steam
        var art = ctx.Settings.SteamArt ?? System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "BloodBorne PC", "steam-art");
        var steamStatus = Ui.Text(Steam.Running() ? "Steam is running: close it first." : "Adds the launcher to Steam as Bloodborne, with your art.", 12, "muted");
        var preview = new Grid { Height = 120 };
        var portrait = System.IO.Path.Combine(art, "bbp.png");
        if (File.Exists(portrait))
        {
            try
            {
                preview.Children.Add(new Border
                {
                    Width = 80, Height = 120, CornerRadius = new CornerRadius(10), ClipToBounds = true, HorizontalAlignment = HorizontalAlignment.Left,
                    Child = new Image { Source = new Bitmap(portrait), Stretch = Stretch.UniformToFill },
                });
                var wide = System.IO.Path.Combine(art, "bb.png");
                if (File.Exists(wide))
                    preview.Children.Add(new Border
                    {
                        Width = 250, Height = 117, CornerRadius = new CornerRadius(10), ClipToBounds = true, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(92, 0, 0, 0),
                        Child = new Image { Source = new Bitmap(wide), Stretch = Stretch.UniformToFill },
                    });
            }
            catch (Exception) { }
        }
        var steam = Ui.Col(8, Ui.Eyebrow("Add to Steam"), preview,
            PathRow("Art", art, "Not set", "The pictures Steam shows: bbp.png (the library's portrait), bb.png (wide), bb_hero.jpg (the banner), bb_logo.png and bb_icon.png. Missing ones are left out.",
                Ui.Ghost("Choose…", async () =>
                {
                    var d = await ctx.PickFolder(art);
                    if (d == null) return;
                    ctx.Settings.SteamArt = d; ctx.Settings.Save();
                    ctx.GoTo(SetupPage);
                })),
            Ui.Tip(Ui.Row2("Add to Steam", "", Ui.Ghost("Add", () =>
            {
                var exe = Environment.ProcessPath ?? "";
                var (ok, msg) = Steam.Add(exe, System.IO.Path.GetDirectoryName(exe) ?? "", Directory.Exists(art) ? art : null);
                steamStatus.Text = msg;
            })), "Adds this launcher to every Steam account on this PC as \"Bloodborne\" (or updates the one there), with the art above. Steam must be closed: it rewrites its shortcut list when it quits. For the DualSense's HD haptics and adaptive triggers, turn Steam Input off for it (its controller settings)."),
            steamStatus);

        // Import a save
        var importStatus = Ui.Text($"Into the profile \"{ctx.Settings.Profile}\". The save it replaces is backed up first.", 12, "muted");
        var import = Ui.Col(8, Ui.Eyebrow("Import a save"),
            Ui.Tip(Ui.Row2("Import from a folder", "A PS4 save (Apollo Save Tool), shadPS4's, Lantern Engine's, another PC's.",
                Ui.Ghost("Choose…", async () =>
                {
                    var d = await ctx.PickFolder(null);
                    if (d == null) return;
                    new Saves(ctx.Config().DataDir).Import(d, out var msg);
                    importStatus.Text = msg;
                })),
                "Choose the save's folder, or any folder above it: the one with userdata0000 in it is found (SPRJ0005 first). Its save files replace this profile's; the save they replace is backed up first (More > Saves can put it back). The game must be closed. A PS4 save has to be decrypted (Apollo Save Tool on a jailbroken PS4 exports it so)."),
            importStatus);

        var page = Ui.Col(18, Header("Game setup", "Your game's files, the DLC, saves and Steam. Shared by every profile unless it says otherwise."));
        page.Children[0].Margin = new Thickness(8, 6, 8, 0);
        status.Margin = new Thickness(8, 0, 8, 0);
        page.Children.Add(status);
        page.Children.Add(Columns(Ui.Col(18, Ui.Glass(files, 18), Ui.Glass(dlc, 18)), Ui.Col(18, Ui.Glass(steam, 18), Ui.Glass(import, 18))));
        return page;
    }

    static Control Graphics(Ctx ctx)
    {
        var page = (StackPanel)Settings("Graphics", "Effects and the look of the world. All apply live in the game too.", OptionSchema.Graphics, ctx);
        page.Children.Insert(1, BestCard(ctx));
        return page;
    }

    static Control Controls(Ctx ctx)
    {
        var page = (StackPanel)Settings("Controls", "Any controller: PlayStation, Xbox or another. Haptics, icons, mouse and keyboard.", OptionSchema.Controller, ctx);
        page.Children.Insert(1, PadsCard(ctx));
        page.Children.Add(BindingsCard(ctx));
        return page;
    }

    // The controllers plugged in, which one the game listens to, and what each can do.
    static Control PadsCard(Ctx ctx)
    {
        var st = ctx.Settings;
        var list = Ui.Col(6);
        var choice = new ComboBox { MinWidth = 260, VerticalAlignment = VerticalAlignment.Center };
        var hint = Ui.Text("", 12, "muted");
        List<Controllers.Pad> pads = new();
        string seen = "";
        bool filling = false;
        void Fill()
        {
            pads = Controllers.List();
            var key = string.Join("|", pads.Select(x => x.Name + x.Usb));
            if (key == seen) return;
            seen = key;
            list.Children.Clear();
            if (pads.Count == 0)
                list.Children.Add(CheckRow(new CheckItem("No controller connected", "Plug one in by USB, or pair it by Bluetooth. The keyboard and mouse always work.", CheckState.Note)));
            foreach (var x in pads)
            {
                string what = x.Kind switch
                {
                    Controllers.Kind.Steam => "Steam Input's own pad: Steam's layout drives the game",
                    _ when x.DualSense && x.Usb => "USB · HD haptics and adaptive triggers ready",
                    _ when x.DualSense => "Bluetooth · rumble and adaptive triggers; HD haptics need USB",
                    Controllers.Kind.PlayStation => (x.Usb ? "USB" : "Bluetooth") + " · rumble",
                    Controllers.Kind.Xbox => (x.Usb ? "USB" : "Wireless") + " · rumble · Xbox icons on Auto",
                    _ => (x.Usb ? "USB" : "Wireless") + " · rumble where it has it",
                };
                list.Children.Add(CheckRow(new CheckItem(x.Short, what, CheckState.Ok)));
            }
            filling = true;
            var items = new List<string> { "Automatic: the one you use" };
            items.AddRange(pads.Where(x => x.Kind != Controllers.Kind.Steam).Select(x => x.Short));
            if (st.Gamepad != "auto" && !items.Contains(st.Gamepad)) items.Add(st.Gamepad);
            choice.ItemsSource = items;
            choice.SelectedItem = st.Gamepad == "auto" ? items[0] : st.Gamepad;
            filling = false;
            bool steam = pads.Any(x => x.Kind == Controllers.Kind.Steam);
            bool hd = pads.Any(x => x.DualSense && x.Usb);
            hint.Text = (steam ? "Steam Input is on for the game: for HD haptics and the adaptive triggers, turn it off for Bloodborne in Steam (its controller settings). " : "") +
                        (hd ? "" : "HD haptics need an HD-haptics controller: a DualSense or DualSense Edge plugged in by USB. Other controllers rumble.");
        }
        choice.SelectionChanged += (_, _) =>
        {
            if (filling || choice.SelectedItem is not string s) return;
            st.Gamepad = choice.SelectedIndex <= 0 ? "auto" : s;
            st.Save();
        };
        Fill();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += (_, _) => Fill();
        list.AttachedToVisualTree += (_, _) => timer.Start();
        list.DetachedFromVisualTree += (_, _) => timer.Stop();
        var use = Ui.Tip(Ui.Row2("Controller the game uses", "Automatic takes input from every controller, the one you press switching the icons.", choice),
            "Automatic: every controller drives the game and the icons follow the one you last pressed; with Steam Input on, Steam's own pad stands in for the controllers Steam handles, so nothing is pressed twice. Choose one to have the game listen to it alone while it is connected (another plugged in for good, like a wheel or a second pad, is then left alone). Applies when the game starts.");
        return Ui.Glass(Ui.Col(10, Ui.Eyebrow("Controllers"), list, use, hint), 18);
    }

    // Keyboard and mouse bindings (Core/Bindings.cs): per action a key and a mouse input.
    static Control BindingsCard(Ctx ctx)
    {
        var o = ctx.Options;
        var rowsLeft = Ui.Col(6);
        var rowsRight = Ui.Col(6);
        var all = Bindings.All;
        var rebuild = new List<Action>();
        Button Pill(string text, bool empty)
        {
            var b = new Button
            {
                Content = text, Classes = { "flat", "ghost" }, Height = 34, MinWidth = 110, Padding = new Thickness(12, 0), FontSize = 13,
                Opacity = empty ? 0.55 : 1.0,
            };
            return b;
        }
        for (int i = 0; i < all.Length; i++)
        {
            var a = all[i];
            var keyBtn = Pill("", true);
            var mouseBtn = Pill("", true);
            void Show()
            {
                var (k, m) = Bindings.Read(o, a);
                keyBtn.Content = string.IsNullOrEmpty(k) ? "No key" : k;
                keyBtn.Opacity = string.IsNullOrEmpty(k) ? 0.55 : 1.0;
                mouseBtn.Content = m > 0 ? Bindings.MouseLabels[m] : "No mouse";
                mouseBtn.Opacity = m > 0 ? 1.0 : 0.55;
            }
            Show();
            rebuild.Add(Show);
            keyBtn.Click += (_, _) =>
            {
                keyBtn.Content = "Press a key…";
                ctx.Capture(false, (name, _) =>
                {
                    if (name != null) { var (_, m) = Bindings.Read(o, a); Bindings.Write(o, a, name, m); }
                    Show();
                });
            };
            mouseBtn.Click += (_, _) =>
            {
                mouseBtn.Content = "Click a button…";
                ctx.Capture(true, (_, mouse) =>
                {
                    if (mouse > 0) { var (k, _) = Bindings.Read(o, a); Bindings.Write(o, a, k, mouse); }
                    Show();
                });
            };
            // Right-click clears that half.
            keyBtn.PointerReleased += (_, e) => { if (e.InitialPressMouseButton == Avalonia.Input.MouseButton.Right) { var (_, m) = Bindings.Read(o, a); Bindings.Write(o, a, null, m); Show(); } };
            mouseBtn.PointerReleased += (_, e) => { if (e.InitialPressMouseButton == Avalonia.Input.MouseButton.Right) { var (k, _) = Bindings.Read(o, a); Bindings.Write(o, a, k, 0); Show(); } };
            var controls = Ui.Row(6, keyBtn, mouseBtn);
            var row = Ui.Tip(Ui.Row2(a.Label, a.Help, controls),
                "Click the key, then press the one you want; click the mouse half, then click a mouse button or turn the wheel. Esc cancels; right-click clears.");
            (i < (all.Length + 1) / 2 ? rowsLeft : rowsRight).Children.Add(row);
        }
        var reset = Ui.Ghost("Reset all", () =>
        {
            foreach (var a in all) Bindings.Write(o, a, a.DefaultKey, a.DefaultMouse);
            foreach (var r in rebuild) r();
        });
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        head.Children.Add(Ui.Col(2, Ui.Eyebrow("Keyboard and mouse"), Ui.Text("Each action takes a key and a mouse input. The game reads them when it starts.", 12, "muted")));
        reset.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(reset, 1); head.Children.Add(reset);
        return Ui.Glass(Ui.Col(12, head, Columns(rowsLeft, rowsRight)), 18);
    }

    // The save and its backups (Core/Saves.cs): the engine's automatic copies, one made on
    // request, and one put back (the save it replaces backed up first).
    static Control SavesCard(Ctx ctx)
    {
        var saves = new Saves(ctx.Config().DataDir);
        var status = Ui.Text("", 12, "muted");
        var list = Ui.Col(6);
        Button? armed = null;
        void Fill()
        {
            list.Children.Clear();
            var all = saves.List();
            status.Text = saves.HasSave ? $"{all.Count} backups · newest {(all.Count > 0 ? all[0].When.ToString("MMM d, HH:mm") : "none yet")}" : "No save yet.";
            foreach (var b in all.Take(6))
            {
                var restore = new Button { Content = "Restore", Classes = { "flat", "ghost" }, Height = 34, Padding = new Thickness(14, 0), FontSize = 13 };
                restore.Click += (_, _) =>
                {
                    // Two clicks: the first arms it, the second restores.
                    if (!ReferenceEquals(armed, restore))
                    {
                        if (armed != null) armed.Content = "Restore";
                        armed = restore;
                        restore.Content = "Click again to restore";
                        return;
                    }
                    armed = null;
                    saves.Restore(b, out var msg);
                    Fill();
                    status.Text = msg;
                };
                list.Children.Add(Ui.Row2(b.When.ToString("ddd MMM d, HH:mm"), b.Bytes >= 1L << 20 ? $"{b.Bytes / (1L << 20)} MB" : $"{b.Bytes / 1024} KB", restore));
            }
        }
        Fill();
        var now = Ui.Ghost("Back up now", () =>
        {
            var d = saves.BackUpNow();
            Fill();
            if (d == null) status.Text = saves.HasSave ? "The backup failed." : "No save to back up yet.";
        });
        var open = Ui.Ghost("Open folder", () =>
        {
            try
            {
                var dir = Directory.Exists(saves.BackupRoot) ? saves.BackupRoot : saves.SaveDir;
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(OperatingSystem.IsWindows() ? "explorer" : "xdg-open", dir) { UseShellExecute = false });
            }
            catch (Exception) { }
        });
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        head.Children.Add(Ui.Col(2, Ui.Eyebrow("Saves"), status));
        var buttons = Ui.Row(8, now, open);
        Grid.SetColumn(buttons, 1); head.Children.Add(buttons);
        return Ui.Tip(Ui.Glass(Ui.Col(10, head, list), 18),
            "The game backs up your save by itself as it writes it (at most every 5 minutes, the newest 8 kept). Restore puts one back as your save; the save it replaces is backed up first, so nothing is lost. The game must be closed.");
    }

    // Recommended settings for this PC (Core/Saves.cs, HardwareInfo).
    static Control BestCard(Ctx ctx)
    {
        var (w, h) = ctx.ScreenSize();
        var hw = HardwareInfo.Detect(w, h);
        var (settings, why) = hw.Recommend();
        var detail = Ui.Text(why + " " + string.Join(", ", settings.Select(x => $"{x.Key.Replace('_', ' ')} {x.Value}")), 12, "muted");
        var apply = Ui.Ghost("Apply", () =>
        {
            foreach (var (k, v) in settings) ctx.Options.Set(k, v);
            detail.Text = "Applied. " + why;
            ctx.Changed();
        });
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 14 };
        head.Children.Add(Ui.Col(2, Ui.Eyebrow("Best settings for this PC"), Ui.Text(hw.Summary, 13, "soft")));
        apply.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(apply, 1); head.Children.Add(apply);
        return Ui.Tip(Ui.Glass(Ui.Col(10, head, detail), 18),
            "Picks a resolution, upscaler, quality and frame rate from your GPU and screen. Apply writes them to your settings; you can change any of them afterwards.");
    }

    // ---- Trophies ----

    static Control TrophiesPage(Ctx ctx)
    {
        var cfg = ctx.Config();
        var all = Trophies.Load(cfg.DataDir);
        var icons = TrophyPackage.ReadIcons(cfg.App0);
        int earned = all.Count(t => t.Unlocked);
        double frac = (double)earned / all.Count;

        var ring = new Grid { Width = 104, Height = 104 };
        ring.Children.Add(new Ellipse { Stroke = Brush.Parse("#59000000"), StrokeThickness = 9, Margin = new Thickness(4.5) });
        ring.Children.Add(new Arc { Stroke = Brush.Parse("#C9D6EE"), StrokeThickness = 9, StrokeLineCap = PenLineCap.Round, StartAngle = -90, SweepAngle = 360 * frac, Margin = new Thickness(4.5) });
        var center = Ui.Col(0, Ui.Text($"{(int)Math.Round(frac * 100)}%", 32), Ui.Text($"{earned} of {all.Count}", 11, "muted"));
        ((TextBlock)center.Children[0]).Classes.Add("display");
        foreach (var c in center.Children) c.HorizontalAlignment = HorizontalAlignment.Center;
        center.VerticalAlignment = VerticalAlignment.Center;
        ring.Children.Add(center);

        var last = all.Where(t => t.Unlocked).OrderByDescending(t => t.Earned).FirstOrDefault();
        var needPlat = all.Count(t => !t.Trophy.Dlc && t.Trophy.Id != Trophies.PlatinumId && !t.Unlocked);
        var tiers = Ui.Row(10, new[] { Grade.Platinum, Grade.Gold, Grade.Silver, Grade.Bronze }.Select(gr =>
        {
            var of = all.Where(t => t.Trophy.Grade == gr).ToList();
            var c = Ui.Col(6, new Ellipse { Width = 28, Height = 28, Fill = Ui.Medal(gr) }, Ui.Text($"{of.Count(t => t.Unlocked)} / {of.Count}", 15, null, FontWeight.Bold), Ui.Text(gr.ToString(), 12, "muted"));
            foreach (var x in c.Children) x.HorizontalAlignment = HorizontalAlignment.Center;
            return (Control)new Border { Classes = { "well" }, Width = 96, CornerRadius = new CornerRadius(16), Child = c };
        }).ToArray());
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 26 };
        head.Children.Add(ring);
        var words = Ui.Col(6, Ui.Title("Trophies", 40),
            Ui.Text((last != null ? $"Last earned {last.Earned:MMM d}" : "None earned yet") + (needPlat > 0 ? $" · {needPlat} left until the platinum" : " · Platinum earned"), 14, "soft"));
        words.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(words, 1); head.Children.Add(words);
        tiers.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(tiers, 2); head.Children.Add(tiers);

        var grid = new UniformGrid { Columns = 3 };
        void Fill(string filter)
        {
            grid.Children.Clear();
            IEnumerable<TrophyState> shown = all;
            if (filter.StartsWith("Earned")) shown = all.Where(t => t.Unlocked).OrderByDescending(t => t.Earned);
            else if (filter.StartsWith("Locked")) shown = all.Where(t => !t.Unlocked);
            else shown = all.OrderBy(t => t.Unlocked ? 0 : 1).ThenByDescending(t => t.Earned).ThenBy(t => t.Trophy.Id);
            foreach (var t in shown) grid.Children.Add(TrophyCard(t, icons));
        }
        var filters = new[] { $"All {all.Count}", $"Earned {earned}", $"Locked {all.Count - earned}" };
        var bar = Ui.Segmented(filters, filters[0], Fill);
        bar.HorizontalAlignment = HorizontalAlignment.Left;
        Fill(filters[0]);

        return Ui.Col(16, Ui.Glass(head, 22), bar, grid);
    }

    static Control TrophyCard(TrophyState t, Dictionary<int, byte[]> icons)
    {
        Control pic;
        if (icons.TryGetValue(t.Trophy.Id, out var png))
        {
            using var ms = new MemoryStream(png);
            pic = new Image { Source = new Bitmap(ms), Stretch = Stretch.UniformToFill };
        }
        else pic = new Border { Background = Brush.Parse("#59000000") };
        var picFrame = new Grid { Width = 60, Height = 60 };
        picFrame.Children.Add(new Border { CornerRadius = new CornerRadius(14), ClipToBounds = true, Child = pic, BorderBrush = Brush.Parse("#1FFFFFFF"), BorderThickness = new Thickness(1) });
        picFrame.Children.Add(new Ellipse
        {
            Width = 20, Height = 20, Fill = Ui.Medal(t.Trophy.Grade), Stroke = Brush.Parse("#11141C"), StrokeThickness = 2,
            HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, -5, -5),
        });

        var when = t.Unlocked ? $"Earned {t.Earned:MMM d, yyyy}" : "Locked";
        var text = Ui.Col(3, Ui.Text(t.Trophy.Name, 14, null, FontWeight.SemiBold), Ui.Text(t.Trophy.Description, 12, "muted"), Ui.Text(when + (t.Trophy.Dlc ? " · The Old Hunters" : ""), 11, "muted"));
        text.VerticalAlignment = VerticalAlignment.Center;
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*"), ColumnSpacing = 14 };
        g.Children.Add(picFrame);
        Grid.SetColumn(text, 1); g.Children.Add(text);
        return new Border
        {
            Classes = { "glass" }, CornerRadius = new CornerRadius(18), Padding = new Thickness(14), Margin = new Thickness(0, 0, 12, 12),
            Background = Brush.Parse(t.Unlocked ? "#16FFFFFF" : "#08FFFFFF"), Opacity = t.Unlocked ? 1 : 0.55, Child = g,
        };
    }
}
