using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Media;
using Avalonia.Styling;

namespace HeliasDream.Views;

/// <summary>
/// The page change: the old page drifts away and fades, the new one glides in from the side it
/// is coming from (the right going forward, the left going back) and fades up.
/// </summary>
public sealed class GlidePage : IPageTransition
{
    public bool Reduced { get; set; }

    public async Task Start(Visual? from, Visual? to, bool forward, CancellationToken cancel)
    {
        if (cancel.IsCancellationRequested) return;
        double dir = forward ? 1 : -1;
        var tasks = new System.Collections.Generic.List<Task>();
        var outTime = TimeSpan.FromMilliseconds(Reduced ? 120 : 300);
        var inTime = TimeSpan.FromMilliseconds(Reduced ? 160 : 620);

        if (from != null)
        {
            var a = new Animation
            {
                Duration = outTime, Easing = new CubicEaseIn(), FillMode = FillMode.Forward,
                Children =
                {
                    new KeyFrame { Cue = new Cue(0), Setters = { new Setter(Visual.OpacityProperty, 1d), new Setter(TranslateTransform.XProperty, 0d) } },
                    new KeyFrame { Cue = new Cue(1), Setters = { new Setter(Visual.OpacityProperty, 0d), new Setter(TranslateTransform.XProperty, Reduced ? 0d : -dir * 36) } },
                },
            };
            tasks.Add(a.RunAsync(from, cancel));
        }
        if (to != null)
        {
            to.IsVisible = true;
            var a = new Animation
            {
                Duration = inTime, Easing = new CubicEaseOut(), FillMode = FillMode.Forward, Delay = TimeSpan.FromMilliseconds(Reduced ? 0 : 90),
                Children =
                {
                    new KeyFrame { Cue = new Cue(0), Setters = { new Setter(Visual.OpacityProperty, 0d), new Setter(TranslateTransform.XProperty, Reduced ? 0d : dir * 52) } },
                    new KeyFrame { Cue = new Cue(1), Setters = { new Setter(Visual.OpacityProperty, 1d), new Setter(TranslateTransform.XProperty, 0d) } },
                },
            };
            tasks.Add(a.RunAsync(to, cancel));
        }
        await Task.WhenAll(tasks);
        if (from != null && !cancel.IsCancellationRequested) from.IsVisible = false;
    }
}
