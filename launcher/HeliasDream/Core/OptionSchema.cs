using System.Linq;

namespace HeliasDream.Core;

/// <summary>
/// One setting of HeliasDream-options.toml: its key, the values the engine accepts (the same lists as
/// src/host/options.cpp's g_set, which the F10 menu shows), the default, and its help line.
/// </summary>
public sealed record OptionDef(string Key, string Label, string[] Values, int Default, string Help, bool Restart = false)
{
    public string DefaultValue => Values[Default];
}

public sealed record OptionGroup(string Title, OptionDef[] Options);

public static class OptionSchema
{
    static readonly string[] OnOff = { "On", "Off" };
    static readonly string[] OffOn = { "Off", "On" };
    static readonly string[] Zero10 = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };
    /// "lo" .. "hi" in ones, as the engine's number_labels (HDR's nits).
    static string[] Range(int lo, int hi) => System.Linq.Enumerable.Range(lo, hi - lo + 1).Select(v => v.ToString()).ToArray();

    public static readonly OptionDef Resolution = new("resolution", "Resolution",
        new[] { "1280x720", "1600x900", "1920x1080", "2560x1440", "3200x1800", "3840x2160", "2560x1080", "3440x1440",
                "5120x2160", "3840x1080", "5120x1440", "1280x800", "960x600", "1024x640" }, 2,
        "What the game renders at. The largest sizes can need a restart.", true);
    public static readonly OptionDef FrameCap = new("frame_cap", "Frame rate", new[] { "30", "60", "90", "120", "Uncapped" }, 0,
        "30 is the game's own pace. 60 and 90 run its logic at that rate; 120 steps it by the frame time.", true);
    public static readonly OptionDef Upscaler = new("upscaler", "Upscaler", new[] { "Off", "FSR 1", "FSR 3.1", "FSR 4.1.1", "Native TAA", "FSR 4", "DLSS" }, 1,
        "FSR 4.1.1 replays AMD's upscaler from assets built once from your own DLL; without them it is FSR 3.1.");
    public static readonly OptionDef Quality = new("upscale_quality", "Upscaling quality",
        new[] { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" }, 0,
        "The share of the resolution the 3D scene renders at. Menus and text stay sharp.");
    public static readonly OptionDef Sharpness = new("upscale_sharpness", "Upscaling sharpness",
        new[] { "0.0", "0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0" }, 8,
        "How much FSR 3.1, FSR 4 and FSR 4.1.1 sharpen. 0 turns it off.");

    /// Render scale per quality preset (output / render), as the engine uses them.
    public static double ScaleOf(string quality) => quality switch
    {
        "Quality" => 1.5, "Balanced" => 1.7, "Performance" => 2.0, "Ultra Performance" => 3.0, _ => 1.0,
    };

    /// The longer explanation each setting shows on hover, after the engine's own help
    /// (src/host/options.cpp), in plain words.
    public static readonly System.Collections.Generic.Dictionary<string, string> Tips = new()
    {
        ["resolution"] = "What the game renders at, before any upscaling. 1920x1080 is what it shipped with. Wide (21:9, 32:9) and 16:10 screens are filled, with the HUD kept 16:9. The largest sizes can need a restart, because their memory is set aside when the game starts.",
        ["live_resolution"] = "On: changing Resolution in the game applies right away. To allow that, memory and display buffers for your largest display's size are set aside when the game starts, and cleared every frame. Off sizes everything for the resolution you start at, so it is a little lighter when the game runs below your display's size (no difference when it runs at it); a new resolution then applies at the next start.",
        ["menu_key"] = "The key that opens the in-game settings menu while you play. F10 always works as well, so you cannot lock yourself out. On a controller, the PS (Home, Xbox) button or L3 + R3 clicked together opens and closes it; L1 / R1 switch tabs, the D-pad or left stick moves and changes, Cross toggles and Circle closes.",
        ["fps_counter"] = "Shows the FPS counter in the game. Its look and what it shows are set below; F10 in the game has the same settings.",
        ["fps_position"] = "Which corner of the screen the FPS counter sits in, or the top centre.",
        ["fps_size"] = "How large the FPS counter's text is, from 75% to twice the default.",
        ["fps_colour"] = "The colour of the FPS counter's text.",
        ["fps_opacity"] = "How solid the FPS counter's text is; lower lets the game show through.",
        ["fps_background"] = "The colour of the rounded box behind the FPS counter.",
        ["fps_background_opacity"] = "How solid the box behind the FPS counter is. 0% hides the box and leaves only the text.",
        ["fps_border"] = "A thin outline round the FPS counter's box, in the text colour.",
        ["fps_show_rate"] = "Frames per second actually shown on screen.",
        ["fps_show_frame_time"] = "How long a frame takes, in milliseconds (8.3 ms is 120 fps).",
        ["fps_show_upscaler"] = "The upscaler and quality preset running, for example FSR 4.1.1 Performance.",
        ["fps_show_cpu"] = "How much of the whole CPU the game uses; 100% would be every core busy.",
        ["fps_show_gpu"] = "How busy the game keeps the GPU. Near 100% the GPU is the limit; well below, the CPU is.",
        ["hdr"] = "Shows the game on an HDR screen in HDR10: the picture as it is, with its brightest parts (lamps, fire, the moon) brighter than white can be in SDR. Turn HDR on for that monitor first in your desktop's display settings (KDE: System Settings > Display Configuration). Can be switched while playing, from the F10 menu: the screen blinks once as the picture is remade.",
        ["hdr_paper_white"] = "How bright the game's white is on the HDR screen, in nits. Around 200 suits a dim room; raise it in a bright one. Changes live.",
        ["hdr_peak_auto"] = "On: the brightest the display says it can show (from its EDID), as KDE also reads it. Off: the Peak brightness setting below.",
        ["hdr_peak"] = "Your display's peak brightness in nits, used when it is not taken from the display. Nothing in the game goes above it.",
        ["hdr_ui_brightness"] = "How bright the game's HUD, menus and subtitles (and the in-game menu and FPS counter) are on an HDR screen, as a share of paper white, kept out of the highlight boost so white text never glares. Like the picture: they are boosted with everything else, as before. The UI is told apart from the picture each frame by comparing the frame before and after its UI is drawn.",
        ["hdr_boost"] = "How far the brightest parts of the picture rise above paper white, towards the peak. Off keeps the SDR picture exactly, only on an HDR screen; Max uses the display's whole range.",
        ["brightness"] = "The picture's brightness, applied to the game's finished image (menus and the FPS counter are left as they are). 5 is the game's own; each step is about 10% lighter or darker.",
        ["contrast"] = "The picture's contrast around mid grey. 5 is the game's own; higher deepens shadows and brightens highlights.",
        ["hue"] = "Turns every colour of the picture round the colour wheel by this many degrees. 0 is the game's own.",
        ["window_mode"] = "Fullscreen is a borderless window over the whole display at the desktop resolution: no mode switch when you alt-tab, and overlays and pop-ups (trophies, the FPS counter, Steam, Discord) show over it. Windowed keeps the title bar.",
        ["vsync"] = "On waits for the display before showing a frame (no tearing). Off shows frames as soon as they are ready, which can tear. With a variable refresh rate (FreeSync, G-Sync) monitor either works well; the Presentation mode setting decides exactly how frames are delivered.",
        ["frame_cap"] = "The game's pace. 30 is how it shipped. 60 and 90 run the game's logic at that fixed rate. 120 and Uncapped step it by the real frame time, so the game keeps real speed if the frame rate dips. 120 never goes above 120 fps; Uncapped follows the display (up to 240 with V-Sync off). Physics and timings are made for these rates; above 120 is less tested.",
        ["fps_counter"] = "Frames per second actually shown on screen, in the top right corner.",
        ["fov"] = "How wide the camera sees. +0% is the game's own (about 43 degrees up and down); each step widens it 5%. +50% is about 97 degrees across.",
        ["camera_distance"] = "How far behind your character the camera sits. 5 is the game's own; each step is 10% nearer or farther.",
        ["camera_height"] = "How high above your character the camera looks. 5 is the game's own.",
        ["upscaler"] = "DLSS: NVIDIA's upscaler (preset K, M for Performance, L for Ultra Performance, as Lantern Engine picks them), on an NVIDIA RTX GPU with its driver; choosing it takes effect at the next start, and elsewhere FSR 3.1 stands in. Native TAA: temporal anti-aliasing at the full resolution (FSR 3.1's Native AA mode), smoother edges and less shimmer than the game's own AA, nothing upscaled; Quality does not apply. FSR 4: AMD's FSR 4 machine-learning upscaler (its INT8 model, from the source AMD published), sharper than FSR 3.1 with steadier fine detail; it needs a one-time download of its shaders and runs on any GPU with INT8 dot products (RX 6000 or newer, RTX 20 or newer, Arc). FSR 4.1.1: AMD's newest upscaler, the sharpest; it needs assets built once from your own copy of AMD's DLL, and falls back to FSR 3.1 without them. FSR 3.1: temporal, uses motion vectors, works on any GPU. FSR 1: simple and light, no motion vectors. Off renders at the full resolution.",
        ["upscale_quality"] = "How much smaller the 3D scene renders before it is upscaled: Native AA 100% (just anti-aliasing), Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%. Lower is faster. Menus, text and the HUD always stay at the full resolution. Changes apply live.",
        ["upscale_sharpness"] = "How much FSR 3.1, FSR 4 and FSR 4.1.1 sharpen the upscaled picture. 0 turns sharpening off; 0.8 matches FSR 1's default. FSR 1 keeps its own sharpening.",
        ["upscale_reactive"] = "Marks particles, fog, Blood Echoes, the lantern's glow and other transparent effects for the upscaler, so they do not smear or leave ghost trails. Leave on; off is for comparison.",
        ["upscale_jitter"] = "The 3D scene shifts by a fraction of a pixel each frame so the upscaler can rebuild fine detail. Off gives a softer, more aliased picture; it is for comparison only.",
        ["anti_alias"] = "The game's own anti-aliasing pass. Auto turns it off while FSR 4.1.1, FSR 4 or DLSS upscales below Native AA, and with Native TAA (they smooth edges themselves, and the pass costs about 0.35 ms at 4K), and on otherwise. FSR 3.1 and FSR 1 keep it on.",
        ["ssao"] = "Screen-space ambient occlusion: soft shadows in corners and where objects meet. Off is faster and flatter.",
        ["motion_blur"] = "The blur on fast camera and character movement. Off also saves a little GPU time.",
        ["depth_of_field"] = "Blurs distant scenery, as the game does in some views. Indoors there is rarely anything far enough to blur.",
        ["chromatic_aberration"] = "The colour fringing at the edges of the frame, like an old lens.",
        ["vignette"] = "The darkened frame edges some areas use. Most areas have none, so Off often changes nothing.",
        ["bloom"] = "The glow around bright light. 10 is the area's own; lower dims it, 0 removes it.",
        ["ao_strength"] = "How dark ambient occlusion makes corners and contact shadows. 5 is the game's own; 0 none, 10 twice as dark.",
        ["saturation"] = "Colour intensity. 5 is the game's own; 0 is black and white, 10 twice as vivid.",
        ["fog"] = "How thick distance and height fog are. 10 is the area's own; 0 clears it.",
        ["shadow_distance"] = "How far the sun and moon cast shadows. Farther spreads each shadow map wider, so shadows nearby get a little softer.",
        ["model_detail"] = "Normal is the game's own. Low and Lowest swap in simpler models sooner (faster); Full always uses the finest models.",
        ["haptics"] = "HD needs an HD-haptics controller: a DualSense or DualSense Edge plugged in by USB. It plays the game's rumble and its sound effects (hits, gunshots, footsteps, doors) on its haptic motors, with music and ambience filtered out. Rumble: classic vibration. Over Bluetooth, or with another controller, HD is Rumble.",
        ["haptics_strength"] = "How strong HD haptics are. 5 is the default, 0 nothing, 10 twice as strong.",
        ["adaptive_triggers"] = "DualSense and DualSense Edge: L2 (your gun) has a trigger break that goes slack when you are out of Quicksilver Bullets; R2 resists on a heavy attack's wind-up, rumbles as it charges and kicks on release. Relaxed in menus and loading screens.",
        ["trigger_strength"] = "How hard the adaptive triggers push back.",
        ["menu_haptics"] = "HD haptics: a light tap as the cursor moves through the title menu, pause menu, inventory and System menu, and a firmer one on confirm and back.",
        ["mouse_camera"] = "The mouse turns the camera whenever no menu is open. Off leaves the camera to the controller.",
        ["mouse_sens"] = "How far a given mouse movement turns the camera. 5 is the default.",
        ["mouse_invert_x"] = "Reverses the mouse's horizontal camera movement.",
        ["mouse_invert_y"] = "Reverses the mouse's vertical camera movement.",
        ["mouse_auto_rotation"] = "Off: while you use the mouse, the camera stays where you put it instead of swinging behind you as you move. A controller always gets the game's own camera.",
        ["draw_cursor"] = "Shows a mouse pointer over the game's menus. The game has none of its own, so the engine draws it; turn it off if you play with a controller and the pointer is in the way.",
        ["mouse_menu"] = "In menus: click confirms, right-click goes back, the wheel moves the cursor. A click only reaches menus while one is open, so it never swings a weapon.",
        ["pad_icons"] = "The button icons the game's prompts show while you play with a controller. Auto: the game's own PlayStation icons with a PlayStation controller, and the Xbox buttons by name ([A], [RB], D-pad Up) with an Xbox or any other controller. The world's \"Examine\" prompts drawn on the HUD keep the PlayStation icons for now.",
        ["submit_split"] = "Experimental, untested on real hardware. Every N draws (Off, 32, 64, 128, 256), if the graphics card has finished everything it was given, the work recorded so far is handed to it at once instead of at the end of the game's batch. It aims at the card's idle gaps while it waits for the CPU: up to 80-120 ms a second (8-12%) were measured where the card had time to spare. Expected: at best a few % more FPS where the CPU holds the frame back; nothing when the card itself is the limit, since then it never runs dry and nothing is split. Each split is one more submission (about 27 µs on the card), so a small N can cost a little instead. May add stutter, and flashing or black frames would mean it does not work on your system: turn it off then. Changes live; HELIAS_SUBMIT_SPLIT wins when set.",
        ["thread_priority"] = "Experimental, from Lantern Engine. The engine's graphics threads (command processors, recorder, submission, presenter) and the game's main and render threads run at a higher CPU priority than its other threads (nice -5, the frame clock -10), so a frame isn't held up behind loading or audio work. Expected: steadier frame times where the CPU is busy; nothing where the graphics card is the limit. Linux only lets a program do this when the system allows it (ulimit -e above 20; CachyOS and many desktops do): otherwise the log says so and nothing changes. On Windows these threads already run at a raised priority. Changes live.",
        ["fsr4_sharp_motion"] = "Experimental, from Lantern Engine. With FSR 4 at Performance or Ultra Performance, it uses FSR 4's balanced model instead of the one made for that ratio: fewer trails and less smearing behind moving characters, for a little more GPU time. No effect at Native AA, Quality or Balanced, or with other upscalers. Changes live: FSR 4 is rebuilt in the background, with FSR 3.1 standing in for a moment.",
        ["compile_note"] = "Shows \"Building shaders...\" under the FPS counter's corner while the game builds a shader for something it shows for the first time, so a brief stutter then reads as what it is. Built shaders are kept, so it shows less and less.",
        ["menu_language"] = "The language of the in-game settings menu (F10): its tabs, settings, values and notes. Changes live. The game's own text follows Game language.",
        ["game_language"] = "The language of the game's own text, menus and subtitles, from the message bundles on your disc. Takes effect at the next start. The in-game settings menu (F10) is in English.",
        ["button_prompts"] = "What the game's own button prompts and tutorial notes show. Auto follows the device you used last, keyboard or controller.",
        ["trophy_popups"] = "When you earn a trophy, a frosted glass card slides into the top right corner with its medal, its name and your progress, and a DualSense gives a firm tap. The platinum is awarded once every base-game trophy is earned.",
        ["mip_bias"] = "How sharp textures stay at a distance. With an upscaler the scene renders smaller, so the game picks blurrier texture levels sooner; Auto puts back what the render scale took (Performance: one level sharper), as AMD recommends for FSR. 0.0 is the game's own. Sharper can shimmer on fine patterns. Changes live.",
        ["skip_logos"] = "Skips the three company logos before the title screen, and the warning shown when the last session did not end with Exit Game.",
        ["change_appearance"] = "The Hunter's Dream mirror opens the character appearance editor (the game's unused \"Put on Disguise\").",
        ["rebirth"] = "With the Yharnam Stone, the Altar of Despair resets your level and attributes so you can rebuild your character.",
        ["five_players"] = "Allows two invaders in Mensis, Mergo's Loft, the Frontier and the DLC. Every player in the session needs it on.",
        ["debug_camera"] = "Lance McDonald's restored debug camera: hold Interact (E) and press L3 (Left Ctrl) to cycle the free camera's modes. The debug menu's load tests crash while it is on.",
        ["debug_motion_vectors"] = "Draws the scene's motion vectors over the picture: red and green for sideways and up-down movement, blue where a moving object moved a pixel. For checking the upscaler.",
        ["debug_upscaler_time"] = "Shows the upscaler's and the presenter's GPU time under the FPS counter, and logs it every 10 seconds.",
    };

    // The Display page (Lantern Engine's, in this launcher's layout): the screen, its
    // advanced switches, the frame rate, the FPS counter and HDR. The values are the
    // engine's own (src/host/options.cpp).
    public static readonly OptionDef[] Screen =
    {
        Resolution,
        new("window_mode", "Window mode", new[] { "Windowed", "Fullscreen" }, 0, "Fullscreen is borderless: no mode switch."),
        new("vsync", "V-Sync", OnOff, 0, "Off presents as soon as a frame is ready, and can tear."),
    };
    public static readonly OptionDef LiveResolution = new("live_resolution", "Live resolution change", OnOff, 0,
        "On: a new resolution applies in the game. Off: at the next start.", true);
    public static readonly OptionDef MenuKey = new("menu_key", "Menu key", new[] { "F10", "Insert", "Home", "F1" }, 0,
        "What opens the in-game settings menu. F10 always works too.");
    public static readonly OptionDef FpsShow = new("fps_counter", "FPS counter", OffOn, 0, "Frames per second, in the corner you choose.");
    public static readonly OptionDef FpsPosition =
        new("fps_position", "Position", new[] { "Top right", "Top left", "Bottom right", "Bottom left", "Top centre" }, 0, "Where the counter sits.");
    public static readonly OptionDef CompileNote = new("compile_note", "Building shaders note", OnOff, 0,
        "\"Building shaders...\" under the counter while the game builds them.");
    public static readonly OptionDef GameLanguage = new("game_language", "Game language",
        new[] { "English", "French", "Spanish", "German", "Italian", "Russian", "Japanese" }, 0, "The game's own text and voices.", true);
    public static readonly OptionDef MenuLanguage = new("menu_language", "Menu language",
        new[] { "English", "Русский", "Español", "Português (Brasil)", "Français", "Deutsch", "Italiano", "Polski", "Türkçe" }, 0, "The in-game settings menu (F10).");
    public static readonly OptionDef[] FpsLook =
    {
        new("fps_size", "Size", new[] { "75%", "100%", "125%", "150%", "175%", "200%" }, 1, "How large its text is."),
        new("fps_colour", "Text colour", new[] { "White", "Yellow", "Green", "Cyan", "Red", "Orange", "Pink", "Black" }, 0, "The counter's text colour."),
        new("fps_opacity", "Text opacity", new[] { "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, 9, "How solid the text is."),
        new("fps_background", "Background", new[] { "Dark grey", "Black", "Dark red", "Dark blue", "White" }, 0, "The box behind the counter."),
        new("fps_background_opacity", "Background opacity", new[] { "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" }, 6, "0% hides the box."),
        new("fps_border", "Border", OffOn, 0, "A thin outline round the box."),
    };
    public static readonly OptionDef[] FpsParts =
    {
        new("fps_show_rate", "Show frame rate", OnOff, 0, "Frames per second actually shown."),
        new("fps_show_frame_time", "Show frame time", OffOn, 0, "Milliseconds a frame."),
        new("fps_show_upscaler", "Show upscaler", OffOn, 0, "The upscaler and preset running."),
        new("fps_show_cpu", "Show CPU usage", OffOn, 0, "How much of the CPU the game uses."),
        new("fps_show_gpu", "Show GPU usage", OnOff, 0, "How busy the game keeps the GPU."),
    };
    public static readonly OptionDef[] Hdr =
    {
        new("hdr", "HDR output", OffOn, 0, "The picture on an HDR screen in HDR10, with brighter highlights."),
        new("hdr_paper_white", "Paper white", Range(10, 500), 190, "How bright the game's white is, in nits (10 to 500)."),
        new("hdr_peak_auto", "Peak from the display", OnOff, 0, "The peak the display reports. Off: the value below."),
        new("hdr_peak", "Peak brightness", Range(10, 4000), 990,
            "Your display's peak, in nits (10 to 4000), when not taken from the display."),
        new("hdr_boost", "Highlight boost", new[] { "Off", "Low", "Medium", "High", "Max" }, 1,
            "Off keeps the game's own look; higher makes lamps, fire and the sky brighter."),
        new("hdr_ui_brightness", "UI brightness",
            new[] { "Like the picture", "50%", "60%", "70%", "80%", "90%", "100%", "110%", "120%", "130%", "140%", "150%", "160%", "170%", "180%", "190%", "200%" }, 0,
            "The HUD, menus and subtitles against paper white, kept out of the boost."),
    };

    public static readonly OptionGroup[] UpscalingExtras =
    {
        new("Fine tuning", new[]
        {
            new OptionDef("upscale_reactive", "Upscaler reactive mask", OffOn, 1, "Keeps fog, particles and the lantern's glow from smearing."),
            new OptionDef("upscale_jitter", "Upscaler jitter", OffOn, 1, "Lets the upscaler rebuild fine detail."),
            new OptionDef("anti_alias", "Game anti-aliasing", new[] { "On", "Off", "Auto" }, 2, "Auto: off while FSR 4.1.1, FSR 4, DLSS or Native TAA smooths edges, on otherwise."),
            new OptionDef("mip_bias", "Texture detail", new[] { "Auto", "-2.0", "-1.5", "-1.0", "-0.5", "0.0", "+0.5", "+1.0" }, 5,
                "Auto matches the upscaler; below 0 sharper textures, above softer."),
        }),
    };

    public static readonly OptionGroup[] Graphics =
    {
        new("Effects", new[]
        {
            new OptionDef("ssao", "Ambient occlusion", OnOff, 0, "Off is faster."),
            new OptionDef("motion_blur", "Motion blur", OnOff, 0, "The blur on fast camera and character movement."),
            new OptionDef("depth_of_field", "Depth of field", OnOff, 0, "Blurs distant scenery."),
            new OptionDef("chromatic_aberration", "Chromatic aberration", OnOff, 0, "The colour fringing at the frame's edges."),
            new OptionDef("vignette", "Vignette", OnOff, 0, "Darkened edges, in the few areas that use one."),
            new OptionDef("model_detail", "Model detail", new[] { "Full", "Normal", "Low", "Lowest" }, 1, "Normal is the game's own."),
            new OptionDef("shadow_distance", "Shadow distance",
                new[] { "x1.0", "x1.2", "x1.4", "x1.6", "x1.8", "x2.0", "x2.2", "x2.4", "x2.6", "x2.8", "x3.0" }, 0, "How far shadows reach."),
        }),
        new("Picture", new[]
        {
            new OptionDef("brightness", "Brightness", Zero10, 5, "5 is the game's own."),
            new OptionDef("contrast", "Contrast", Zero10, 5, "5 is the game's own."),
            new OptionDef("saturation", "Saturation", Zero10, 5, "5 is the game's own; 0 is black and white."),
            new OptionDef("hue", "Hue shift", new[] { "-180", "-165", "-150", "-135", "-120", "-105", "-90", "-75", "-60", "-45", "-30", "-15", "0", "15", "30", "45", "60", "75", "90", "105", "120", "135", "150", "165", "180" }, 12, "Degrees round the colour wheel; 0 is the game's own."),
            new OptionDef("bloom", "Bloom", Zero10, 10, "The glow around bright light. 10 is the game's own."),
            new OptionDef("ao_strength", "AO strength", Zero10, 5, "5 is the game's own."),
            new OptionDef("fog", "Fog", Zero10, 10, "10 is the game's own; 0 clears it."),
        }),
        new("Camera", new[]
        {
            new OptionDef("fov", "Field of view", new[] { "+0%", "+5%", "+10%", "+15%", "+20%", "+25%", "+30%", "+35%", "+40%", "+45%", "+50%" }, 0,
                "How wide the camera sees. +0% is the game's own."),
            new OptionDef("camera_distance", "Camera distance", Zero10, 5, "5 is the game's own."),
            new OptionDef("camera_height", "Camera height", Zero10, 5, "5 is the game's own."),
        }),
    };

    public static readonly OptionGroup[] Controller =
    {
        new("Gamepad", new[]
        {
            new OptionDef("pad_icons", "Controller icons", new[] { "Auto", "PlayStation", "Xbox" }, 0, "Auto: PlayStation's with a PlayStation pad, Xbox buttons with any other."),
            new OptionDef("haptics", "Haptics", new[] { "Off", "Rumble", "HD" }, 2, "HD needs a DualSense or DualSense Edge on USB; other pads rumble."),
            new OptionDef("haptics_strength", "Haptics strength", Zero10, 5, "5 is the default; 10 is twice as strong."),
            new OptionDef("adaptive_triggers", "Adaptive triggers", OnOff, 0, "L2's trigger break and R2's charge resistance."),
            new OptionDef("trigger_strength", "Trigger strength", new[] { "Light", "Medium", "Strong" }, 1, "How hard the triggers push back."),
            new OptionDef("menu_haptics", "Menu haptics", OnOff, 0, "A light tap as the cursor moves through menus."),
        }),
        new("Mouse and keyboard", new[]
        {
            new OptionDef("mouse_camera", "Mouse camera", OnOff, 0, "The mouse turns the camera outside menus."),
            new OptionDef("mouse_sens", "Mouse sensitivity", Zero10, 5, "How far a mouse movement turns the camera."),
            new OptionDef("mouse_invert_x", "Invert mouse X", OffOn, 0, "Reverses horizontal camera movement."),
            new OptionDef("mouse_invert_y", "Invert mouse Y", OffOn, 0, "Reverses vertical camera movement."),
            new OptionDef("mouse_auto_rotation", "Mouse auto-rotation", OffOn, 0, "Off: the camera stays where you put it."),
            new OptionDef("mouse_menu", "Mouse in menus", OffOn, 1, "Click confirms, right-click goes back."),
            new OptionDef("draw_cursor", "Draw the pointer", OnOff, 0, "The game has no cursor of its own: the engine draws it."),
            new OptionDef("button_prompts", "Button prompts", new[] { "Auto", "Keyboard", "Controller" }, 0, "Auto follows the device you last used."),
        }),
    };

    /// More > Performance (experimental): engine switches that are not measured enough to be on
    /// by default. The card shows a warning line above them (Pages.More).
    public static readonly OptionGroup Experimental = new("Performance (experimental)", new[]
    {
        new OptionDef("submit_split", "Submit split", new[] { "Off", "32", "64", "128", "256" }, 0,
            "Every N draws, give an idle GPU the work recorded so far."),
        new OptionDef("thread_priority", "Graphics threads first", new[] { "Off", "On" }, 0,
            "Graphics threads at a higher CPU priority than the game's other threads."),
        new OptionDef("fsr4_sharp_motion", "Sharper motion (FSR 4)", new[] { "Off", "On" }, 0,
            "FSR 4's balanced model at Performance and Ultra Performance."),
    });

    public static readonly OptionGroup[] More =
    {
        new("Game", new[]
        {
            new OptionDef("skip_logos", "Skip company logos", OffOn, 0, "And the warning after a session that did not end with Exit Game.", true),
            new OptionDef("trophy_popups", "Trophy pop-ups", OnOff, 0, "A card in the corner when you earn a trophy."),
            new OptionDef("change_appearance", "Hunter's Dream mirror", OnOff, 0, "The Dream's mirror opens the appearance editor.", true),
            new OptionDef("rebirth", "Rebirth at the Altar", OnOff, 0, "With the Yharnam Stone, the Altar of Despair resets your level.", true),
            new OptionDef("five_players", "Five players", OnOff, 0, "Two invaders in some areas; all players need it.", true),
        }),
        new("Debug", new[]
        {
            new OptionDef("debug_camera", "Debug camera", OffOn, 0, "Hold Interact and press L3 to cycle the free camera.", true),
            new OptionDef("debug_motion_vectors", "Show motion vectors", OffOn, 0, "Draws the scene's motion vectors over the picture."),
            new OptionDef("debug_upscaler_time", "Show upscaler GPU time", OffOn, 0, "Under the FPS counter, and in the log."),
        }),
    };
}
