using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using HeliasDream.Core;

namespace HeliasDream.Views;

/// <summary>
/// The Console page: the running game's log (or a tool's, building FSR 4.1.1), live, filling the
/// page and scaling with the window as Lantern Engine's does. Lines arrive from any thread and are
/// shown five times a second; the newest 4000 stay on screen, the whole run is in the log file.
/// </summary>
public sealed class ConsoleView : Grid
{
    const int Keep = 4000;
    readonly ConcurrentQueue<string> _incoming = new();
    readonly LinkedList<string> _lines = new();
    readonly SelectableTextBlock _text = new()
    {
        FontFamily = new FontFamily("JetBrains Mono, DejaVu Sans Mono, Liberation Mono, Noto Sans Mono, monospace"),
        FontSize = 12, LineHeight = 17, TextWrapping = TextWrapping.Wrap, Foreground = Brush.Parse("#CFD6E2"),
    };
    readonly ScrollViewer _scroll;
    readonly TextBlock _title = Ui.Text("Console", 16, null, FontWeight.SemiBold);
    readonly TextBlock _state = Ui.Text("Nothing has run yet.", 12, "muted");
    readonly Border _dot = new() { Width = 10, Height = 10, CornerRadius = new CornerRadius(5), Background = Brush.Parse("#5A6070"), VerticalAlignment = VerticalAlignment.Center };
    readonly Button _stop, _openLog, _follow;
    bool _dirty, _followTail = true;
    string? _logFile;
    Action? _stopAction;

    public bool HasContent => _lines.Count > 0;

    public ConsoleView()
    {
        RowDefinitions = new RowDefinitions("Auto,*");
        RowSpacing = 14;
        _scroll = new ScrollViewer { Content = _text, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled, Padding = new Thickness(0, 0, 10, 0) };
        _scroll.ScrollChanged += (_, e) =>
        {
            // Scrolled up by hand: hold the view there until Follow (or the end) brings it back.
            if (e.ExtentDelta.Y != 0 || e.ViewportDelta.Y != 0) return;
            _followTail = _scroll.Offset.Y >= _scroll.Extent.Height - _scroll.Viewport.Height - 4;
            _follow!.IsVisible = !_followTail;
        };
        _stop = Ui.Ghost("Stop", () => _stopAction?.Invoke());
        _stop.IsVisible = false;
        _openLog = Ui.Ghost("Open log", () => Open(_logFile));
        _openLog.IsVisible = false;
        _follow = Ui.Ghost("Follow", () => { _followTail = true; _follow!.IsVisible = false; _scroll.ScrollToEnd(); });
        _follow.IsVisible = false;
        var copy = Ui.Ghost("Copy", async () =>
        {
            var top = TopLevel.GetTopLevel(this);
            if (top?.Clipboard != null) await top.Clipboard.SetTextAsync(string.Join('\n', _lines));
        });
        Ui.Tip(_stop, "Asks the game to quit, as closing its window does. Unsaved progress since the last autosave is lost.");
        Ui.Tip(copy, "Copies the lines shown here, for a bug report. The whole run is in the log file (Open log).");

        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 12 };
        head.Children.Add(_dot);
        var words = Ui.Col(2, _title, _state);
        Grid.SetColumn(words, 1); head.Children.Add(words);
        var buttons = Ui.Row(8, _follow, copy, _openLog, _stop);
        buttons.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(buttons, 2); head.Children.Add(buttons);
        Children.Add(Ui.Glass(head, 16));

        var body = new Border { Classes = { "glass" }, Padding = new Thickness(18, 14, 8, 14), Child = _scroll, Background = Brush.Parse("#B0080A10") };
        Grid.SetRow(body, 1);
        Children.Add(body);

        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
        timer.Tick += (_, _) => Drain();
        timer.Start();
    }

    /// A new run: the title, its log file and how to stop it. The last run's lines are cleared.
    public void Begin(string title, string? logFile, Action? stop)
    {
        Dispatcher.UIThread.VerifyAccess();
        while (_incoming.TryDequeue(out _)) { }
        _lines.Clear();
        _dirty = true;
        _title.Text = title;
        _state.Text = "Running" + (logFile != null ? $" · {logFile}" : "");
        _dot.Background = Brush.Parse("#9FD4A8");
        _logFile = logFile;
        _openLog.IsVisible = logFile != null;
        _stopAction = stop;
        _stop.IsVisible = stop != null;
        _followTail = true;
        _follow.IsVisible = false;
    }

    /// The run's log file, once it is known.
    public void SetLog(string? logFile)
    {
        _logFile = logFile;
        _openLog.IsVisible = logFile != null;
        if (logFile != null) _state.Text = $"Running · {logFile}";
    }

    /// Any thread.
    public void Add(string line) => _incoming.Enqueue(line);

    public void End(string how)
    {
        Dispatcher.UIThread.VerifyAccess();
        Drain();
        _state.Text = how + (_logFile != null ? $" · {_logFile}" : "");
        _dot.Background = Brush.Parse("#5A6070");
        _stop.IsVisible = false;
        _stopAction = null;
    }

    /// Runs a tool (the FSR 4.1.1 build) with its output here. `done` gets its exit code.
    public void RunTool(string title, string file, IEnumerable<string> args, string workDir, Action<int> done)
    {
        Process? p = null;
        Begin(title, null, () => { try { p?.Kill(true); } catch (Exception) { } });
        var psi = new ProcessStartInfo(file) { WorkingDirectory = workDir, UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var a in args) psi.ArgumentList.Add(a);
        try { p = Process.Start(psi); }
        catch (Exception e) { Add("Could not start it: " + e.Message); End("Did not start"); done(-1); return; }
        if (p == null) { End("Did not start"); done(-1); return; }
        p.OutputDataReceived += (_, e) => { if (e.Data != null) Add(e.Data); };
        p.ErrorDataReceived += (_, e) => { if (e.Data != null) Add(e.Data); };
        p.EnableRaisingEvents = true;
        p.Exited += (_, _) =>
        {
            try { p.WaitForExit(); } catch (Exception) { }
            int code = p.ExitCode;
            Dispatcher.UIThread.Post(() => { End(code == 0 ? "Finished" : $"Ended with code {code}"); done(code); });
        };
        p.BeginOutputReadLine();
        p.BeginErrorReadLine();
    }

    /// Runs work of the launcher's own (a download) with its lines here.
    public async void RunTask(string title, Func<Action<string>, System.Threading.CancellationToken, System.Threading.Tasks.Task<bool>> work, Action<bool> done)
    {
        var cts = new System.Threading.CancellationTokenSource();
        Begin(title, null, () => cts.Cancel());
        bool ok;
        try { ok = await work(Add, cts.Token); }
        catch (OperationCanceledException) { Add("Stopped."); ok = false; }
        catch (Exception e) { Add(e.Message); ok = false; }
        End(ok ? "Finished" : "Did not finish");
        done(ok);
    }

    void Drain()
    {
        while (_incoming.TryDequeue(out var l))
        {
            _lines.AddLast(l);
            if (_lines.Count > Keep) _lines.RemoveFirst();
            _dirty = true;
        }
        if (!_dirty) return;
        _dirty = false;
        _text.Text = string.Join('\n', _lines);
        if (_followTail) Dispatcher.UIThread.Post(() => _scroll.ScrollToEnd(), DispatcherPriority.Background);
    }

    static void Open(string? path)
    {
        if (path == null || !File.Exists(path)) return;
        try { Process.Start(new ProcessStartInfo(OperatingSystem.IsWindows() ? "explorer" : "xdg-open", path) { UseShellExecute = false }); }
        catch (Exception) { }
    }
}
