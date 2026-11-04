package dev.tux.mc;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

import com.mojang.blaze3d.platform.NativeImage;

import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.core.ClientAsset;
import net.minecraft.resources.Identifier;

/**
 * Client-side capes, painted by libtuxmc (native/capes.c) at 8x the vanilla
 * 64x32 layout. Only you see them; others see your Mojang cape.
 */
public final class Capes {
    public static final int S = 8;

    /** id -> display name, in menu order */
    public static final Map<String, String> ALL = new LinkedHashMap<>();
    static {
        ALL.put("theme", "Theme");
        ALL.put("tux", "Tux");
        ALL.put("gentoo", "Gentoo");
        ALL.put("stars", "Starfield");
        ALL.put("aurora", "Aurora");
        ALL.put("ember", "Ember");
        ALL.put("ocean", "Ocean");
        ALL.put("synth", "Synthwave");
        ALL.put("image", "Picture");
    }

    private static final Map<String, ClientAsset.Texture> made = new HashMap<>();
    private static final Map<String, int[]> previews = new HashMap<>();
    private static volatile boolean themeDirty;
    private static String imageKey = "";
    private static long imageChecked;

    public static void themeChanged() { themeDirty = true; }

    /** null = no custom cape (official / none) */
    public static ClientAsset.Texture current() {
        return ALL.containsKey(Config.cape) ? get(Config.cape) : null;
    }

    private static void invalidate() {
        if (themeDirty) {
            themeDirty = false;
            made.remove("theme");
            previews.remove("theme");
        }
        long now = System.currentTimeMillis();
        if (now - imageChecked < 2000)
            return;
        imageChecked = now;
        String k = Config.capeImage + "@" + mtime(Config.capeImage);
        if (!k.equals(imageKey)) {
            imageKey = k;
            made.remove("image");
            previews.remove("image");
        }
    }

    public static ClientAsset.Texture get(String id) {
        invalidate();
        if (made.containsKey(id))
            return made.get(id);
        ClientAsset.Texture t = make(id);
        made.put(id, t);
        return t;
    }

    private static ClientAsset.Texture make(String id) {
        int[] px = paint(id, S);
        if (px == null)
            return null;
        int w = 64 * S, h = 32 * S;
        NativeImage img = new NativeImage(w, h, true);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                img.setPixel(x, y, px[y * w + x]);
        Identifier loc = Identifier.fromNamespaceAndPath("tuxmc", "capes/" + id);
        Minecraft.getInstance().getTextureManager().register(loc, new DynamicTexture(() -> "tuxmc cape " + id, img));
        return new ClientAsset.ResourceTexture(loc, loc);
    }

    /** the outside face as 10x16 ARGB, for menu previews */
    public static int[] preview(String id) {
        invalidate();
        return previews.computeIfAbsent(id, k -> {
            int[] px = paint(k, 1);
            if (px == null)
                return null;
            int[] f = new int[160];
            for (int y = 0; y < 16; y++)
                System.arraycopy(px, (y + 1) * 64 + 1, f, y * 10, 10);
            return f;
        });
    }

    private static int[] paint(String id, int s) {
        int[] pal = {Theme.accent, Theme.selection, Theme.bg, Theme.fgBright, Theme.warn};
        Img im = id.equals("gentoo") ? logo() : id.equals("image") ? picture() : null;
        return im == null ? Native.cape(id, s, pal, null, 0, 0) : Native.cape(id, s, pal, im.px, im.w, im.h);
    }

    private record Img(int[] px, int w, int h) {}

    private static Img logo;

    private static Img logo() {
        if (logo == null)
            try (var in = Capes.class.getResourceAsStream("/assets/tuxmc/gentoo.png")) {
                logo = read(NativeImage.read(in));
            } catch (Exception e) {
                TuxMc.LOG.warn("tuxmc: gentoo logo: {}", e.toString());
            }
        return logo;
    }

    /** Config.capeImage: any png, cover-fitted to the cape */
    private static Img picture() {
        if (Config.capeImage.isBlank())
            return null;
        try (var in = Files.newInputStream(Path.of(Config.capeImage.replaceFirst("^~", System.getProperty("user.home"))))) {
            return read(NativeImage.read(in));
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: cape picture {}: {}", Config.capeImage, e.toString());
            return null;
        }
    }

    private static Img read(NativeImage im) {
        int w = im.getWidth(), h = im.getHeight();
        int[] px = new int[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                px[y * w + x] = im.getPixel(x, y);
        im.close();
        return new Img(px, w, h);
    }

    private static long mtime(String p) {
        try {
            return p.isBlank() ? 0 : Files.getLastModifiedTime(Path.of(p.replaceFirst("^~", System.getProperty("user.home")))).toMillis();
        } catch (Exception e) {
            return 0;
        }
    }

    private Capes() {}
}
