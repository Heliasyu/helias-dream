using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Controls.Primitives;
using HeliasDream.Core;

namespace HeliasDream.Views;

/// <summary>Small builders for the glass controls every page uses.</summary>
public static class Ui
{
    public static TextBlock Text(string text, double size = 14, string? cls = null, FontWeight weight = FontWeight.Normal)
    {
        var t = new TextBlock { Text = text, FontSize = size, FontWeight = weight, TextWrapping = TextWrapping.Wrap };
        if (cls != null) foreach (var c in cls.Split(' ')) t.Classes.Add(c);
        return t;
    }

    public static TextBlock Title(string text, double size = 48) =>
        new() { Text = text, FontSize = size, Classes = { "display" }, LineHeight = size * 1.05 };

    public static TextBlock Eyebrow(string text) => Text(text.ToUpperInvariant(), 12, "eyebrow");

    public static Border Glass(Control child, double padding = 20) =>
        new() { Classes = { "glass" }, Padding = new Thickness(padding), Child = child };

    public static Border Well(Control child) => new() { Classes = { "well" }, Child = child };

    public static StackPanel Col(double gap, params Control[] children)
    {
        var s = new StackPanel { Spacing = gap };
        foreach (var c in children) s.Children.Add(c);
        return s;
    }

    public static StackPanel Row(double gap, params Control[] children)
    {
        var s = new StackPanel { Orientation = Orientation.Horizontal, Spacing = gap };
        foreach (var c in children) s.Children.Add(c);
        return s;
    }

    public static Button Ghost(string text, Action onClick)
    {
        var b = new Button { Content = text, Classes = { "flat", "ghost" }, FontSize = 14 };
        b.Click += (_, _) => onClick();
        return b;
    }

    /// A round status dot with a tick, a dot or a cross.
    public static Control StatusDot(CheckState state)
    {
        var (fill, stroke, data) = state switch
        {
            CheckState.Ok => ("#2E78C88C", "#9FD4A8", "M3.5,8.5 L6.5,11.5 L12.5,4.5"),
            CheckState.Note => ("#2EE0B060", "#E8C27A", "M8,4 L8,9 M8,12 L8,12.2"),
            _ => ("#33E06060", "#F0A0A0", "M4.5,4.5 L11.5,11.5 M11.5,4.5 L4.5,11.5"),
        };
        return new Border
        {
            Width = 22, Height = 22, CornerRadius = new CornerRadius(11), Background = Brush.Parse(fill),
            Child = new Path
            {
                Data = Geometry.Parse(data), Stroke = Brush.Parse(stroke), StrokeThickness = 2,
                StrokeLineCap = PenLineCap.Round, StrokeJoin = PenLineJoin.Round, Width = 16, Height = 16,
                Stretch = Stretch.None, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            },
        };
    }

    /// A row of pill buttons with a glass bead that slides to the chosen one.
    public static Control Segmented(IReadOnlyList<string> values, string current, Action<string> onPick,
                                    Func<string, string>? label = null, double fontSize = 13, bool stretch = false)
    {
        var track = new Border { Classes = { "track" } };
        var grid = new Grid();
        var bead = new Border
        {
            Classes = { "bead" }, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Stretch,
            RenderTransform = new TranslateTransform(), IsHitTestVisible = false, Opacity = 0,
        };
        var tt = (TranslateTransform)bead.RenderTransform!;
        tt.Transitions = new Transitions { new DoubleTransition { Property = TranslateTransform.XProperty, Duration = TimeSpan.FromMilliseconds(420), Easing = new CubicEaseOut() } };
        bead.Transitions = new Transitions { new DoubleTransition { Property = Layoutable.WidthProperty, Duration = TimeSpan.FromMilliseconds(420), Easing = new CubicEaseOut() } };

        Panel buttons = stretch ? new UniformGrid { Rows = 1 } : new StackPanel { Orientation = Orientation.Horizontal, Spacing = 3 };
        var list = new List<Button>();
        string sel = current;

        void Place(bool animate)
        {
            var b = list.FirstOrDefault(x => (string)x.Tag! == sel);
            if (b == null || b.Bounds.Width <= 0) { bead.Opacity = 0; return; }
            if (!animate) { var t = tt.Transitions; tt.Transitions = null; tt.X = b.Bounds.X; tt.Transitions = t; var w = bead.Transitions; bead.Transitions = null; bead.Width = b.Bounds.Width; bead.Transitions = w; }
            else { tt.X = b.Bounds.X; bead.Width = b.Bounds.Width; }
            bead.Opacity = 1;
            foreach (var x in list) x.Classes.Set("selected", ReferenceEquals(x, b));
        }

        foreach (var v in values)
        {
            var b = new Button
            {
                Content = label?.Invoke(v) ?? v, Tag = v, Classes = { "flat" }, CornerRadius = new CornerRadius(999),
                Padding = new Thickness(14, 7), FontSize = fontSize, FontWeight = FontWeight.SemiBold,
                HorizontalAlignment = stretch ? HorizontalAlignment.Stretch : HorizontalAlignment.Left,
            };
            b.Click += (_, _) => { sel = v; Place(true); onPick(v); };
            list.Add(b);
            buttons.Children.Add(b);
        }
        grid.Children.Add(bead);
        grid.Children.Add(buttons);
        track.Child = grid;
        bool first = true;
        buttons.LayoutUpdated += (_, _) => { if (first && list.All(b => b.Bounds.Width > 0)) { first = false; Place(false); } };
        track.SizeChanged += (_, _) => Place(false);
        return track;
    }

    /// One setting as a row: its label and help, and the control that suits its values.
    public static Control OptionRow(OptionDef def, OptionsFile opts, Action? changed = null)
    {
        var value = opts.Get(def.Key) ?? def.DefaultValue;
        if (!def.Values.Contains(value)) value = def.DefaultValue;
        void Set(string v) { opts.Set(def.Key, v); changed?.Invoke(); }

        Control control;
        bool numeric = def.Values.Length >= 6 && def.Values.All(v => double.TryParse(v.TrimStart('x', '+').TrimEnd('%'), out _));
        bool onOff = def.Values.Length == 2 && def.Values.Contains("On") && def.Values.Contains("Off");
        if (onOff)
        {
            var sw = new ToggleSwitch { IsChecked = value == "On", HorizontalAlignment = HorizontalAlignment.Right };
            sw.IsCheckedChanged += (_, _) => Set(sw.IsChecked == true ? "On" : "Off");
            control = sw;
        }
        else if (numeric)
        {
            var readout = Text(value, 14, null, FontWeight.SemiBold);
            readout.MinWidth = 44; readout.TextAlignment = TextAlignment.Right; readout.VerticalAlignment = VerticalAlignment.Center;
            var slider = new Slider
            {
                Minimum = 0, Maximum = def.Values.Length - 1, Value = Array.IndexOf(def.Values, value), Width = 220,
                IsSnapToTickEnabled = true, TickFrequency = 1, VerticalAlignment = VerticalAlignment.Center,
            };
            slider.PropertyChanged += (_, e) =>
            {
                if (e.Property != RangeBase.ValueProperty) return;
                var v = def.Values[(int)Math.Round(slider.Value)];
                if (readout.Text == v) return;
                readout.Text = v; Set(v);
            };
            if (def.Values.Length > 100)
            {
                // A long range (HDR's nits, in ones): the slider for the rough value, a step
                // of one either way for the fine one (the arrow keys on the slider do the same).
                Button Step(string glyph, int by)
                {
                    var b = new Button { Content = glyph, Classes = { "flat", "ghost" }, Width = 34, Height = 34, Padding = new Thickness(0), FontSize = 16, HorizontalContentAlignment = HorizontalAlignment.Center };
                    b.Click += (_, _) => slider.Value = Math.Clamp(Math.Round(slider.Value) + by, 0, def.Values.Length - 1);
                    return b;
                }
                readout.MinWidth = 52;
                slider.SmallChange = 1;
                control = Row(8, Step("−", -1), slider, Step("+", 1), readout);
            }
            else control = Row(12, slider, readout);
        }
        else if (def.Values.Length <= 5 && def.Values.Sum(v => v.Length) <= 30)
            control = Segmented(def.Values, value, Set, label: v => def.Key == "window_mode" && v == "Fullscreen" ? "Fullscreen (borderless)" : v);
        else
        {
            var combo = new ComboBox { ItemsSource = def.Values, SelectedItem = value };
            combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string s) Set(s); };
            control = combo;
        }
        control.VerticalAlignment = VerticalAlignment.Center;

        var labels = Col(2, Text(def.Label, 14, null, FontWeight.Medium), Text(def.Help + (def.Restart ? " Takes effect at the next start." : ""), 12, "muted"));
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
        g.Children.Add(labels);
        Grid.SetColumn(control, 1);
        g.Children.Add(control);
        var row = Well(g);
        Tip(row, OptionSchema.Tips.TryGetValue(def.Key, out var tip) ? tip : def.Help);
        return row;
    }

    /// A hover explanation on a row or control, in the glass tooltip style.
    public static T Tip<T>(T control, string text) where T : Control
    {
        ToolTip.SetTip(control, new TextBlock { Text = text, TextWrapping = TextWrapping.Wrap, MaxWidth = 340, FontSize = 13, LineHeight = 19 });
        ToolTip.SetShowDelay(control, 450);
        ToolTip.SetPlacement(control, PlacementMode.Top);
        return control;
    }

    /// A row with a label, a line of help and a control on the right.
    public static Control Row2(string label, string help, Control control)
    {
        control.VerticalAlignment = VerticalAlignment.Center;
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
        g.Children.Add(Col(2, Text(label, 14, null, FontWeight.Medium), Text(help, 12, "muted")));
        Grid.SetColumn(control, 1);
        g.Children.Add(control);
        return Well(g);
    }

    /// Settings folded away under a title, as Lantern Engine's Advanced.
    public static Control Advanced(string title, params Control[] rows)
    {
        var body = Col(8, rows);
        body.Margin = new Thickness(0, 8, 0, 0);
        return new Expander
        {
            Header = Text(title, 13, "soft", FontWeight.SemiBold), Content = body, Classes = { "advanced" },
            HorizontalAlignment = HorizontalAlignment.Stretch, IsExpanded = false,
        };
    }

    public static Control Group(OptionGroup group, OptionsFile opts, Action? changed = null)
    {
        var col = Col(8, Eyebrow(group.Title));
        col.Children[0].Margin = new Thickness(4, 0, 0, 6);
        foreach (var d in group.Options) col.Children.Add(OptionRow(d, opts, changed));
        return Glass(col, 18);
    }

    public static IBrush Medal(Grade g) => g switch
    {
        Grade.Platinum => Radial("#FFFFFF", "#C9D6EE", "#6D7D99"),
        Grade.Gold => Radial("#F8ECC8", "#C9A35E", "#6E5530"),
        Grade.Silver => Radial("#F2F4F7", "#AEB6C2", "#5D6573"),
        _ => Radial("#F1D2B6", "#B07A4F", "#5E3C22"),
    };

    static IBrush Radial(string a, string b, string c) => new RadialGradientBrush
    {
        GradientOrigin = new RelativePoint(0.35, 0.3, RelativeUnit.Relative),
        Center = new RelativePoint(0.35, 0.3, RelativeUnit.Relative),
        GradientStops = { new GradientStop(Color.Parse(a), 0), new GradientStop(Color.Parse(b), 0.55), new GradientStop(Color.Parse(c), 1) },
    };
}
