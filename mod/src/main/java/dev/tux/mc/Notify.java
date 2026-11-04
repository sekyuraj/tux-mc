package dev.tux.mc;

import java.util.ArrayList;
import java.util.List;

import dev.tux.mc.ui.Draw;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.resources.sounds.SimpleSoundInstance;
import net.minecraft.sounds.SoundEvents;

/** themed toasts, top right; also a desktop notification when the game isn't focused */
public final class Notify {
    private record Toast(String title, String body, int color, float born) {}

    private static final List<Toast> toasts = new ArrayList<>();

    public static void show(String title, String body, int color, boolean sound, boolean desktop) {
        Minecraft mc = Minecraft.getInstance();
        mc.execute(() -> {
            toasts.add(new Toast(title, body, color, Draw.now()));
            while (toasts.size() > 5)
                toasts.remove(0);
            if (sound && Config.alertSound)
                mc.getSoundManager().play(SimpleSoundInstance.forUI(SoundEvents.NOTE_BLOCK_PLING, 1.6f));
            if (desktop && Config.desktopAlerts && !mc.isWindowActive())
                desktop(title, body);
        });
    }

    public static void desktop(String title, String body) {
        try {
            new ProcessBuilder("notify-send", "-a", "Minecraft", "-i", "minecraft", title, body == null ? "" : body)
                    .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
        } catch (Exception ignored) {
        }
    }

    public static void extract(GuiGraphicsExtractor g) {
        if (toasts.isEmpty())
            return;
        var f = Minecraft.getInstance().font;
        float now = Draw.now(), life = Math.max(1, Config.toastTime);
        int y = 6;
        for (int i = toasts.size() - 1; i >= 0; i--) {
            Toast t = toasts.get(i);
            float age = now - t.born;
            if (age > life) {
                toasts.remove(i);
                continue;
            }
            float in = Draw.easeOut(age / 0.3f), out = Draw.clamp01((life - age) / 0.4f);
            float a = Math.min(in, out);
            int w = Math.max(f.width(t.title), t.body == null ? 0 : f.width(t.body)) + 18;
            int h = t.body == null ? 16 : 26;
            int x = g.guiWidth() - 6 - Math.round(w * (0.3f + 0.7f * in)) + Math.round((1 - out) * 30);
            x = Math.max(x, g.guiWidth() - 6 - w);
            Draw.panel(g, x, y, w, h, Theme.alpha(Theme.bg, 0.9f * a), Theme.alpha(Theme.surface, a));
            g.fill(x, y + 2, x + 2, y + h - 2, Theme.alpha(t.color, a));
            g.text(f, t.title, x + 9, y + 4, Theme.alpha(Theme.fgBright, a), false);
            if (t.body != null)
                g.text(f, t.body, x + 9, y + 14, Theme.alpha(Theme.subtext, a), false);
            y += Math.round((h + 4) * Math.min(1, in * 1.4f));
        }
    }

    private Notify() {}
}
