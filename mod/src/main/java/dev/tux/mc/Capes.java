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
    /* logo capes get 4x the pixels (2048x1024): thin lines like Puffy's spines need them */
    public static final int S_LOGO = 32;

    private static int scale(String id) { return LOGOS.contains(id) ? S_LOGO : S; }

    /** id -> display name, in menu order */
    public static final Map<String, String> ALL = new LinkedHashMap<>();
    static {
        ALL.put("tux", "Tux");
        ALL.put("gentoo", "Gentoo");
        ALL.put("steamos", "SteamOS");
        ALL.put("arch", "Arch");
        ALL.put("debian", "Debian");
        ALL.put("mint", "Mint");
        ALL.put("freebsd", "FreeBSD");
        ALL.put("openbsd", "OpenBSD");
        ALL.put("lfs", "LFS");
        ALL.put("crux", "CRUX");
        ALL.put("windows11", "Windows 11");
        ALL.put("stars", "Starfield");
        ALL.put("synth", "Synthwave");
        ALL.put("image", "Picture");
    }

    private static final Map<String, ClientAsset.Texture> made = new HashMap<>();
    private static final Map<String, int[]> previews = new HashMap<>();
    private static volatile boolean themeDirty;
    private static String imageKey = "";
    private static long imageChecked;

    public static void themeChanged() { themeDirty = true; }

    /** set only while the tux menu builds its preview player: that cape instead of yours */
    public static String previewId;

    /** null = no custom cape (official / none) */
    public static ClientAsset.Texture current() {
        String id = previewId != null ? previewId : Config.cape;
        return ALL.containsKey(id) ? get(id) : null;
    }

    private static void invalidate() {
        if (themeDirty) {
            themeDirty = false;
            /* no cape uses the theme colours any more; kept for the hook in Theme */
        }
        long now = System.currentTimeMillis();
        if (now - imageChecked < 2000)
            return;
        imageChecked = now;
        String k = Config.capeImage + "@" + mtime(Config.capeImage);
        if (!k.equals(imageKey)) {
            imageKey = k;
            generation++;                        /* a picture being painted is out of date now */
            made.remove("image");
            previews.keySet().removeIf(x -> x.startsWith("image@"));
        }
    }

    public static ClientAsset.Texture get(String id) {
        invalidate();
        if (made.containsKey(id))
            return made.get(id);
        if (!background()) {
            ClientAsset.Texture t = upload(id, image(id));
            made.put(id, t);
            return t;
        }
        /* Config.perfCosmetics: paint on the cape thread, upload on the render thread when it's done;
         * until then no cape (a frame or two), instead of a 20-80 ms hitch for a 2048x1024 logo */
        if (pending.add(id)) {
            int gen = generation;
            PAINT.execute(() -> {
                NativeImage img = image(id);
                Minecraft.getInstance().execute(() -> {
                    pending.remove(id);
                    if (gen != generation) {
                        if (img != null)
                            img.close();
                        return;
                    }
                    made.put(id, upload(id, img));
                });
            });
        }
        return null;
    }

    /* the cape thread: painting only (libtuxmc + NativeImage memory); GL stays on the render thread */
    private static final java.util.concurrent.ExecutorService PAINT = java.util.concurrent.Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "tuxmc-capes");
        t.setDaemon(true);
        t.setPriority(Thread.MIN_PRIORITY);
        return t;
    });
    private static final java.util.Set<String> pending = java.util.concurrent.ConcurrentHashMap.newKeySet();
    private static volatile int generation;

    private static boolean background() { return Config.perfOn() && Config.perfCosmetics; }

    private static NativeImage image(String id) {
        int s = scale(id);
        int[] px = paint(id, s);
        return px == null ? null : toImage(px, 64 * s, 32 * s);
    }

    private static NativeImage toImage(int[] px, int w, int h) {
        NativeImage img = new NativeImage(w, h, true);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                img.setPixel(x, y, px[y * w + x]);
        return img;
    }

    private static ClientAsset.Texture upload(String id, NativeImage img) {
        if (img == null)
            return null;
        Identifier loc = Identifier.fromNamespaceAndPath("tuxmc", "capes/" + id);
        Minecraft.getInstance().getTextureManager().register(loc, new DynamicTexture(() -> "tuxmc cape " + id, img));
        return new ClientAsset.ResourceTexture(loc, loc);
    }

    private static final class Made {
        final long at;
        final ClientAsset.Texture tex;
        long used = System.currentTimeMillis();

        Made(long at, ClientAsset.Texture tex) {
            this.at = at;
            this.tex = tex;
        }
    }

    private static final Map<java.util.UUID, Made> remote = new HashMap<>();

    /** another player's synced cape; built-in ones share our textures, theme and
     *  picture capes get one of their own (their colours / their image) */
    public static ClientAsset.Texture remote(java.util.UUID u, Sync.RemoteCape c) {
        if (c == null || c.id().equals("official") || c.id().equals("none"))
            return null;
        boolean own = c.id().equals("image");
        if (!own)
            return get(c.id());
        Made m = remote.get(u);
        if (m != null && m.at == c.at()) {
            m.used = System.currentTimeMillis();
            return m.tex;
        }
        if (!background()) {
            ClientAsset.Texture t = uploadRemote(u, remoteImage(c));
            remote.put(u, new Made(c.at(), t));
            return t;
        }
        if (remotePending.add(u)) {
            PAINT.execute(() -> {
                NativeImage img = remoteImage(c);
                Minecraft.getInstance().execute(() -> {
                    remotePending.remove(u);
                    remote.put(u, new Made(c.at(), uploadRemote(u, img)));
                });
            });
        }
        return m != null ? m.tex : null;         /* their old cape until the new one is painted */
    }

    private static final java.util.Set<java.util.UUID> remotePending = java.util.concurrent.ConcurrentHashMap.newKeySet();

    private static NativeImage remoteImage(Sync.RemoteCape c) {
        Img im = c.img() == null ? null : new Img(c.img(), c.w(), c.h());
        int[] pal = c.pal() != null ? c.pal() : new int[] {Theme.accent, Theme.selection, Theme.bg, Theme.fgBright, Theme.warn};
        int[] px = im == null ? Native.cape(c.id(), S, pal, null, 0, 0) : Native.cape(c.id(), S, pal, im.px, im.w, im.h);
        return px == null ? null : toImage(px, 64 * S, 32 * S);
    }

    private static Identifier remoteLoc(java.util.UUID u) {
        return Identifier.fromNamespaceAndPath("tuxmc", "capes/p/" + u.toString().replace("-", ""));
    }

    private static ClientAsset.Texture uploadRemote(java.util.UUID u, NativeImage img) {
        if (img == null)
            return null;
        Identifier loc = remoteLoc(u);
        Minecraft.getInstance().getTextureManager().register(loc, new DynamicTexture(() -> "tuxmc cape of " + u, img));
        return new ClientAsset.ResourceTexture(loc, loc);
    }

    private static int pruneTicks;

    /** Config.perfMemory: once a minute, free the cape textures of players not drawn for 5 minutes
     *  (a long session on a busy server would otherwise keep one per tux-mc player ever met) */
    public static void tick(Minecraft mc) {
        if (++pruneTicks % 1200 != 0 || !Config.perfOn() || !Config.perfMemory)
            return;
        long old = System.currentTimeMillis() - 5 * 60_000;
        var it = remote.entrySet().iterator();
        while (it.hasNext()) {
            var e = it.next();
            if (e.getValue().used < old) {
                if (e.getValue().tex != null)
                    mc.getTextureManager().release(remoteLoc(e.getKey()));
                it.remove();
            }
        }
    }

    /** the outside face as (10*k)x(16*k) ARGB, for menu previews */
    public static int[] preview(String id, int k) {
        invalidate();
        return previews.computeIfAbsent(id + "@" + k, key -> {
            int[] px = paint(id, k);
            if (px == null)
                return null;
            int w = 10 * k, h = 16 * k;
            int[] f = new int[w * h];
            for (int y = 0; y < h; y++)
                System.arraycopy(px, (y + k) * 64 * k + k, f, y * w, w);
            return f;
        });
    }

    private static int[] paint(String id, int s) {
        int[] pal = {Theme.accent, Theme.selection, Theme.bg, Theme.fgBright, Theme.warn};
        Img im = LOGOS.contains(id) ? logo(id) : id.equals("image") ? picture() : null;
        return im == null ? Native.cape(id, s, pal, null, 0, 0) : Native.cape(id, s, pal, im.px, im.w, im.h);
    }

    private record Img(int[] px, int w, int h) {}

    /** capes drawn from assets/tuxmc/<id>.png (same list as LOGOS in native/capes.c) */
    public static final java.util.Set<String> LOGOS = java.util.Set.of("tux", "gentoo", "steamos", "arch", "debian",
            "mint", "freebsd", "openbsd", "lfs", "crux", "windows11");

    private static final Map<String, Img> logos = new HashMap<>();

    /** assets/tuxmc/<id>.png: the real logo, painted over the cape's colours */
    private static Img logo(String id) {
        return logos.computeIfAbsent(id, k -> {
            try (var in = Capes.class.getResourceAsStream("/assets/tuxmc/" + k + ".png")) {
                return read(NativeImage.read(in));
            } catch (Exception e) {
                TuxMc.LOG.warn("tuxmc: {} logo: {}", k, e.toString());
                return null;
            }
        });
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
