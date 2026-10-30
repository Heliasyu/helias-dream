using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;

namespace HeliasDream.Core;

/// <summary>
/// FSR 4's assets (src/host/fsr4v07_pass.h): AMD's FSR 4 INT8 model ("v07", from the FidelityFX
/// SDK source AMD published under MIT) as SPIR-V and weights, built by FireBurn's Q2RTX port and
/// kept in its repository with that licence. Downloaded on the player's request into
/// ~/.local/share/HeliasDream/fsr4_v07, as Lantern Engine's tools/fetch_fsr4_assets.sh does; nothing of
/// it is in this repository.
/// </summary>
public static class Fsr4Assets
{
    const string Commit = "ae8d628fae208813172446d1e49ed94150b04658";
    const string Base = "https://raw.githubusercontent.com/FireBurn/Q2RTX/" + Commit + "/baseq2/fsr4_shaders/";
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(60) };

    public static string Dir => Path.Combine(Paths.HeliasDataDir, "fsr4_v07");

    public static bool Present => File.Exists(Path.Combine(Dir, "rcas.spv")) &&
                                  File.Exists(Path.Combine(Dir, "fsr4_model_v07_i8_balanced_initializers.bin"));

    /// Every file of the set: the shared passes, and per model its weights and, per output tier
    /// (1080: outputs up to 1920x1080; 2160: up to 3840x2160), its passes.
    public static List<string> Files()
    {
        var files = new List<string> { "LICENSE-FSR4-v07.txt", "rcas.spv", "spd_auto_exposure.spv" };
        foreach (var model in new[] { "native", "quality", "balanced", "performance", "ultraperf", "drs" })
        {
            files.Add($"fsr4_model_v07_i8_{model}_initializers.bin");
            files.Add($"fsr4_model_v07_i8_{model}_pre_weights.bin");
            files.Add($"fsr4_model_v07_i8_{model}_shader_manifest.json");
            foreach (var tier in new[] { "1080", "2160" })
            {
                files.Add($"fsr4_model_v07_i8_{model}_{tier}_pre.spv");
                files.Add($"fsr4_model_v07_i8_{model}_{tier}_post.spv");
                for (int pass = 1; pass <= 12; pass++) files.Add($"fsr4_model_v07_i8_{model}_{tier}_pass{pass}.spv");
            }
        }
        return files;
    }

    /// Downloads what is missing; each line of progress to `say`. True when the set is whole.
    public static async Task<bool> Download(Action<string> say, CancellationToken ct = default)
    {
        Directory.CreateDirectory(Dir);
        var files = Files();
        int got = 0, had = 0;
        say($"FSR 4 assets: {files.Count} files from FireBurn/Q2RTX ({Commit[..8]}, MIT) into {Dir}");
        foreach (var f in files)
        {
            var dest = Path.Combine(Dir, f);
            if (File.Exists(dest) && new FileInfo(dest).Length > 0) { had++; continue; }
            try
            {
                var bytes = await Http.GetByteArrayAsync(Base + f, ct);
                await File.WriteAllBytesAsync(dest + ".part", bytes, ct);
                File.Move(dest + ".part", dest, true);
                got++;
                if (got % 10 == 0 || got + had == files.Count) say($"  {got + had} of {files.Count}");
            }
            catch (Exception e) when (e is not OperationCanceledException)
            {
                say($"  {f}: {e.Message}");
                return false;
            }
        }
        say($"Done: {got} downloaded, {had} already there.");
        return Present;
    }
}
