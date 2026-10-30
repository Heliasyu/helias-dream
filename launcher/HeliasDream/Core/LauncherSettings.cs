using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HeliasDream.Core;

/// <summary>The launcher's own settings, in launcher.json beside the engine's config.</summary>
public sealed class LauncherSettings
{
    public string? GameDir { get; set; }
    public bool ReduceMotion { get; set; }
    /// Blur the game's own key art (sce_sys/pic1.png in the player's dump) behind the glass.
    public bool GameArtBackground { get; set; } = true;
    public bool CloseOnPlay { get; set; }
    /// How frames reach the screen (HELIAS_PRESENT_MODE): auto follows V-Sync.
    public string PresentMode { get; set; } = "mailbox";
    /// The monitor the game opens on (a connector name, DP-1); empty: the primary one.
    public string Monitor { get; set; } = "";
    /// The player profile (Core/Profiles.cs): Default, or one of profiles/*.toml.
    public string Profile { get; set; } = "Default";
    /// The art Add to Steam uses (bbp.png, bb.png, bb_hero.jpg, bb_logo.png, bb_icon.png).
    public string? SteamArt { get; set; }
    /// The controller the game takes input from (HELIAS_GAMEPAD, part of its name), or "auto":
    /// every controller, Steam Input's own pad standing in for the ones it handles.
    public string Gamepad { get; set; } = "auto";

    public static LauncherSettings Load()
    {
        try
        {
            if (File.Exists(Paths.LauncherSettingsFile))
                return JsonSerializer.Deserialize(File.ReadAllText(Paths.LauncherSettingsFile), SettingsJson.Default.LauncherSettings) ?? new();
        }
        catch (Exception) { }
        return new();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Paths.LauncherSettingsFile)!);
            File.WriteAllText(Paths.LauncherSettingsFile, JsonSerializer.Serialize(this, SettingsJson.Default.LauncherSettings));
        }
        catch (Exception) { }
    }
}

[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(LauncherSettings))]
internal partial class SettingsJson : JsonSerializerContext { }
