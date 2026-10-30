using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.Automation;
using Avalonia.Controls.Primitives;
using Path = Avalonia.Controls.Shapes.Path;
using HeliasDream.Core;
using HeliasDream.Views;

namespace HeliasDream;

/// <summary>
/// The launcher's one window: the moonlit scene behind everything, the header with the page
/// pills, the page (gliding in on a change), and the footer with Play.
/// </summary>
public sealed class MainWindow : Window
{
    readonly LauncherSettings _settings = LauncherSettings.Load();
    readonly OptionsFile _options;
    readonly Scene _scene;
    readonly TransitioningContentControl _page;
    readonly GlidePage _glide = new();
    readonly TextBlock _summary = Ui.Text("", 13, "soft");
    readonly TextBlock _version = Ui.Text("Launcher 0.1", 12, "muted");
    string? _releaseUrl;
    // A binding being set (the Controller page): the next key, or mouse press / wheel.
    Action<string?, int>? _capture;
    bool _captureMouse;
    readonly Button _play;
    readonly List<Button> _tabs = new();
    readonly Border _bead;
    readonly TranslateTransform _beadMove = new();
    readonly Ctx _ctx;
    readonly ConsoleView _console = new();
    readonly Button _consoleBtn;
    ScrollViewer? _scroll;
    int _current = -1;
    string? _gameDir;

    public MainWindow()
    {
        Title = "Helia's Dream";
        Icon = new WindowIcon(Avalonia.Platform.AssetLoader.Open(new Uri("avares://HeliasDream/Assets/HeliasDream.ico")));
        Width = 1280; Height = 800; MinWidth = 1120; MinHeight = 720;
        // Centred on the primary monitor (KDE's priority 1), not whichever screen
        // the window system lists first.
        WindowStartupLocation = WindowStartupLocation.Manual;
        CenterOnPrimary();
        SystemDecorations = SystemDecorations.None;
        TransparencyLevelHint = new[] { WindowTransparencyLevel.Transparent };
        Background = Brushes.Transparent;
        CanResize = true;

        if (!Profiles.List().Contains(_settings.Profile)) _settings.Profile = Profiles.Default;
        _options = new OptionsFile(Profiles.OptionsFile(_settings.Profile));
        _gameDir = Paths.FindGameDir(_settings.GameDir);
        _ctx = new Ctx
        {
            Options = _options, Settings = _settings,
            Config = () => new HeliasToml(Paths.HeliasToml, Profiles.ConfigFile(_settings.Profile)),
            PickFile = PickFile,
            SwitchProfile = p =>
            {
                _settings.Profile = p;
                _settings.Save();
                _options.Retarget(Profiles.OptionsFile(p));
                int keep = _current;
                _current = -1;
                UpdateSummary();
                GoTo(keep);
            },
            GameDir = () => _gameDir = Paths.FindGameDir(_settings.GameDir),
            GoTo = i => GoTo(i),
            Changed = () =>
            {
                UpdateSummary();
                _scene!.ReduceMotion = _settings.ReduceMotion; _glide.Reduced = _settings.ReduceMotion;
                // Play's ready check and "This hunt" are built from the settings: built again in
                // place (after the click that changed them is done).
                if (_current == Pages.PlayTab) Dispatcher.UIThread.Post(Rebuild);
            },
            Console = _console,
            PickFolder = PickFolder,
            Monitors = Monitors,
            Capture = (mouse, done) => { _captureMouse = mouse; _capture = done; },
            ScreenSize = () =>
            {
                var sc = Screens.Primary ?? Screens.All.FirstOrDefault();
                return sc == null ? (1920, 1080) : (sc.Bounds.Width, sc.Bounds.Height);
            },
        };

        var cfg = new HeliasToml(Paths.HeliasToml);
        _scene = new Scene(_settings.GameArtBackground ? KeyArt(cfg.App0) : null) { ReduceMotion = _settings.ReduceMotion };
        _glide.Reduced = _settings.ReduceMotion;

        // Header
        var logo = new Grid { Width = 34, Height = 34 };
        // The program icon's crescent and star (res/icon/heliasdream.svg).
        var crescent = new CombinedGeometry(GeometryCombineMode.Exclude,
            new EllipseGeometry(new Rect(2, 3.3, 28, 28)), new EllipseGeometry(new Rect(8.8, 1.3, 23.2, 23.2)));
        logo.Children.Add(new Path { Data = crescent, Fill = Brush.Parse("#C9D6EE"), Width = 34, Height = 34, Stretch = Stretch.None });
        logo.Children.Add(new Path { Data = Geometry.Parse("M23.5,10.5 L24.5,14.3 L28.3,15.3 L24.5,16.3 L23.5,20.1 L22.5,16.3 L18.7,15.3 L22.5,14.3 Z"),
                                     Fill = Brush.Parse("#F7F9FD"), Width = 34, Height = 34, Stretch = Stretch.None });
        var name = Ui.Col(3, new TextBlock { Text = "Helia's Dream", FontSize = 28, Classes = { "display" }, LetterSpacing = 1.6 },
                             _version);
        var brand = Ui.Row(12, logo, name);
        brand.VerticalAlignment = VerticalAlignment.Center;

        _bead = new Border { Classes = { "bead" }, HorizontalAlignment = HorizontalAlignment.Left, RenderTransform = _beadMove, IsHitTestVisible = false, Opacity = 0 };
        _beadMove.Transitions = new Transitions { new DoubleTransition { Property = TranslateTransform.XProperty, Duration = TimeSpan.FromMilliseconds(480), Easing = new CubicEaseOut() } };
        _bead.Transitions = new Transitions { new DoubleTransition { Property = Layoutable.WidthProperty, Duration = TimeSpan.FromMilliseconds(480), Easing = new CubicEaseOut() } };
        var tabRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        for (int i = 0; i < Pages.Names.Length; i++)
        {
            int idx = i;
            var b = new Button { Content = Pages.Names[i], Classes = { "flat" }, CornerRadius = new CornerRadius(999), Padding = new Thickness(16, 9), FontSize = 14, FontWeight = FontWeight.SemiBold };
            b.Click += (_, _) => GoTo(idx);
            _tabs.Add(b);
            tabRow.Children.Add(b);
        }
        var tabGrid = new Grid();
        tabGrid.Children.Add(_bead);
        tabGrid.Children.Add(tabRow);
        var tabs = new Border { Classes = { "track", "dark" }, Padding = new Thickness(5), Child = tabGrid, Margin = new Thickness(28, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        tabRow.LayoutUpdated += (_, _) => { if (_bead.Opacity == 0 && _current >= 0) PlaceBead(false); };

        var min = IconButton("M3,8 L13,8", "Minimize", () => WindowState = WindowState.Minimized);
        var max = IconButton("M3.5,3.5 L12.5,3.5 L12.5,12.5 L3.5,12.5 Z", "Maximize", ToggleMaximized);
        PropertyChanged += (_, e) =>
        {
            if (e.Property != WindowStateProperty) return;
            // Maximized: two overlapping squares, the usual "restore" mark.
            ((Path)max.Content!).Data = Geometry.Parse(WindowState == WindowState.Maximized
                ? "M5.5,3.5 L12.5,3.5 L12.5,10.5 M3.5,5.5 L10.5,5.5 L10.5,12.5 L3.5,12.5 Z" : "M3.5,3.5 L12.5,3.5 L12.5,12.5 L3.5,12.5 Z");
            AutomationProperties.SetName(max, WindowState == WindowState.Maximized ? "Restore" : "Maximize");
        };
        var close = IconButton("M4,4 L12,12 M12,4 L4,12", "Close", Close);
        close.Classes.Add("close");
        var winButtons = Ui.Row(8, min, max, close);
        winButtons.VerticalAlignment = VerticalAlignment.Center;

        var header = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,Auto,*,Auto"), Background = Brushes.Transparent };
        header.Children.Add(brand);
        Grid.SetColumn(tabs, 1); header.Children.Add(tabs);
        Grid.SetColumn(winButtons, 3); header.Children.Add(winButtons);
        header.PointerPressed += (_, e) => { if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) BeginMoveDrag(e); };
        header.DoubleTapped += (_, _) => ToggleMaximized();

        // Page
        _page = new TransitioningContentControl { PageTransition = _glide, ClipToBounds = false };

        // Footer
        _play = new Button { Content = new TextBlock { Text = "PLAY", LetterSpacing = 5, Foreground = Brush.Parse("#FFF4F2") }, Classes = { "flat", "play" } };
        _play.Click += (_, _) => Play();
        var footerRow = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto,Auto,Auto,Auto"), ColumnSpacing = 12 };
        _summary.VerticalAlignment = VerticalAlignment.Center;
        footerRow.Children.Add(_summary);
        _consoleBtn = Ui.Ghost("Console", () => GoTo(Pages.ConsolePage));
        _consoleBtn.IsVisible = false;
        Ui.Tip(_consoleBtn, "The game's log, live while it runs (and the FSR 4.1.1 build's). The whole run is also kept in logs/ in the game folder.");
        Grid.SetColumn(_consoleBtn, 1); footerRow.Children.Add(_consoleBtn);
        var setupBtn = Ui.Ghost("Setup", () => GoTo(Pages.SetupPage));
        Ui.Tip(setupBtn, "Game setup: the game's files, the update, mods, The Old Hunters, save import and Add to Steam.");
        Grid.SetColumn(setupBtn, 2); footerRow.Children.Add(setupBtn);
        var quit = Ui.Ghost("Quit", Close);
        Grid.SetColumn(quit, 3); footerRow.Children.Add(quit);
        Grid.SetColumn(_play, 4); footerRow.Children.Add(_play);
        var footer = new Border { Classes = { "dark" }, CornerRadius = new CornerRadius(22), Padding = new Thickness(22, 12, 12, 12), Child = footerRow };

        var layout = new Grid { RowDefinitions = new RowDefinitions("Auto,*,Auto"), RowSpacing = 18, Margin = new Thickness(26, 22) };
        layout.Children.Add(header);
        Grid.SetRow(_page, 1); layout.Children.Add(_page);
        Grid.SetRow(footer, 2); layout.Children.Add(footer);

        var root = new Grid();
        root.Children.Add(_scene);
        root.Children.Add(layout);
        root.Children.Add(Handles());
        Content = new Border { CornerRadius = new CornerRadius(22), ClipToBounds = true, Child = root, Background = Brush.Parse("#07080C") };

        AddHandler(KeyDownEvent, (_, e) =>
        {
            if (_capture == null) return;
            var done = _capture;
            if (e.Key == Key.Escape) { _capture = null; done(null, 0); e.Handled = true; return; }
            if (_captureMouse) { e.Handled = true; return; }
            var name = Bindings.SdlName(e.PhysicalKey);
            if (name == null) { e.Handled = true; return; }
            _capture = null;
            done(name, 0);
            e.Handled = true;
        }, Avalonia.Interactivity.RoutingStrategies.Tunnel);
        AddHandler(PointerPressedEvent, (_, e) =>
        {
            if (_capture == null || !_captureMouse) return;
            var p = e.GetCurrentPoint(this).Properties;
            int m = p.IsLeftButtonPressed ? 1 : p.IsRightButtonPressed ? 2 : p.IsMiddleButtonPressed ? 3 : p.IsXButton1Pressed ? 4 : p.IsXButton2Pressed ? 5 : 0;
            if (m == 0) return;
            var done = _capture;
            _capture = null;
            e.Handled = true;
            // After this click is over, so the button it lands on does not take it.
            Dispatcher.UIThread.Post(() => done(null, m));
        }, Avalonia.Interactivity.RoutingStrategies.Tunnel);
        AddHandler(PointerWheelChangedEvent, (_, e) =>
        {
            if (_capture == null || !_captureMouse || e.Delta.Y == 0) return;
            var done = _capture;
            _capture = null;
            e.Handled = true;
            done(null, e.Delta.Y > 0 ? 6 : 7);
        }, Avalonia.Interactivity.RoutingStrategies.Tunnel);
        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter && _play.IsEnabled) Play();
            else if (e.Key == Key.Escape) WindowState = WindowState.Minimized;
        };

        UpdateSummary();
        GoTo(0);
        _version.PointerPressed += (_, _) =>
        {
            if (_releaseUrl == null) return;
            try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(OperatingSystem.IsWindows() ? "explorer" : "xdg-open", _releaseUrl) { UseShellExecute = false }); }
            catch (Exception) { }
        };
        _ = CheckVersion();
    }

    // The header's line: the engine's version, and a newer release when there is one.
    async Task CheckVersion()
    {
        var dir = _gameDir;
        var engine = await Task.Run(() => Updates.EngineVersion(dir));
        _version.Text = engine != null ? $"Launcher 0.1 · engine {engine.Split('-')[0]}-{(engine.Split('-').Length > 1 ? engine.Split('-')[1] : "")}" : "Launcher 0.1";
        var latest = await Updates.Latest();
        if (latest == null || engine == null) return;
        if (Updates.Newer(latest.Tag, engine))
        {
            _releaseUrl = latest.Url;
            _version.Text += $" · {latest.Tag} available";
            _version.Foreground = Brush.Parse("#E8C27A");
            _version.Cursor = new Cursor(StandardCursorType.Hand);
            ToolTip.SetTip(_version, "A newer release is out. Click to open its page.");
        }
        else _version.Text += " · up to date";
    }

    /// The monitors by their connector names (DP-1), the primary first.
    IReadOnlyList<(string Name, string Label, bool Primary)> Monitors()
    {
        var list = new List<(string, string, bool)>();
        try
        {
            foreach (var sc in Screens.All.OrderByDescending(x => x.IsPrimary))
            {
                var name = sc.DisplayName ?? "";
                if (string.IsNullOrEmpty(name)) continue;
                list.Add((name, $"{name}  ·  {sc.Bounds.Width}×{sc.Bounds.Height}{(sc.IsPrimary ? "  ·  primary" : "")}", sc.IsPrimary));
            }
        }
        catch (Exception) { }
        return list;
    }

    void CenterOnPrimary()
    {
        var screen = Screens.Primary ?? Screens.All.OrderByDescending(x => x.Bounds.Width * x.Bounds.Height).FirstOrDefault();
        if (screen == null) { WindowStartupLocation = WindowStartupLocation.CenterScreen; return; }
        var wa = screen.WorkingArea;
        double scale = screen.Scaling;
        int w = (int)(Width * scale), h = (int)(Height * scale);
        Position = new PixelPoint(wa.X + Math.Max(0, (wa.Width - w) / 2), wa.Y + Math.Max(0, (wa.Height - h) / 2));
    }

    void ToggleMaximized() => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    /// The frameless window's own edges: the very top drags it (double-click maximizes), the
    /// corners and the other three sides resize it.
    Control Handles()
    {
        const double edge = 7, corner = 18;
        var g = new Grid { IsHitTestVisible = true, Background = null };
        Border Zone(HorizontalAlignment h, VerticalAlignment v, double w, double ht, StandardCursorType cursor, WindowEdge? resize)
        {
            var b = new Border
            {
                HorizontalAlignment = h, VerticalAlignment = v, Background = Brushes.Transparent, Cursor = new Cursor(cursor),
                Width = double.IsNaN(w) ? double.NaN : w, Height = double.IsNaN(ht) ? double.NaN : ht,
            };
            b.PointerPressed += (_, e) =>
            {
                if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
                if (resize is { } r) { if (WindowState == WindowState.Normal) BeginResizeDrag(r, e); }
                else BeginMoveDrag(e);
                e.Handled = true;
            };
            if (resize == null) b.DoubleTapped += (_, _) => ToggleMaximized();
            g.Children.Add(b);
            return b;
        }
        var top = Zone(HorizontalAlignment.Stretch, VerticalAlignment.Top, double.NaN, 14, StandardCursorType.Arrow, null);
        top.Margin = new Thickness(corner, 0, corner, 0);
        Zone(HorizontalAlignment.Left, VerticalAlignment.Stretch, edge, double.NaN, StandardCursorType.LeftSide, WindowEdge.West).Margin = new Thickness(0, corner, 0, corner);
        Zone(HorizontalAlignment.Right, VerticalAlignment.Stretch, edge, double.NaN, StandardCursorType.RightSide, WindowEdge.East).Margin = new Thickness(0, corner, 0, corner);
        Zone(HorizontalAlignment.Stretch, VerticalAlignment.Bottom, double.NaN, edge, StandardCursorType.BottomSide, WindowEdge.South).Margin = new Thickness(corner, 0, corner, 0);
        Zone(HorizontalAlignment.Left, VerticalAlignment.Top, corner, corner, StandardCursorType.TopLeftCorner, WindowEdge.NorthWest);
        Zone(HorizontalAlignment.Right, VerticalAlignment.Top, corner, corner, StandardCursorType.TopRightCorner, WindowEdge.NorthEast);
        Zone(HorizontalAlignment.Left, VerticalAlignment.Bottom, corner, corner, StandardCursorType.BottomLeftCorner, WindowEdge.SouthWest);
        Zone(HorizontalAlignment.Right, VerticalAlignment.Bottom, corner, corner, StandardCursorType.BottomRightCorner, WindowEdge.SouthEast);
        return g;
    }

    static string? KeyArt(string? app0)
    {
        if (app0 == null) return null;
        foreach (var d in new[] { app0 + "-patch", app0 + "-UPDATE", app0 })
        {
            var p = System.IO.Path.Combine(d, "sce_sys", "pic1.png");
            if (File.Exists(p)) return p;
        }
        return null;
    }

    static Button IconButton(string data, string label, Action click)
    {
        var b = new Button
        {
            Classes = { "flat", "icon" }, Padding = new Thickness(0),
            Content = new Path { Data = Geometry.Parse(data), Stroke = Brush.Parse("#D5DBE6"), StrokeThickness = 1.6, StrokeLineCap = PenLineCap.Round, Width = 16, Height = 16, Stretch = Stretch.None },
        };
        AutomationProperties.SetName(b, label);
        b.Click += (_, _) => click();
        return b;
    }

    public void ShowPage(int index) => GoTo(index);

    void GoTo(int index)
    {
        if (index == _current) return;
        bool forward = index > _current;
        _current = index;
        _options.Reload();
        _page.IsTransitionReversed = !forward;
        _scene.Scrolled(0);
        if (index == Pages.ConsolePage)
        {
            // The console fills the page and scrolls itself.
            _consoleBtn.IsVisible = true;
            _scroll = null;
            if (_console.Parent is Border old) old.Child = null;
            _console.Margin = new Thickness(0, 0, 0, 6);
            _page.Content = new Border { Child = _console, RenderTransform = new TranslateTransform() };
        }
        else
        {
            var page = Pages.Build(index, _ctx);
            page.Margin = new Thickness(0, 0, 14, 6);
            var scroll = new ScrollViewer { Content = page, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, RenderTransform = new TranslateTransform() };
            scroll.ScrollChanged += (_, _) => _scene.Scrolled(scroll.Offset.Y);
            _scroll = scroll;
            _page.Content = scroll;
        }
        _scene.ShowPage(index);
        PlaceBead(true);
    }

    /// The page shown, built again where it is (its scroll position kept), with no transition.
    void Rebuild()
    {
        if (_scroll == null || _current < 0 || _current == Pages.ConsolePage) return;
        var at = _scroll.Offset;
        _options.Reload();
        var page = Pages.Build(_current, _ctx);
        page.Margin = new Thickness(0, 0, 14, 6);
        _scroll.Content = page;
        Dispatcher.UIThread.Post(() => _scroll.Offset = at, DispatcherPriority.Loaded);
    }

    void PlaceBead(bool animate)
    {
        if (_current >= _tabs.Count)
        {
            // A page with no tab (Game setup): no bead.
            _bead.Opacity = 0;
            foreach (var t in _tabs) t.Classes.Set("selected", false);
            return;
        }
        if (_current < 0) return;
        var b = _tabs[_current];
        if (b.Bounds.Width <= 0) return;
        if (!animate || _settings.ReduceMotion)
        {
            var t = _beadMove.Transitions; _beadMove.Transitions = null; _beadMove.X = b.Bounds.X; _beadMove.Transitions = t;
            var w = _bead.Transitions; _bead.Transitions = null; _bead.Width = b.Bounds.Width; _bead.Transitions = w;
        }
        else { _beadMove.X = b.Bounds.X; _bead.Width = b.Bounds.Width; }
        _bead.Height = b.Bounds.Height;
        _bead.Opacity = 1;
        for (int i = 0; i < _tabs.Count; i++) _tabs[i].Classes.Set("selected", i == _current);
    }

    void UpdateSummary()
    {
        var o = _options;
        var parts = new List<string>();
        var up = o.Get("upscaler", "FSR 1");
        parts.Add(up is "FSR 3.1" or "FSR 4.1.1" or "FSR 4" or "DLSS" ? $"{up} {o.Get("upscale_quality", "Native AA")}" : up == "Native TAA" ? "Native TAA" : $"Upscaler {up}");
        parts.Add(o.Get("resolution", "1920x1080").Replace("x", "×"));
        var cap = o.Get("frame_cap", "30");
        parts.Add(cap == "Uncapped" ? "Uncapped" : cap + " fps");
        parts.Add(o.SignedIn ? "Online" : "Offline");
        if (o.Get("haptics", "HD") == "HD" && ReadyCheck.DualSenseOnUsb()) parts.Add("DualSense HD");
        _summary.Text = string.Join(" · ", parts);
        _play.IsEnabled = _gameDir != null && !GameRunner.Running();
    }

    void Play()
    {
        _gameDir = Paths.FindGameDir(_settings.GameDir);
        if (_gameDir == null) { GoTo(7); return; }
        var monitor = string.IsNullOrEmpty(_settings.Monitor) ? Monitors().FirstOrDefault(m => m.Primary).Name : _settings.Monitor;
        // Closing as it starts: nothing to read the game's output, which goes where it always did.
        if (_settings.CloseOnPlay)
        {
            if (GameRunner.Start(_gameDir, _settings.PresentMode, monitor, Profiles.ConfigFile(_settings.Profile), _settings.Gamepad, null, () => { }) != null) Close();
            return;
        }
        var started = DateTime.Now;
        _console.Begin($"Bloodborne · {_settings.Profile}", null, GameRunner.Stop);
        var p = GameRunner.Start(_gameDir, _settings.PresentMode, monitor, Profiles.ConfigFile(_settings.Profile), _settings.Gamepad,
            _console.Add, () => Dispatcher.UIThread.Post(() => GameEnded(started)));
        if (p == null) { _console.End("Did not start"); GoTo(Pages.ConsolePage); return; }
        _console.SetLog(GameRunner.LogFile);
        _play.IsEnabled = false;
        _play.Content = new TextBlock { Text = "RUNNING", LetterSpacing = 5, Foreground = Brush.Parse("#C8B4B4") };
        GoTo(Pages.ConsolePage);
    }

    void GameEnded(DateTime started)
    {
        var played = DateTime.Now - started;
        _console.End($"The game quit after {(played.TotalHours >= 1 ? $"{(int)played.TotalHours} h {played.Minutes} min" : $"{Math.Max(1, (int)played.TotalMinutes)} min")}");
        _play.Content = new TextBlock { Text = "PLAY", LetterSpacing = 5, Foreground = Brush.Parse("#FFF4F2") };
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        UpdateSummary();
        Rebuild();
    }

    async Task<string?> PickFile(string title, string? start)
    {
        var opts = new FilePickerOpenOptions { Title = title, AllowMultiple = false };
        if (start != null && Directory.Exists(start)) opts.SuggestedStartLocation = await StorageProvider.TryGetFolderFromPathAsync(start);
        var r = await StorageProvider.OpenFilePickerAsync(opts);
        return r.FirstOrDefault()?.TryGetLocalPath();
    }

    async Task<string?> PickFolder(string? start)
    {
        var opts = new FolderPickerOpenOptions { Title = "The Helia's Dream folder", AllowMultiple = false };
        if (start != null) opts.SuggestedStartLocation = await StorageProvider.TryGetFolderFromPathAsync(start);
        var r = await StorageProvider.OpenFolderPickerAsync(opts);
        return r.FirstOrDefault()?.TryGetLocalPath();
    }
}
