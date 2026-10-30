using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace HeliasDream.Core;

public enum Grade { Platinum, Gold, Silver, Bronze }

public sealed record Trophy(int Id, string Name, string Description, Grade Grade, bool Dlc);

public sealed record TrophyState(Trophy Trophy, DateTimeOffset? Earned)
{
    public bool Unlocked => Earned != null;
}

/// <summary>
/// Bloodborne's 40 trophies (the PS4 ids of CUSA03173) and what the engine recorded: it appends
/// "id unix-time" to &lt;data&gt;/trophies.txt when the game unlocks one (src/hle/system.cpp).
/// The platinum is the console's to award: it counts once every base-game trophy is earned.
/// The table follows Lantern Engine's (github.com/Awakenchan/LanternEngine, GPL), from
/// pyL1nx/bloodborne_pc's trophy research.
/// </summary>
public static class Trophies
{
    public const int PlatinumId = 0;

    public static readonly Trophy[] All =
    {
        new(0, "Bloodborne", "All trophies acquired. Hats off!", Grade.Platinum, false),
        new(1, "Yharnam Sunrise", "You lived through the hunt, and saw another day.", Grade.Gold, false),
        new(2, "Honoring Wishes", "Captivated by the moon presence, you pledge to watch over the hunter's dream.", Grade.Gold, false),
        new(3, "Childhood's Beginning", "You became an infant Great One, lifting humanity into its next childhood.", Grade.Gold, false),
        new(4, "Yharnam, Pthumerian Queen", "Defeat Yharnam, Blood Queen of the Old Labyrinth.", Grade.Gold, false),
        new(5, "Hunter's Essence", "Acquire all hunter weapons.", Grade.Gold, false),
        new(6, "Hunter's Craft", "Acquire all special hunter tools.", Grade.Gold, false),
        new(7, "Weapon Master", "Acquire a weapon of the highest level.", Grade.Silver, false),
        new(8, "Blood Gem Master", "Acquire an extremely precious blood gem.", Grade.Silver, false),
        new(9, "Rune Master", "Acquire an extremely precious Caryll Rune.", Grade.Silver, false),
        new(10, "Cainhurst", "Gain entry to Cainhurst, the lost and ruined castle.", Grade.Silver, false),
        new(11, "The Choir", "Gain entry to the realm of the Choir, the high stratum of the Healing Church.", Grade.Silver, false),
        new(12, "The Source of the Dream", "Discover the abandoned old workshop, the source of the hunter's dream.", Grade.Silver, false),
        new(13, "Nightmare Lecture Building", "Gain entry into the Byrgenwerth lecture building, that drifts within the realm of nightmare.", Grade.Silver, false),
        new(14, "Father Gascoigne", "Defeat the beast that once was Father Gascoigne.", Grade.Bronze, false),
        new(15, "Vicar Amelia", "Defeat the beast that once was Vicar Amelia.", Grade.Bronze, false),
        new(16, "Shadow of Yharnam", "Defeat the Shadow of Yharnam.", Grade.Bronze, false),
        new(17, "Rom, the Vacuous Spider", "Defeat Great One: Rom, the Vacuous Spider.", Grade.Bronze, false),
        new(18, "The One Reborn", "Defeat the One Reborn.", Grade.Bronze, false),
        new(19, "Micolash, Host of the Nightmare", "Defeat Micolash, Host of the Nightmare.", Grade.Bronze, false),
        new(20, "Mergo's Wet Nurse", "Defeat Great One: Mergo's Wet Nurse.", Grade.Bronze, false),
        new(21, "Cleric Beast", "Defeat Cleric Beast.", Grade.Bronze, false),
        new(22, "Blood-starved Beast", "Defeat Blood-starved Beast.", Grade.Bronze, false),
        new(23, "The Witch of Hemwick", "Defeat the Witch of Hemwick.", Grade.Bronze, false),
        new(24, "Darkbeast Paarl", "Defeat Darkbeast Paarl.", Grade.Bronze, false),
        new(25, "Amygdala", "Defeat Great One: Amygdala.", Grade.Bronze, false),
        new(26, "Martyr Logarius", "Defeat Martyr Logarius.", Grade.Bronze, false),
        new(27, "Celestial Emissary", "Defeat Great One: Celestial Emissary.", Grade.Bronze, false),
        new(28, "Ebrietas, Daughter of the Cosmos", "Defeat Great One: Ebrietas, Daughter of the Cosmos.", Grade.Bronze, false),
        new(29, "Blood Gem Contact", "Acquire a blood gem that imbues hunter weapons with special strength.", Grade.Bronze, false),
        new(30, "Rune Contact", "Acquire a Caryll Rune that endows hunters with special strength.", Grade.Bronze, false),
        new(31, "Chalice of Pthumeru", "Acquire the Chalice of Pthumeru that seals the catacombs that form a web deep below Yharnam.", Grade.Bronze, false),
        new(32, "Chalice of Ailing Loran", "Acquire the Chalice of Ailing Loran that seals the tragic land lost to the sands.", Grade.Bronze, false),
        new(33, "Chalice of Isz", "Acquire the Great Chalice of Isz that seals the home of the cosmic kin.", Grade.Bronze, false),
        new(34, "Old Hunter's Essence", "Acquire all old hunter weapons.", Grade.Gold, true),
        new(35, "Orphan of Kos", "Defeat Great One: Orphan of Kos.", Grade.Silver, true),
        new(36, "Ludwig, the Holy Blade", "Defeat the beast that was once Ludwig, the Holy Blade.", Grade.Bronze, true),
        new(37, "Lady Maria of the Astral Clocktower", "Defeat Lady Maria of the Astral Clocktower.", Grade.Bronze, true),
        new(38, "Living Failures", "Defeat the failed attempts to become Great Ones.", Grade.Bronze, true),
        new(39, "Laurence, the First Vicar", "Defeat the beast that was once Laurence, the First Vicar.", Grade.Bronze, true),
    };

    public static int Points(Grade g) => g switch { Grade.Platinum => 180, Grade.Gold => 90, Grade.Silver => 30, _ => 15 };

    /// Every trophy with when it was earned (first record wins), the platinum derived.
    public static List<TrophyState> Load(string dataDir)
    {
        var earned = new Dictionary<int, DateTimeOffset>();
        var file = Path.Combine(dataDir, "trophies.txt");
        try
        {
            if (File.Exists(file))
                foreach (var line in File.ReadAllLines(file))
                {
                    var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length >= 2 && int.TryParse(parts[0], out var id) && long.TryParse(parts[1], out var ts) && !earned.ContainsKey(id))
                        earned[id] = DateTimeOffset.FromUnixTimeSeconds(ts).ToLocalTime();
                }
        }
        catch (IOException) { }

        var baseIds = All.Where(t => !t.Dlc && t.Id != PlatinumId).Select(t => t.Id).ToList();
        if (!earned.ContainsKey(PlatinumId) && baseIds.All(earned.ContainsKey))
            earned[PlatinumId] = baseIds.Select(i => earned[i]).Max();

        return All.Select(t => new TrophyState(t, earned.TryGetValue(t.Id, out var w) ? w : null)).ToList();
    }
}

/// <summary>
/// The trophy pictures from the player's own dump: sce_sys/trophy/trophy00.trp keeps them as plain
/// PNG entries (TROP000.PNG is trophy 0). Read at run time; nothing of the game is shipped.
/// </summary>
public static class TrophyPackage
{
    public static Dictionary<int, byte[]> ReadIcons(string? app0)
    {
        var icons = new Dictionary<int, byte[]>();
        if (string.IsNullOrEmpty(app0)) return icons;
        var candidates = new[] { app0 + "-patch", app0 + "-UPDATE", app0 }
            .Select(d => Path.Combine(d, "sce_sys", "trophy", "trophy00.trp"));
        var trp = candidates.FirstOrDefault(File.Exists);
        if (trp == null) return icons;
        try
        {
            var d = File.ReadAllBytes(trp);
            if (d.Length < 0x60 || BE32(d, 0) != 0xDCA24D00) return icons;
            int count = (int)BE32(d, 16), esz = (int)BE32(d, 20);
            for (int i = 0; i < count; i++)
            {
                int o = 0x60 + i * esz;
                if (o + 48 > d.Length) break;
                var name = System.Text.Encoding.ASCII.GetString(d, o, 32).TrimEnd('\0');
                long off = (long)BE64(d, o + 32), size = (long)BE64(d, o + 40);
                if (!name.StartsWith("TROP") || !name.EndsWith(".PNG") || off + size > d.Length) continue;
                if (int.TryParse(name.AsSpan(4, name.Length - 8), out var id))
                    icons[id] = d.AsSpan((int)off, (int)size).ToArray();
            }
        }
        catch (Exception) { }
        return icons;
    }

    static uint BE32(byte[] d, int o) => (uint)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]);
    static ulong BE64(byte[] d, int o) => (ulong)BE32(d, o) << 32 | BE32(d, o + 4);
}
