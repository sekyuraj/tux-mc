package dev.tux.mc;

import java.awt.image.BufferedImage;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.CompletableFuture;

import javax.imageio.ImageIO;

import com.mojang.blaze3d.platform.NativeImage;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.resources.Identifier;

/**
 * Your picture behind the menus instead of the panorama (PanoramaMixin).
 * Config: menuBg (a png/jpg path; the launcher copies it to
 * config/tuxmc/menu-bg.png), menuBlur, menuDim, menuParallax.
 */
public final class MenuBg {
    private static final Identifier TEX = Identifier.fromNamespaceAndPath("tuxmc", "menu_bg");
    private static String key = "", loading = "";
    private static int w, h;
    private static boolean ready;
    private static float px, py;                    /* smoothed parallax, -1..1 */

    /** draws it and returns true; false = show the panorama (none set, or still loading) */
    public static boolean draw(GuiGraphicsExtractor g, int sw, int sh) {
        if (Config.menuBg.isBlank())
            return false;
        Path p = Path.of(Config.menuBg.replaceFirst("^~", System.getProperty("user.home")));
        long m;
        try {
            m = Files.getLastModifiedTime(p).toMillis();
        } catch (Exception e) {
            return false;
        }
        String k = p + "@" + m + "/" + Config.menuBlur;
        if (!k.equals(key) && !k.equals(loading))
            load(p, k);
        if (!ready)
            return false;

        Minecraft mc = Minecraft.getInstance();
        var win = mc.getWindow();
        float tx = 0, ty = 0;
        if (Config.menuParallax && win.getScreenWidth() > 0) {
            tx = (float) (mc.mouseHandler.xpos() / win.getScreenWidth() * 2 - 1);
            ty = (float) (mc.mouseHandler.ypos() / win.getScreenHeight() * 2 - 1);
        }
        px += (tx - px) * 0.06f;
        py += (ty - py) * 0.06f;
        float zoom = Config.menuParallax ? 1.05f : 1f;
        float s = Math.max((float) sw / w, (float) sh / h) * zoom;
        float dw = w * s, dh = h * s;
        float x0 = (sw - dw) / 2 - px * (dw - sw) / 2 * 0.8f, y0 = (sh - dh) / 2 - py * (dh - sh) / 2 * 0.8f;
        g.blit(TEX, Math.round(x0), Math.round(y0), Math.round(x0 + dw), Math.round(y0 + dh), 0f, 1f, 0f, 1f);
        int dim = Math.round(Math.max(0, Math.min(0.9f, Config.menuDim)) * 255);
        if (dim > 0)
            g.fill(0, 0, sw, sh, dim << 24);
        return true;
    }

    private static void load(Path p, String k) {
        loading = k;
        int blur = Config.menuBlur;
        CompletableFuture.supplyAsync(() -> {
            try {
                BufferedImage src = ImageIO.read(p.toFile());
                if (src == null)
                    return null;
                /* at most 1920 wide: plenty behind a menu, and blurring stays quick */
                double k2 = Math.min(1, 1920.0 / src.getWidth());
                int iw = Math.max(1, (int) (src.getWidth() * k2)), ih = Math.max(1, (int) (src.getHeight() * k2));
                BufferedImage im = new BufferedImage(iw, ih, BufferedImage.TYPE_INT_ARGB);
                var gg = im.createGraphics();
                gg.setRenderingHint(java.awt.RenderingHints.KEY_INTERPOLATION, java.awt.RenderingHints.VALUE_INTERPOLATION_BILINEAR);
                gg.drawImage(src, 0, 0, iw, ih, null);
                gg.dispose();
                int[] px = im.getRGB(0, 0, iw, ih, null, 0, iw);
                int r = Math.round(blur * ih / 1080f);
                if (r > 0)
                    for (int pass = 0; pass < 3; pass++) {
                        boxBlur(px, iw, ih, r, true);
                        boxBlur(px, iw, ih, r, false);
                    }
                return new Object[] {px, iw, ih};
            } catch (Exception e) {
                TuxMc.LOG.warn("tuxmc: menu background {}: {}", p, e.toString());
                return null;
            }
        }).thenAccept(res -> Minecraft.getInstance().execute(() -> {
            if (!k.equals(loading))
                return;
            loading = "";
            key = k;
            if (res == null) {
                ready = false;
                return;
            }
            int[] px = (int[]) res[0];
            w = (int) res[1];
            h = (int) res[2];
            NativeImage img = new NativeImage(w, h, false);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    img.setPixel(x, y, px[y * w + x] | 0xFF000000);
            Minecraft.getInstance().getTextureManager().register(TEX, new DynamicTexture(() -> "tuxmc menu background", img));
            ready = true;
        }));
    }

    /** one pass of a running-sum box blur, along rows or columns */
    private static void boxBlur(int[] px, int w, int h, int r, boolean rows) {
        int len = rows ? w : h, lines = rows ? h : w;
        int[] line = new int[len];
        for (int l = 0; l < lines; l++) {
            for (int i = 0; i < len; i++)
                line[i] = px[rows ? l * w + i : i * w + l];
            int sr = 0, sg = 0, sb = 0, n = 2 * r + 1;
            for (int i = -r; i <= r; i++) {
                int c = line[Math.max(0, Math.min(len - 1, i))];
                sr += c >> 16 & 255;
                sg += c >> 8 & 255;
                sb += c & 255;
            }
            for (int i = 0; i < len; i++) {
                px[rows ? l * w + i : i * w + l] = 0xFF000000 | (sr / n) << 16 | (sg / n) << 8 | (sb / n);
                int a = line[Math.min(len - 1, i + r + 1)], b = line[Math.max(0, i - r)];
                sr += (a >> 16 & 255) - (b >> 16 & 255);
                sg += (a >> 8 & 255) - (b >> 8 & 255);
                sb += (a & 255) - (b & 255);
            }
        }
    }

    private MenuBg() {}
}
