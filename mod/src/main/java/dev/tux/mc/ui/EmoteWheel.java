package dev.tux.mc.ui;

import com.mojang.blaze3d.platform.InputConstants;
import dev.tux.mc.Emotes;
import dev.tux.mc.Theme;
import dev.tux.mc.TuxMc;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

/**
 * Hold . and point (or press 1-8, or click): release picks. A quick tap leaves it
 * open so you can click.
 */
public class EmoteWheel extends Screen {
    private static final Emotes.Emote[] E = Emotes.Emote.values();
    private final float openedAt = Draw.now();
    private float last = Draw.now();
    private final float[] hover = new float[E.length];
    private int sel = -1;
    private boolean closing;
    private float closingAt;
    private Emotes.Emote picked;

    public EmoteWheel() {
        super(Component.literal("Emotes"));
    }

    @Override
    public boolean isPauseScreen() { return false; }

    @Override
    public void extractBackground(GuiGraphicsExtractor g, int mx, int my, float partial) {
        /* no blur: you keep seeing the world */
    }

    private void pick(Emotes.Emote e) {
        if (closing)
            return;
        picked = e;
        closing = true;
        closingAt = Draw.now();
    }

    @Override
    public void onClose() { pick(null); }

    @Override
    public boolean keyPressed(KeyEvent e) {
        int k = e.key();
        if (k >= InputConstants.KEY_1 && k < InputConstants.KEY_1 + E.length) {     /* SDL scancodes: 1..9 are in a row */
            pick(E[k - InputConstants.KEY_1]);
            return true;
        }
        return super.keyPressed(e);
    }

    @Override
    public boolean keyReleased(KeyEvent e) {
        if (TuxMc.emoteKey.matches(e) && Draw.now() - openedAt > 0.25f) {
            pick(sel >= 0 ? E[sel] : null);
            return true;
        }
        return super.keyReleased(e);
    }

    @Override
    public boolean mouseClicked(MouseButtonEvent e, boolean dbl) {
        pick(sel >= 0 ? E[sel] : null);
        return true;
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor g, int mx, int my, float partial) {
        float now = Draw.now(), dt = Math.min(0.1f, now - last);
        last = now;
        float open = Draw.easeOut((now - openedAt) / 0.22f);
        if (closing) {
            float c = Draw.clamp01((now - closingAt) / 0.14f);
            open = Math.min(open, 1 - c);
            if (c >= 1) {
                minecraft.gui.setScreen(null);
                if (picked != null)
                    Emotes.play(picked);
                return;
            }
        }
        float cx = width / 2f, cy = height / 2f;
        float R = Math.min(96, Math.min(width, height) * 0.32f) * (0.75f + 0.25f * open);
        float inner = R * 0.42f;

        double dx = mx - cx, dy = my - cy;
        double dist = Math.hypot(dx, dy);
        if (dist > inner * 0.8) {
            double a = Math.atan2(dy, dx) + Math.PI / 2 + Math.PI / E.length;   /* 0 = top */
            a = (a % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI);
            sel = (int) (a / (2 * Math.PI / E.length)) % E.length;
        } else {
            sel = -1;
        }

        Draw.disc(g, cx, cy, R + 6, Theme.alpha(Theme.bg, 0.55f * open));
        float seg = (float) (2 * Math.PI / E.length);
        for (int i = 0; i < E.length; i++) {
            hover[i] = Draw.approach(hover[i], i == sel ? 1 : 0, 18, dt);
            float h = hover[i];
            double a0 = -Math.PI / 2 - seg / 2 + i * seg + 0.035, a1 = a0 + seg - 0.07;
            float mid = (inner + R) / 2 + h * 4;
            float th = (R - inner) - 2 + h * 4;
            int col = Theme.mix(Theme.bgAlt, Theme.selection, h);
            Draw.arc(g, cx, cy, mid, a0, a1, th, Theme.alpha(col, open));
            Draw.arc(g, cx, cy, mid + th / 2, a0, a1, 1.2f,
                    Theme.alpha(Theme.mix(Theme.surface, Theme.accentBright, h), open));
            double am = (a0 + a1) / 2;
            float ix = cx + (float) Math.cos(am) * mid, iy = cy + (float) Math.sin(am) * mid;
            int ic = Theme.alpha(Theme.mix(Theme.subtext, Theme.accentBright, h), open);
            icon(g, E[i], ix, iy - 4, 1 + h * 0.2f, ic);
            g.centeredText(font, E[i].title, Math.round(ix), Math.round(iy + 9),
                    Theme.alpha(Theme.mix(Theme.dim, Theme.fgBright, h), open));
        }
        /* centre: what release does */
        Draw.disc(g, cx, cy, inner - 4, Theme.alpha(Theme.bgAlt, 0.95f * open));
        Draw.ring(g, cx, cy, inner - 4, 1, Theme.alpha(Theme.accent, open));
        String t = sel >= 0 ? E[sel].title : Emotes.current != null ? "stop" : "emotes";
        g.centeredText(font, t, Math.round(cx), Math.round(cy - 4),
                Theme.alpha(sel >= 0 ? Theme.accentBright : Theme.dim, open));
        if (sel >= 0 && E[sel] == Emotes.current)
            g.centeredText(font, "(stop)", Math.round(cx), Math.round(cy + 6), Theme.alpha(Theme.muted, open));
    }

    /** a ~22px stick figure doing the emote, animated */
    public static void icon(GuiGraphicsExtractor g, Emotes.Emote e, float x, float y, float s, int c) {
        float t = Draw.now();
        float th = 1.3f * s;
        float hx = x, hy = y - 7 * s;          /* head */
        float sx = x, sy = y - 3.5f * s;       /* shoulders */
        float px = x, py = y + 3 * s;          /* hips */
        float lax, lay, rax, ray;              /* hands, left / right as you see it */
        float llx = x - 3 * s, lly = y + 9 * s, rlx = x + 3 * s, rly = y + 9 * s;
        switch (e) {
        case WAVE -> {
            lax = x - 3.5f * s; lay = y + 2 * s;
            float w = (float) Math.sin(t * 8) * 2.5f;
            rax = x + (5 + w) * s; ray = y - 10 * s;
        }
        case DANCE -> {
            float b = (float) Math.sin(t * 6);
            lax = x - 5 * s; lay = y + (b > 0 ? -9 : 0) * s;
            rax = x + 5 * s; ray = y + (b > 0 ? 0 : -9) * s;
            hx += b * s; sx += b * s * 0.6f;
        }
        case SIT -> {
            hy += 4 * s; sy += 4 * s; py += 4 * s;
            lax = x - 1 * s; lay = y + 4 * s; rax = x + 4 * s; ray = y + 4 * s;
            llx = x + 6 * s; lly = py; rlx = x + 8 * s; rly = py + 3 * s;
        }
        case LAY -> {
            hx = x - 8 * s; hy = y + 6 * s; sx = x - 4 * s; sy = y + 6 * s; px = x + 3 * s; py = y + 6 * s;
            lax = x - 1 * s; lay = y + 8 * s; rax = x - 1 * s; ray = y + 4 * s;
            llx = x + 9 * s; lly = y + 7 * s; rlx = x + 9 * s; rly = y + 5 * s;
        }
        case CLAP -> {
            float gp = 1 + 2.5f * (float) Math.abs(Math.cos(t * 7));
            lax = x - gp * s; lay = y - 2 * s; rax = x + gp * s; ray = y - 2 * s;
        }
        case SPIN -> {
            float k = (float) Math.cos(t * 5);
            lax = x - 6 * k * s; lay = sy; rax = x + 6 * k * s; ray = sy;
        }
        case BOW -> {
            float b = (float) Math.abs(Math.sin(t * 2));
            hx = x + 4 * b * s; hy = y - 7 * s + 3 * b * s; sx = x + 3 * b * s; sy = y - 3.5f * s + 2 * b * s;
            lax = sx - 1 * s; lay = sy + 6 * s; rax = sx + 1 * s; ray = sy + 6 * s;
        }
        case DAB -> {
            lax = x - 7 * s; lay = y - 9 * s;
            rax = x - 2 * s; ray = y - 6 * s;
            hx = x - 1.5f * s; hy = y - 6 * s;
        }
        default -> { lax = x - 4 * s; lay = y + 2 * s; rax = x + 4 * s; ray = y + 2 * s; }
        }
        Draw.disc(g, hx, hy, 2.6f * s, c);
        Draw.line(g, sx, sy, px, py, th, c);
        Draw.line(g, sx, sy, lax, lay, th, c);
        Draw.line(g, sx, sy, rax, ray, th, c);
        Draw.line(g, px, py, llx, lly, th, c);
        Draw.line(g, px, py, rlx, rly, th, c);
    }
}
