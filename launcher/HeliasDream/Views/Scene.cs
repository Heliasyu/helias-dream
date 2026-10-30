using System;
using System.IO;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Path = Avalonia.Controls.Shapes.Path;

namespace HeliasDream.Views;

/// <summary>
/// The moonlit background: the player's own key art (blurred, if their dump has it), a moon, far
/// spires and near spires. Each layer moves at its own speed when the page changes (sideways) and
/// when a page scrolls (up), which gives the depth.
/// </summary>
public sealed class Scene : Panel
{
    sealed record Layer(Control Visual, TranslateTransform Move, double Speed, double BaseY = 0);

    readonly Layer[] _layers;
    bool _reduceMotion;

    // The near and far skylines: drawn here, nothing of the game's.
    const string Near = "M0,640 L60,640 L70,590 L80,640 L130,640 L140,560 L150,520 L160,560 L170,640 L240,640 L250,600 L262,470 L274,600 L284,640 L360,640 L372,580 L380,540 L388,580 L400,640 L470,640 L480,610 L496,430 L512,610 L522,640 L600,640 L612,590 L640,560 L668,590 L680,640 L760,640 L768,600 L778,500 L788,600 L796,640 L880,640 L892,570 L900,520 L908,570 L920,640 L1010,640 L1022,600 L1036,455 L1050,600 L1062,640 L1140,640 L1150,590 L1160,545 L1170,590 L1180,640 L1280,640 L1280,800 L0,800 Z";
    const string Ground = "M0,690 L90,690 L104,650 L118,690 L300,690 L316,620 L332,690 L700,690 L716,640 L732,690 L1090,690 L1106,630 L1122,690 L1280,690 L1280,800 L0,800 Z";
    const string Far = "M0,600 L90,600 L100,540 L112,600 L220,600 L232,520 L244,600 L400,600 L410,560 L424,480 L438,560 L450,600 L640,600 L652,530 L664,600 L820,600 L832,500 L844,600 L1000,600 L1012,550 L1026,470 L1040,550 L1052,600 L1280,600 L1280,800 L0,800 Z";

    public Scene(string? keyArt)
    {
        ClipToBounds = true;
        Background = new LinearGradientBrush
        {
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
            GradientStops = { new(Color.Parse("#151A26"), 0), new(Color.Parse("#0C0F17"), 0.55), new(Color.Parse("#07080C"), 1) },
        };

        Children.Add(Glow("#73A33142", 0.2, 1.1, 0.6));
        Children.Add(Glow("#8C3C4A68", 0.6, 0.7, 0.55));

        Layer? art = null;
        if (keyArt != null && File.Exists(keyArt))
        {
            try
            {
                var img = new Image
                {
                    Source = new Bitmap(keyArt), Stretch = Stretch.UniformToFill, Opacity = 0.42,
                    Effect = new BlurEffect { Radius = 46 }, Margin = new Thickness(-120, -80, -120, -80),
                };
                art = new Layer(img, new TranslateTransform(), 0.10);
            }
            catch (Exception) { }
        }

        var moon = new Ellipse
        {
            Width = 600, Height = 600, HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top, Margin = new Thickness(0, -190, -130, 0),
            Effect = new BlurEffect { Radius = 8 }, Opacity = 0.85,
            Fill = new RadialGradientBrush
            {
                GradientStops =
                {
                    new(Color.Parse("#F2E9EEFA"), 0), new(Color.Parse("#F2E9EEFA"), 0.152), new(Color.Parse("#59C8D6F0"), 0.157),
                    new(Color.Parse("#24A9BFE3"), 0.46), new(Color.Parse("#00A9BFE3"), 1),
                },
            },
        };

        // Each skyline carries the town below it, so a scroll that lifts the layer never shows
        // past its bottom: the far one a plain dark mass, the near one with lit windows.
        var far = new Grid
        {
            Width = 2200, Height = 330, HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom, Margin = new Thickness(0, 0, 0, 40), Opacity = 0.6,
            Children = { Skyline(Far, "#0A0D14", 1, 2200, 330, 0), BelowBand("#0A0D14", 330, null) },
        };
        var near = new Grid
        {
            Width = 2400, Height = 370, HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom,
            Children =
            {
                Skyline(Near, "#05060A", 0.94, 2400, 370, 0), Skyline(Ground, "#030406", 1, 2400, 180, 0),
                Windows(2400, 180, 14, seed: 3), BelowBand("#030406", 370, Windows(2400, Below, 160, seed: 11)),
            },
        };

        _layers = art != null
            ? new[] { art, new Layer(moon, new TranslateTransform(), 0.14), new Layer(far, new TranslateTransform(), 0.45), new Layer(near, new TranslateTransform(), 1.0) }
            : new[] { new Layer(moon, new TranslateTransform(), 0.14), new Layer(far, new TranslateTransform(), 0.45), new Layer(near, new TranslateTransform(), 1.0) };

        foreach (var l in _layers)
        {
            l.Move.Transitions = new Transitions
            {
                new DoubleTransition { Property = TranslateTransform.XProperty, Duration = TimeSpan.FromMilliseconds(1100), Easing = new CubicEaseOut() },
                new DoubleTransition { Property = TranslateTransform.YProperty, Duration = TimeSpan.FromMilliseconds(260), Easing = new CubicEaseOut() },
            };
            l.Move.Y = l.BaseY;
            l.Visual.RenderTransform = l.Move;
            Children.Add(l.Visual);
        }
    }

    public bool ReduceMotion
    {
        get => _reduceMotion;
        set { _reduceMotion = value; if (value) foreach (var l in _layers) { l.Move.X = 0; l.Move.Y = l.BaseY; } }
    }

    /// Page index: each layer slides left by its own share.
    public void ShowPage(int index)
    {
        if (_reduceMotion) return;
        foreach (var l in _layers) l.Move.X = -index * 130 * l.Speed;
    }

    /// A page's scroll offset: each layer rises by its own share.
    public void Scrolled(double offset)
    {
        if (_reduceMotion) return;
        foreach (var l in _layers) l.Move.Y = l.BaseY - offset * 0.22 * l.Speed;
    }

    static Control Skyline(string data, string fill, double opacity, double width, double height, double lift) => new Path
    {
        Data = Geometry.Parse(data), Fill = Brush.Parse(fill), Opacity = opacity, Stretch = Stretch.Fill,
        Width = width, Height = height, Margin = new Thickness(0, 0, 0, lift),
        HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left, VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom,
    };

    const double Below = 900;

    /// The town under a skyline: a band hanging below the layer's own box (drawn, not laid out,
    /// so the skyline stays at the window's bottom), lifted into view as a page scrolls.
    static Control BelowBand(string fill, double top, Control? content)
    {
        var band = new Grid
        {
            Height = Below, VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top, Background = Brush.Parse(fill),
            RenderTransform = new TranslateTransform(0, top - 1), IsHitTestVisible = false,
        };
        if (content != null) band.Children.Add(content);
        return band;
    }

    /// Warm lit windows scattered over a dark band: small panes in pairs and stacks, a few
    /// brighter than the rest. Drawn from a fixed seed so they stay put between starts.
    static Control Windows(double width, double height, int count, int seed)
    {
        var rnd = new Random(seed);
        var canvas = new Canvas { Width = width, Height = height, IsHitTestVisible = false,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom, HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left };
        for (int i = 0; i < count; i++)
        {
            double x = rnd.NextDouble() * (width - 40), y = 12 + rnd.NextDouble() * (height - 40);
            int cols = 1 + rnd.Next(3), rows = 1 + rnd.Next(3);
            double w = 5 + rnd.Next(4), h = 8 + rnd.Next(6);
            var warm = rnd.NextDouble() < 0.82 ? "#E8B86A" : "#F3D9A4";
            double glow = 0.25 + rnd.NextDouble() * 0.6;
            for (int c = 0; c < cols; c++)
                for (int r = 0; r < rows; r++)
                {
                    if (rnd.NextDouble() < 0.3) continue;
                    var pane = new Border
                    {
                        Width = w, Height = h, CornerRadius = new CornerRadius(w / 2, w / 2, 1, 1),
                        Background = Brush.Parse(warm), Opacity = glow * (0.7 + rnd.NextDouble() * 0.3),
                        BoxShadow = BoxShadows.Parse($"0 0 10 2 #66E8B86A"),
                    };
                    Canvas.SetLeft(pane, x + c * (w + 6));
                    Canvas.SetTop(pane, y + r * (h + 9));
                    canvas.Children.Add(pane);
                }
        }
        return canvas;
    }

    static Control Glow(string color, double x, double y, double radius) => new Border
    {
        IsHitTestVisible = false,
        Background = new RadialGradientBrush
        {
            Center = new RelativePoint(x, y, RelativeUnit.Relative), GradientOrigin = new RelativePoint(x, y, RelativeUnit.Relative),
            RadiusX = new RelativeScalar(radius, RelativeUnit.Relative), RadiusY = new RelativeScalar(radius, RelativeUnit.Relative),
            GradientStops = { new(Color.Parse(color), 0), new(Colors.Transparent, 1) },
        },
    };
}
