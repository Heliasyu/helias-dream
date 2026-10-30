using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Input;

namespace HeliasDream.Core;

/// <summary>
/// The keyboard and mouse bindings, as the engine keeps them ([keys] of HeliasDream-options.toml,
/// src/host/bindings.cpp): per action one key, by SDL's scancode name ("W", "Left Alt"), and
/// one mouse input (Mouse1..Mouse5, WheelUp, WheelDown), written "C, Mouse3".
/// </summary>
public static class Bindings
{
    public sealed record Action(string Key, string Label, string Help, string? DefaultKey, int DefaultMouse);

    public static readonly Action[] All =
    {
        new("move_forward", "Move Forward", "Left stick up.", "W", 0),
        new("move_back", "Move Back", "Left stick down.", "S", 0),
        new("move_left", "Move Left", "Left stick left.", "A", 0),
        new("move_right", "Move Right", "Left stick right.", "D", 0),
        new("walk", "Walk", "Hold with the movement keys to walk.", "Left Alt", 0),
        new("roll", "Dodge / Sprint", "Circle. Tap to dodge, hold to sprint.", "Space", 0),
        new("lock_on", "Lock On", "R3. Lock on, or reset the camera.", "Q", 0),
        new("attack", "Attack", "R1. With the strong modifier held, R2.", null, 1),
        new("strong_attack", "Strong Attack", "R2. Hold to charge.", "V", 0),
        new("strong_modifier", "Strong Attack Modifier", "Held with Attack, attacks strong.", "Left Shift", 0),
        new("transform", "Transform", "L1. Transform the trick weapon.", "C", 3),
        new("firearm", "Firearm / Parry", "L2. Shoot the left-hand weapon.", "X", 2),
        new("blood_vial", "Blood Vial", "Triangle. Restore HP.", "R", 0),
        new("use_item", "Use Item", "Square. Use the quick item.", "F", 0),
        new("interact", "Interact", "Cross. Talk, examine, pick up, open.", "E", 0),
        new("switch_item", "Switch Item", "D-pad down.", "Down", 0),
        new("switch_right", "Switch Right Weapon", "D-pad right.", "Right", 0),
        new("switch_left", "Switch Left Weapon", "D-pad left.", "Left", 0),
        new("dpad_up", "D-pad Up", "D-pad up.", "Up", 0),
        new("gestures", "Gestures", "The touchpad's left side.", "G", 0),
        new("personal_effects", "Personal Effects", "The touchpad's right side.", "T", 0),
        new("look_up", "Look Up", "Right stick up.", "I", 0),
        new("look_down", "Look Down", "Right stick down.", "K", 0),
        new("look_left", "Look Left", "Right stick left.", "J", 0),
        new("look_right", "Look Right", "Right stick right.", "L", 0),
        new("menu", "Menu", "Options. Open the menu.", "Escape", 0),
        new("confirm", "Menu Confirm", "OK, in menus.", "Return", 0),
        new("back", "Menu Back", "Back, in menus.", "Backspace", 0),
        new("l3", "Left Stick Press", "L3.", "Left Ctrl", 0),
    };

    public static readonly string[] MouseNames = { "", "Mouse1", "Mouse2", "Mouse3", "Mouse4", "Mouse5", "WheelUp", "WheelDown" };
    public static readonly string[] MouseLabels = { "", "Left button", "Right button", "Middle button", "Button 4", "Button 5", "Wheel up", "Wheel down" };

    /// The action's key and mouse input as saved (the defaults when not).
    public static (string? Key, int Mouse) Read(OptionsFile o, Action a)
    {
        var v = o.GetIn("keys", a.Key);
        if (v == null) return (a.DefaultKey, a.DefaultMouse);
        string? key = null;
        int mouse = 0;
        foreach (var part in v.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            int m = Array.IndexOf(MouseNames, part);
            if (m > 0) mouse = m;
            else key = part;
        }
        return (key, mouse);
    }

    public static void Write(OptionsFile o, Action a, string? key, int mouse)
    {
        var parts = new List<string>();
        if (!string.IsNullOrEmpty(key)) parts.Add(key);
        if (mouse > 0) parts.Add(MouseNames[mouse]);
        o.SetIn("keys", a.Key, string.Join(", ", parts));
    }

    /// SDL's scancode name for a physical key (the engine reads it with SDL_GetScancodeFromName).
    public static string? SdlName(PhysicalKey k)
    {
        var n = k.ToString();
        if (n.StartsWith("Key") && n.Length == 4) return n[3..];
        if (n.StartsWith("Digit") && n.Length == 6) return n[5..];
        if (n.StartsWith("F") && int.TryParse(n[1..], out var f) && f is >= 1 and <= 24) return n;
        if (n.StartsWith("NumPad") && n.Length == 7 && char.IsDigit(n[6])) return "Keypad " + n[6];
        return k switch
        {
            PhysicalKey.Space => "Space", PhysicalKey.Enter => "Return", PhysicalKey.Escape => "Escape",
            PhysicalKey.Backspace => "Backspace", PhysicalKey.Tab => "Tab", PhysicalKey.CapsLock => "CapsLock",
            PhysicalKey.ShiftLeft => "Left Shift", PhysicalKey.ShiftRight => "Right Shift",
            PhysicalKey.ControlLeft => "Left Ctrl", PhysicalKey.ControlRight => "Right Ctrl",
            PhysicalKey.AltLeft => "Left Alt", PhysicalKey.AltRight => "Right Alt",
            PhysicalKey.ArrowUp => "Up", PhysicalKey.ArrowDown => "Down", PhysicalKey.ArrowLeft => "Left", PhysicalKey.ArrowRight => "Right",
            PhysicalKey.Insert => "Insert", PhysicalKey.Delete => "Delete", PhysicalKey.Home => "Home", PhysicalKey.End => "End",
            PhysicalKey.PageUp => "PageUp", PhysicalKey.PageDown => "PageDown",
            PhysicalKey.Backquote => "`", PhysicalKey.Minus => "-", PhysicalKey.Equal => "=",
            PhysicalKey.BracketLeft => "[", PhysicalKey.BracketRight => "]", PhysicalKey.Backslash => "\\",
            PhysicalKey.Semicolon => ";", PhysicalKey.Quote => "'", PhysicalKey.Comma => ",", PhysicalKey.Period => ".", PhysicalKey.Slash => "/",
            PhysicalKey.NumPadEnter => "Keypad Enter", PhysicalKey.NumPadAdd => "Keypad +", PhysicalKey.NumPadSubtract => "Keypad -",
            PhysicalKey.NumPadMultiply => "Keypad *", PhysicalKey.NumPadDivide => "Keypad /", PhysicalKey.NumPadDecimal => "Keypad .",
            _ => null,
        };
    }
}
