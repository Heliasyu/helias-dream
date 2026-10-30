using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace HeliasDream.Core;

/// <summary>
/// The controllers plugged in (Linux: /proc/bus/input/devices, the joysticks among them), for the
/// Controls page's status and its controller choice. The game itself opens them through SDL;
/// the choice reaches it as HELIAS_GAMEPAD (part of the name), src/host/window.cpp.
/// </summary>
public static class Controllers
{
    public enum Kind { PlayStation, Xbox, Steam, Other }

    public sealed record Pad(string Name, ushort Vendor, ushort Product, bool Usb)
    {
        public Kind Kind => Vendor switch
        {
            0x054c => Kind.PlayStation,
            0x045e => Kind.Xbox,
            0x28de => Kind.Steam,
            _ => Name.Contains("Xbox", StringComparison.OrdinalIgnoreCase) || Name.Contains("X-Box", StringComparison.OrdinalIgnoreCase) ? Kind.Xbox : Kind.Other,
        };
        /// A DualSense or DualSense Edge: HD haptics and the adaptive triggers.
        public bool DualSense => Vendor == 0x054c && Product is 0x0ce6 or 0x0df2;
        /// The name without the maker's prefix, as SDL shows it.
        public string Short => Name.Replace("Sony Interactive Entertainment ", "").Replace("Microsoft ", "").Trim();
    }

    public static List<Pad> List()
    {
        var list = new List<Pad>();
        if (!OperatingSystem.IsLinux()) return list;
        string text;
        try { text = File.ReadAllText("/proc/bus/input/devices"); }
        catch (Exception) { return list; }
        foreach (var block in text.Split("\n\n", StringSplitOptions.RemoveEmptyEntries))
        {
            string? name = null, handlers = null;
            ushort vendor = 0, product = 0;
            bool usb = false;
            foreach (var line in block.Split('\n'))
            {
                if (line.StartsWith("I:"))
                {
                    foreach (var part in line[2..].Split(' ', StringSplitOptions.RemoveEmptyEntries))
                    {
                        var kv = part.Split('=');
                        if (kv.Length != 2) continue;
                        if (kv[0] == "Bus") usb = kv[1] == "0003";
                        if (kv[0] == "Vendor") vendor = Convert.ToUInt16(kv[1], 16);
                        if (kv[0] == "Product") product = Convert.ToUInt16(kv[1], 16);
                    }
                }
                else if (line.StartsWith("N: Name=")) name = line[8..].Trim('"');
                else if (line.StartsWith("H: Handlers=")) handlers = line[12..];
            }
            if (name == null || handlers == null || !handlers.Split(' ').Any(h => h.StartsWith("js"))) continue;
            // A DualSense's motion sensors and touchpad are joysticks of their own; a keyboard
            // with a joystick handler is not a controller.
            if (name.EndsWith("Motion Sensors") || name.EndsWith("Touchpad") || handlers.Contains("kbd")) continue;
            list.Add(new Pad(name, vendor, product, usb));
        }
        return list;
    }
}
