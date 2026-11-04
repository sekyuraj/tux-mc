package dev.tux.mc;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The desktop theme (~/.config/themes/current/theme.sh), re-read when it changes.
 * Without one it falls back to the black palette. ARGB ints.
 */
public final class Theme {
    public static int bg, bgAlt, surface, selection, muted, dim, subtext, fg, fgBright,
            accent, accentBright, urgent, warn;
    public static String name = "default";

    private static final Path FILE = Path.of(System.getProperty("user.home"), ".config/themes/current/theme.sh");
    private static final Pattern LINE = Pattern.compile("^\\s*([a-z_]+)=\"?#([0-9a-fA-F]{6})\"?");
    private static long stamp = -1;

    static { reload(); }

    /** cheap: only re-parses when the file's mtime moved (theme switch) */
    public static void reload() {
        long m;
        try {
            m = Files.getLastModifiedTime(FILE.toRealPath()).toMillis();
        } catch (Exception e) {
            m = 0;
        }
        if (m == stamp)
            return;
        stamp = m;
        Map<String, Integer> v = new HashMap<>();
        try {
            for (String l : Files.readAllLines(FILE)) {
                Matcher x = LINE.matcher(l);
                if (x.find())
                    v.put(x.group(1), 0xFF000000 | Integer.parseInt(x.group(2), 16));
                else if (l.startsWith("name="))
                    name = l.substring(5).replace("\"", "").trim();
            }
        } catch (Exception ignored) {
        }
        bg = v.getOrDefault("bg", 0xFF000000);
        bgAlt = v.getOrDefault("bg_alt", 0xFF0A0A0A);
        surface = v.getOrDefault("surface", 0xFF1A1A1A);
        selection = v.getOrDefault("selection", 0xFF333333);
        muted = v.getOrDefault("muted", 0xFF4A4A4A);
        dim = v.getOrDefault("dim", 0xFF707070);
        subtext = v.getOrDefault("subtext", 0xFFA6A6A6);
        fg = v.getOrDefault("fg", 0xFFFFFFFF);
        fgBright = v.getOrDefault("fg_bright", 0xFFFFFFFF);
        accent = v.getOrDefault("accent", 0xFFFFFFFF);
        accentBright = v.getOrDefault("accent_bright", 0xFFFFFFFF);
        urgent = v.getOrDefault("urgent", 0xFFFFFFFF);
        warn = v.getOrDefault("warn", 0xFFD9D9D9);
        Capes.themeChanged();
    }

    public static int alpha(int argb, float a) {
        int al = Math.round(((argb >>> 24) & 0xFF) * Math.max(0, Math.min(1, a)));
        return (al << 24) | (argb & 0xFFFFFF);
    }

    public static int mix(int a, int b, float t) {
        t = Math.max(0, Math.min(1, t));
        int r = 0;
        for (int s = 0; s <= 24; s += 8) {
            int ca = (a >>> s) & 0xFF, cb = (b >>> s) & 0xFF;
            r |= Math.round(ca + (cb - ca) * t) << s;
        }
        return r;
    }

    private Theme() {}
}
