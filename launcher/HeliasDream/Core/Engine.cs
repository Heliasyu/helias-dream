using System;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;

namespace HeliasDream.Core;

/// <summary>
/// The engine. A release carries it inside this launcher (an embedded resource, packed by
/// tools/win_package.sh and tools/package_linux.sh), so HeliasDream(.exe) is the one program
/// in the folder: it is written to the per-user data folder when it is not there yet or differs,
/// always at the same path (Windows Firewall's rule stays), and run from there with
/// HELIAS_HOME_DIR naming the install folder (its patches, plugins and data). A launcher built
/// without it (a developer's) runs the engine beside it, as before. HELIAS_ENGINE overrides both.
/// </summary>
public static class Engine
{
    const string Resource = "engine";
    static string FileName => Paths.IsWindows ? "HeliasDream-engine.exe" : "heliasdream";
    static string? _extracted;

    public static bool Embedded => typeof(Engine).Assembly.GetManifestResourceInfo(Resource) != null;

    /// The engine to run for an install folder, or null.
    public static string? Find(string? gameDir)
    {
        if (Environment.GetEnvironmentVariable("HELIAS_ENGINE") is { Length: > 0 } e && File.Exists(e)) return e;
        if (Embedded) return Extract();
        return Paths.EngineIn(gameDir);
    }

    static string? Extract()
    {
        if (_extracted != null) return _extracted;
        var dir = Path.Combine(Paths.HeliasDataDir, "engine");
        var target = Path.Combine(dir, FileName);
        var stamp = target + ".sha256";
        try
        {
            string hash;
            using (var s = typeof(Engine).Assembly.GetManifestResourceStream(Resource)!)
                hash = Convert.ToHexString(SHA256.HashData(s));
            if (File.Exists(target) && File.Exists(stamp) && File.ReadAllText(stamp).Trim() == hash) return _extracted = target;
            Directory.CreateDirectory(dir);
            var tmp = target + ".new";
            using (var s = typeof(Engine).Assembly.GetManifestResourceStream(Resource)!)
            using (var f = File.Create(tmp))
                s.CopyTo(f);
            if (!Paths.IsWindows)
                File.SetUnixFileMode(tmp, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute |
                                          UnixFileMode.GroupRead | UnixFileMode.GroupExecute | UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
            File.Move(tmp, target, true);
            File.WriteAllText(stamp, hash);
            return _extracted = target;
        }
        catch (Exception)
        {
            // In use (a game still running) or the disk refused: the copy already there, if any.
            return File.Exists(target) ? _extracted = target : null;
        }
    }

    /// The settings the engine's process gets from the launcher wherever it runs from.
    public static void Prepare(ProcessStartInfo psi, string gameDir)
    {
        psi.WorkingDirectory = gameDir;
        psi.Environment["HELIAS_HOME_DIR"] = gameDir;
    }

    /// --play [engine arguments]: the game without the launcher's window (a terminal, a script,
    /// Steam's Game Mode), its output on this process's own. Returns the engine's exit code.
    public static int Play(string[] args)
    {
        var dir = Paths.FindGameDir(null);
        var engine = Find(dir);
        if (dir == null || engine == null)
        {
            Console.Error.WriteLine("Helia's Dream: no engine found (HELIAS_ENGINE names one)");
            return 1;
        }
        var psi = new ProcessStartInfo(engine) { UseShellExecute = false };
        foreach (var a in args[1..]) psi.ArgumentList.Add(a);
        Prepare(psi, dir);
        using var p = Process.Start(psi);
        if (p == null) return 1;
        p.WaitForExit();
        return p.ExitCode;
    }
}
