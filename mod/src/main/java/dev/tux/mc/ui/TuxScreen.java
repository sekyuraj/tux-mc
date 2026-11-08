package dev.tux.mc.ui;

import com.mojang.blaze3d.platform.InputConstants;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import dev.tux.mc.Capes;
import dev.tux.mc.Config;
import dev.tux.mc.Emotes;
import dev.tux.mc.NetMonitor;
import dev.tux.mc.Notify;
import dev.tux.mc.ServerTools;
import dev.tux.mc.Waypoints;
import dev.tux.mc.Theme;
import dev.tux.mc.TuxMc;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

/** the tux menu: double-tap left shift in game, or the Tux button on the title / pause screen */
public class TuxScreen extends Screen {
    /* the title screen picture to switch back to (the launcher keeps it here) */
    private static String menuBgOff = net.fabricmc.loader.api.FabricLoader.getInstance().getConfigDir()
            .resolve("tuxmc").resolve("menu-bg.png").toString();
    public enum Tab {
        NETWORK("Network"), SERVER("Server"), WAYPOINTS("Waypoints"), SESSION("Session"), EMOTES("Emotes"),
        COSMETICS("Capes"), SETTINGS("Settings");

        final String title;
        Tab(String t) { title = t; }
    }

    private record Hit(int x, int y, int w, int h, Runnable action) {
        boolean in(double mx, double my) { return mx >= x && my >= y && mx < x + w && my < y + h; }
    }

    private static final boolean CYCLE = System.getenv("TUXMC_CYCLE") != null;
    private final Screen parent;
    private Tab tab;
    private final float openedAt = Draw.now();
    private float tabAt = Draw.now();
    private float indicatorY = -1;
    private float last = Draw.now();
    private boolean closing;
    private float closingAt;
    private final List<Hit> hits = new ArrayList<>();
    private final Map<String, Float> anim = new HashMap<>();

    /* session graphs (static: keep history between openings) */
    private static final float[] fpsHist = new float[120], pingHist = new float[120];
    private static int histN;
    private static final java.util.Set<Tab> failed = java.util.EnumSet.noneOf(Tab.class);
    private static long histAt;
    private static final long sessionStart = System.currentTimeMillis();

    public TuxScreen(Screen parent, Tab tab) {
        super(Component.literal("Tux"));
        this.parent = parent;
        this.tab = tab;
        Theme.reload();
    }

    @Override
    public boolean isPauseScreen() { return false; }

    @Override
    public void onClose() {
        if (!closing) {
            closing = true;
            closingAt = Draw.now();
        }
    }

    private void reallyClose() {
        minecraft.gui.setScreen(parent);
    }

    @Override
    public boolean keyPressed(KeyEvent e) {
        int k = e.key();
        if (k >= InputConstants.KEY_1 && k < InputConstants.KEY_1 + Tab.values().length) {   /* SDL scancodes: 1..9 are in a row */
            switchTab(Tab.values()[k - InputConstants.KEY_1]);
            return true;
        }
        return super.keyPressed(e);
    }

    private void switchTab(Tab t) {
        if (t != tab) {
            tab = t;
            tabAt = Draw.now();
        }
    }

    @Override
    public boolean mouseClicked(MouseButtonEvent e, boolean dbl) {
        for (int i = hits.size() - 1; i >= 0; i--)
            if (hits.get(i).in(e.x(), e.y())) {
                hits.get(i).action().run();
                return true;
            }
        int[] p = panelRect();
        if (e.x() < p[0] || e.y() < p[1] || e.x() > p[0] + p[2] || e.y() > p[1] + p[3]) {
            onClose();
            return true;
        }
        return super.mouseClicked(e, dbl);
    }

    /* settings scroll */
    private float scroll, scrollTarget, scrollMax;
    private int clipTop, clipBottom = Integer.MAX_VALUE;

    @Override
    public boolean mouseScrolled(double mx, double my, double sx, double sy) {
        scrollTarget = Math.max(0, Math.min(scrollMax, scrollTarget - (float) sy * 24));
        return true;
    }

    private void hit(int x, int y, int w, int h, Runnable r) {
        if (y + h > clipTop && y < clipBottom)
            hits.add(new Hit(x, Math.max(y, clipTop), w, Math.min(y + h, clipBottom) - Math.max(y, clipTop), r));
    }

    private int[] panelRect() {
        int w = Math.min(540, width - 24), h = Math.min(310, height - 24);
        return new int[] {(width - w) / 2, (height - h) / 2, w, h};
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor g, int mx, int my, float partial) {
        Theme.reload();
        float now = Draw.now(), dt = Math.min(0.1f, now - last);
        last = now;
        if (CYCLE)                                   /* test hook: walk through the tabs */
            switchTab(Tab.values()[(int) ((now - openedAt) / 4) % Tab.values().length]);
        float open = Draw.easeOut((now - openedAt) / 0.28f);
        if (closing) {
            float c = Draw.clamp01((now - closingAt) / 0.16f);
            open = Math.min(open, 1 - Draw.easeInOut(c));
            if (c >= 1) {
                reallyClose();
                return;
            }
        }
        hits.clear();
        sampleHistory();

        g.fill(0, 0, width, height, Theme.alpha(Theme.bg, 0.35f * open));
        int[] p = panelRect();
        int px = p[0], py = p[1], pw = p[2], ph = p[3];
        var m = g.pose();
        m.pushMatrix();
        float sc = 0.94f + 0.06f * open;
        m.translate(width / 2f, height / 2f + (1 - open) * 14);
        m.scale(sc, sc);
        m.translate(-width / 2f, -height / 2f);

        Draw.panel(g, px, py, pw, ph, Theme.alpha(Theme.bg, 0.94f * open), Theme.alpha(Theme.surface, open));
        /* sidebar */
        int sw = 96;
        g.fill(px + 1, py + 2, px + sw, py + ph - 2, Theme.alpha(Theme.bgAlt, open));
        g.fill(px + sw, py + 2, px + sw + 1, py + ph - 2, Theme.alpha(Theme.surface, open));
        g.text(font, "tux", px + 12, py + 12, Theme.alpha(Theme.accentBright, open), false);
        g.text(font, "client", px + 12 + font.width("tux "), py + 12, Theme.alpha(Theme.dim, open), false);

        int ty = py + 36;
        int tabH = 22;
        int target = ty + tab.ordinal() * tabH;
        indicatorY = indicatorY < 0 ? target : Draw.approach(indicatorY, target, 18, dt);
        g.fill(px + 6, (int) indicatorY + 2, px + sw - 6, (int) indicatorY + tabH - 2, Theme.alpha(Theme.selection, open));
        g.fill(px + 6, (int) indicatorY + 4, px + 8, (int) indicatorY + tabH - 4, Theme.alpha(Theme.accent, open));
        for (Tab t : Tab.values()) {
            int y = ty + t.ordinal() * tabH;
            boolean hov = mx >= px && mx < px + sw && my >= y && my < y + tabH;
            float a = anim(t.name(), hov || t == tab ? 1 : 0, dt, 14);
            int col = Theme.mix(Theme.subtext, Theme.fgBright, a);
            g.text(font, t.title, px + 16 + Math.round(a * 3), y + 7, Theme.alpha(col, open), false);
            g.text(font, "" + (t.ordinal() + 1), px + sw - 16, y + 7, Theme.alpha(Theme.muted, open), false);
            final Tab tt = t;
            hits.add(new Hit(px, y, sw, tabH, () -> switchTab(tt)));
        }
        g.text(font, Theme.name, px + 12, py + ph - 26, Theme.alpha(Theme.muted, open), false);
        g.text(font, "esc to close", px + 12, py + ph - 15, Theme.alpha(Theme.muted, open), false);

        /* content, slides in on tab change */
        float ta = Draw.easeOut((now - tabAt) / 0.3f);
        int cx = px + sw + 14, cy = py + 12, cw = pw - sw - 26, ch = ph - 24;
        m.pushMatrix();
        m.translate((1 - ta) * 16, 0);
        float al = open * ta;
        g.text(font, tab.title, cx, cy, Theme.alpha(Theme.fgBright, al), false);
        cy += 16;
        ch -= 16;
        g.enableScissor(cx - 4, cy - 2, cx + cw + 4, cy + ch + 2);
        try {
            switch (tab) {
            case NETWORK -> network(g, cx, cy, cw, ch, mx, my, al, now, dt);
            case SERVER -> server(g, cx, cy, cw, ch, al);
            case WAYPOINTS -> waypoints(g, cx, cy, cw, ch, mx, my, al, dt);
            case SESSION -> session(g, cx, cy, cw, ch, al);
            case EMOTES -> emotes(g, cx, cy, cw, ch, mx, my, al, dt);
            case COSMETICS -> capes(g, cx, cy, cw, ch, mx, my, al, dt);
            case SETTINGS -> settings(g, cx, cy, cw, ch, mx, my, al, dt);
            }
        } catch (RuntimeException e) {         /* a bug in one tab must not take the game down */
            if (failed.add(tab))
                dev.tux.mc.TuxMc.LOG.error("tuxmc: {} tab failed", tab, e);
            g.text(font, "This tab hit a bug: " + e, cx, cy + 4, Theme.alpha(Theme.urgent, al), false);
            g.text(font, "The game log has the details.", cx, cy + 16, Theme.alpha(Theme.dim, al), false);
        }
        g.disableScissor();
        m.popMatrix();
        m.popMatrix();
    }

    private float anim(String key, float target, float dt, float speed) {
        float v = Draw.approach(anim.getOrDefault(key, target), target, speed, dt);
        anim.put(key, v);
        return v;
    }

    /* ---------------------------------------------------------------- network */

    private void network(GuiGraphicsExtractor g, int x, int y, int w, int h, int mx, int my, float al, float now,
            float dt) {
        NetMonitor.want();
        List<NetMonitor.Peer> peers = NetMonitor.peers();
        boolean local = minecraft.isLocalServer();

        int pcx = x + 40, pcy = y + h / 2 - 6;
        int nodeX = x + w - 150;
        int rows = peers.size() + (local ? 1 : 0);
        int maxRows = Math.max(1, (h - 10) / 30);
        int shown = Math.min(rows, maxRows);
        float gap = shown <= 1 ? 0 : Math.min(34, (h - 24f) / (shown - 1));
        float top = pcy - gap * (shown - 1) / 2f;

        double up = 0, down = 0;
        for (NetMonitor.Peer p : peers) {
            up += NetMonitor.alive(p) ? p.upRate : 0;
            down += NetMonitor.alive(p) ? p.downRate : 0;
        }

        NetMonitor.Peer hovered = null;
        int i = 0;
        if (local) {
            float ny = top;
            float ap = anim("local", 1, dt, 6);
            Draw.curve(g, pcx + 22, pcy, pcx + 90, pcy, nodeX - 90, ny, nodeX - 6, ny, 1.2f,
                    Theme.alpha(Theme.accent, al * 0.8f), ap);
            for (int k = 0; k < 3; k++) {
                float t = ((now * 0.5f + k / 3f) % 1f);
                dot(g, pcx + 22, pcy, nodeX - 6, ny, t, Theme.fgBright, al * ap);
            }
            Draw.disc(g, nodeX, ny, 4.5f, Theme.alpha(Theme.accent, al));
            g.text(font, "This world (singleplayer)", nodeX + 10, (int) ny - 8, Theme.alpha(Theme.fg, al), false);
            g.text(font, "in-process, no network", nodeX + 10, (int) ny + 2, Theme.alpha(Theme.dim, al), false);
            i++;
        }
        for (NetMonitor.Peer p : peers) {
            if (i >= maxRows)
                break;
            float ny = top + gap * i;
            boolean alive = NetMonitor.alive(p);
            p.appear = Draw.approach(p.appear, alive ? 1 : 0, alive ? 5 : 3, dt);
            float a = al * p.appear;
            boolean hov = mx >= nodeX - 10 && mx < x + w && my >= ny - 12 && my < ny + 12;
            float hv = anim("peer" + p.key, hov ? 1 : 0, dt, 16);
            if (hov)
                hovered = p;

            double rate = p.upRate + p.downRate;
            float th = 1f + (float) Math.min(2.2, Math.log10(rate + 1) / 2.2) + hv;
            int lineCol = p.game ? Theme.accentBright : Theme.accent;
            Draw.curve(g, pcx + 22, pcy, pcx + 90, pcy, nodeX - 90, ny, nodeX - 6, ny, th,
                    Theme.alpha(Theme.mix(lineCol, Theme.fgBright, hv), a * (0.55f + 0.45f * hv)), p.appear);
            /* packets: out = squares toward the node, in = dots toward the PC */
            int nUp = particles(p.upRate), nDown = particles(p.downRate);
            float spUp = 0.25f + (float) Math.min(1.2, Math.log10(p.upRate + 1) / 4);
            float spDn = 0.25f + (float) Math.min(1.2, Math.log10(p.downRate + 1) / 4);
            for (int k = 0; k < nUp; k++)
                square(g, pcx + 22, pcy, nodeX - 6, ny, (now * spUp + k / (float) nUp + p.port * 0.37f) % 1f,
                        Theme.warn, a);
            for (int k = 0; k < nDown; k++)
                dot(g, pcx + 22, pcy, nodeX - 6, ny, 1 - ((now * spDn + k / (float) nDown) % 1f), Theme.fgBright, a);

            float pulse = rate > 1 ? 0.6f + 0.4f * (float) Math.sin(now * 6 + i) : 0;
            Draw.disc(g, nodeX, ny, 4.5f + hv * 1.5f, Theme.alpha(p.game ? Theme.accentBright : Theme.accent, a));
            if (pulse > 0)
                Draw.ring(g, nodeX, ny, 7 + pulse * 2, 0.8f, Theme.alpha(Theme.accent, a * 0.5f));
            String l = font.plainSubstrByWidth(p.label, 140);
            g.text(font, l, nodeX + 10, (int) ny - 8, Theme.alpha(Theme.mix(Theme.fg, Theme.fgBright, hv), a), false);
            String sub = p.ip + (p.port > 0 ? ":" + p.port : "") + "  " + (alive ? arrows(p) : "closed");
            g.text(font, font.plainSubstrByWidth(sub, 140), nodeX + 10, (int) ny + 2,
                    Theme.alpha(alive ? Theme.dim : Theme.urgent, a), false);
            i++;
        }
        if (rows > maxRows)
            g.text(font, "+" + (rows - maxRows) + " more", nodeX + 10, y + h - 9, Theme.alpha(Theme.muted, al), false);

        /* the PC */
        computer(g, pcx, pcy, al, now, up + down);
        g.centeredText(font, "This PC", pcx, pcy + 26, Theme.alpha(Theme.fg, al));
        g.centeredText(font, "↑ " + Draw.rate(up), pcx, pcy + 37, Theme.alpha(Theme.warn, al));
        g.centeredText(font, "↓ " + Draw.rate(down), pcx, pcy + 47, Theme.alpha(Theme.fgBright, al));

        if (peers.isEmpty() && !local)
            g.text(font, "waiting for connections…", nodeX - 20, pcy - 4, Theme.alpha(Theme.dim, al), false);

        if (hovered != null) {
            NetMonitor.Peer p = hovered;
            List<String> lines = new ArrayList<>();
            lines.add(p.label + (p.game ? "  (you're playing here)" : ""));
            lines.add(p.proto.toUpperCase() + "  " + p.ip + ":" + p.port);
            if (p.host != null && !p.host.isEmpty())
                lines.add(p.host);
            lines.add("sent " + Draw.bytes(p.sent) + "   received " + Draw.bytes(p.recv));
            int tw = 0;
            for (String s : lines)
                tw = Math.max(tw, font.width(s));
            int bx = Math.min(mx + 10, x + w - tw - 12), by = Math.min(my + 10, y + h - lines.size() * 10 - 10);
            Draw.panel(g, bx, by, tw + 12, lines.size() * 10 + 8, Theme.alpha(Theme.bgAlt, 0.97f * al),
                    Theme.alpha(Theme.accent, al));
            for (int k = 0; k < lines.size(); k++)
                g.text(font, lines.get(k), bx + 6, by + 5 + k * 10, Theme.alpha(k == 0 ? Theme.fgBright : Theme.subtext, al),
                        false);
        }
    }

    private static String arrows(NetMonitor.Peer p) {
        return "↑" + Draw.rate(p.upRate) + " ↓" + Draw.rate(p.downRate);
    }

    private static int particles(double rate) {
        if (rate < 1)
            return 0;
        return (int) Math.min(7, 1 + Math.log10(rate + 1) * 1.4);
    }

    private static float curveX(float x0, float x3, float t) { return Draw.bez(x0, x0 + 68, x3 - 84, x3, t); }

    private static float curveY(float y0, float y3, float t) { return Draw.bez(y0, y0, y3, y3, t); }

    private static void dot(GuiGraphicsExtractor g, float x0, float y0, float x3, float y3, float t, int col, float a) {
        float fade = Math.min(1, Math.min(t, 1 - t) * 8);
        Draw.disc(g, curveX(x0, x3, t), curveY(y0, y3, t), 1.6f, Theme.alpha(col, a * fade));
    }

    private static void square(GuiGraphicsExtractor g, float x0, float y0, float x3, float y3, float t, int col,
            float a) {
        float fade = Math.min(1, Math.min(t, 1 - t) * 8);
        int x = Math.round(curveX(x0, x3, t)), y = Math.round(curveY(y0, y3, t));
        g.fill(x - 1, y - 1, x + 2, y + 2, Theme.alpha(col, a * fade));
    }

    /** a little monitor; its screen shows live bars */
    private void computer(GuiGraphicsExtractor g, int cx, int cy, float al, float now, double rate) {
        int w = 40, h = 28;
        int x = cx - w / 2, y = cy - h / 2 - 4;
        float glow = (float) Math.min(1, Math.log10(rate + 1) / 5);
        Draw.panel(g, x - 2, y - 2, w + 4, h + 4, Theme.alpha(Theme.surface, al),
                Theme.alpha(Theme.mix(Theme.accent, Theme.accentBright, glow), al));
        g.fill(x + 2, y + 2, x + w - 2, y + h - 2, Theme.alpha(Theme.bg, al));
        for (int b = 0; b < 7; b++) {
            float v = 0.25f + 0.75f * (float) Math.abs(Math.sin(now * (1.3 + b * 0.37) + b)) * (0.3f + glow);
            int bh = Math.max(1, Math.round(v * (h - 8)));
            g.fill(x + 5 + b * 5, y + h - 4 - bh, x + 8 + b * 5, y + h - 4, Theme.alpha(Theme.accent, al * 0.85f));
        }
        g.fill(cx - 3, y + h + 2, cx + 3, y + h + 7, Theme.alpha(Theme.muted, al));
        g.fill(cx - 10, y + h + 7, cx + 10, y + h + 9, Theme.alpha(Theme.muted, al));
    }

    /* ---------------------------------------------------------------- session */

    private void sampleHistory() {
        long t = System.currentTimeMillis();
        if (t - histAt < 250)
            return;
        histAt = t;
        System.arraycopy(fpsHist, 1, fpsHist, 0, fpsHist.length - 1);
        System.arraycopy(pingHist, 1, pingHist, 0, pingHist.length - 1);
        fpsHist[fpsHist.length - 1] = minecraft.getFps();
        pingHist[pingHist.length - 1] = Math.max(0, TuxMc.ping());
        histN = Math.min(fpsHist.length, histN + 1);
    }

    private void session(GuiGraphicsExtractor g, int x, int y, int w, int h, float al) {
        int half = (w - 8) / 2;
        int gh = Math.max(50, Math.min(80, h - 3 * 44));
        graph(g, x, y, half, gh, "FPS", minecraft.getFps() + "", fpsHist, histN, Theme.accent, al);
        int ping = TuxMc.ping();
        graph(g, x + half + 8, y, half, gh, "Ping", ping < 0 ? "local" : ping + " ms", pingHist, histN, Theme.warn, al);

        int ry = y + gh + 8;
        Runtime rt = Runtime.getRuntime();
        long used = rt.totalMemory() - rt.freeMemory(), max = rt.maxMemory();
        card(g, x, ry, half, 36, "Memory", Draw.bytes(used) + " / " + Draw.bytes(max), al);
        float fr = (float) used / max;
        g.fill(x + 8, ry + 29, x + half - 8, ry + 31, Theme.alpha(Theme.surface, al));
        g.fill(x + 8, ry + 29, x + 8 + Math.round((half - 16) * fr), ry + 31,
                Theme.alpha(fr > 0.85f ? Theme.urgent : Theme.accent, al));
        long s = (System.currentTimeMillis() - sessionStart) / 1000;
        card(g, x + half + 8, ry, half, 36, "Session", String.format("%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60), al);

        ry += 44;
        String where;
        if (minecraft.isLocalServer())
            where = "Singleplayer";
        else if (minecraft.getCurrentServer() != null)
            where = minecraft.getCurrentServer().name + "  (" + minecraft.getCurrentServer().ip + ")";
        else
            where = "Main menu";
        card(g, x, ry, half, 36, "World", where, al);
        if (minecraft.player != null) {
            var p = minecraft.player;
            double sp = Math.hypot(p.getDeltaMovement().x, p.getDeltaMovement().z) * 20;
            card(g, x + half + 8, ry, half, 36, String.format("Position  %.1f b/s", sp),
                    String.format("%.0f  %.0f  %.0f", p.getX(), p.getY(), p.getZ()), al);
        }
    }

    private void card(GuiGraphicsExtractor g, int x, int y, int w, int h, String title, String value, float al) {
        Draw.panel(g, x, y, w, h, Theme.alpha(Theme.bgAlt, al), Theme.alpha(Theme.surface, al));
        g.text(font, title, x + 8, y + 7, Theme.alpha(Theme.dim, al), false);
        g.text(font, font.plainSubstrByWidth(value, w - 16), x + 8, y + 18, Theme.alpha(Theme.fgBright, al), false);
    }

    /** the last `count` samples of data (older slots are still empty) */
    private void graph(GuiGraphicsExtractor g, int x, int y, int w, int h, String title, String value, float[] data,
            int count, int col, float al) {
        Draw.panel(g, x, y, w, h, Theme.alpha(Theme.bgAlt, al), Theme.alpha(Theme.surface, al));
        g.text(font, title, x + 8, y + 7, Theme.alpha(Theme.dim, al), false);
        g.text(font, value, x + w - 8 - font.width(value), y + 7, Theme.alpha(Theme.fgBright, al), false);
        float max = 10;
        for (float v : data)
            max = Math.max(max, v);
        max *= 1.15f;
        int gx = x + 6, gy = y + 20, gw = w - 12, gh = h - 26;
        int n = data.length;
        float px = -1, py = 0;
        for (int i = Math.max(0, n - count); i < n; i++) {
            float fx = gx + gw * (i / (float) (n - 1));
            float fy = gy + gh - gh * (data[i] / max);
            g.fill(Math.round(fx), Math.round(fy), Math.round(fx) + 1, gy + gh, Theme.alpha(col, al * 0.18f));
            if (px >= 0)
                Draw.line(g, px, py, fx, fy, 1.2f, Theme.alpha(col, al));
            px = fx;
            py = fy;
        }
    }

    /* ---------------------------------------------------------------- emotes */

    private void emotes(GuiGraphicsExtractor g, int x, int y, int w, int h, int mx, int my, float al, float dt) {
        g.text(font, font.plainSubstrByWidth("Press . for the wheel. Moving, jumping, sneaking or Esc stops one.", w), x, y,
                Theme.alpha(Theme.dim, al), false);
        g.text(font, font.plainSubstrByWidth("Other tux-mc players see them too (Settings, Sharing).", w), x, y + 10,
                Theme.alpha(Theme.muted, al), false);
        int cols = 4, gap = 8;
        int bw = (w - gap * (cols - 1)) / cols, bh = 46;
        int i = 0;
        for (Emotes.Emote e : Emotes.Emote.values()) {
            int bx = x + (i % cols) * (bw + gap), by = y + 28 + (i / cols) * (bh + gap);
            boolean hov = mx >= bx && mx < bx + bw && my >= by && my < by + bh;
            boolean on = Emotes.current == e;
            float a = anim("emote" + e, hov || on ? 1 : 0, dt, 14);
            Draw.panel(g, bx, by - Math.round(a * 2), bw, bh, Theme.alpha(Theme.mix(Theme.bgAlt, Theme.selection, a), al),
                    Theme.alpha(on ? Theme.accentBright : Theme.mix(Theme.surface, Theme.accent, a), al));
            EmoteWheel.icon(g, e, bx + bw / 2f, by + 16 - a * 2, 1f, Theme.alpha(Theme.mix(Theme.subtext, Theme.accentBright, a), al));
            g.centeredText(font, e.title, bx + bw / 2, by + bh - 13 - Math.round(a * 2),
                    Theme.alpha(Theme.mix(Theme.fg, Theme.fgBright, a), al));
            final Emotes.Emote ee = e;
            hits.add(new Hit(bx, by, bw, bh, () -> {
                if (minecraft.player == null)
                    return;
                Emotes.play(ee);
                onClose();
            }));
            i++;
        }
    }

    /* ---------------------------------------------------------------- capes */

    private String hoverCape;

    /** you, turning slowly, wearing the hovered cape (or the chosen one) */
    @SuppressWarnings({"unchecked", "rawtypes"})
    private void capePreview(GuiGraphicsExtractor g, int x, int y, int w, int h, float al) {
        Draw.panel(g, x, y, w, h, Theme.alpha(Theme.bgAlt, al), Theme.alpha(Theme.surface, al));
        var p = minecraft.player;
        if (p == null || al < 0.05f)
            return;
        String id = hoverCape != null ? hoverCape : Config.cape;
        net.minecraft.client.renderer.entity.state.EntityRenderState st;
        Capes.previewId = id;
        try {
            net.minecraft.client.renderer.entity.EntityRenderer r = minecraft.getEntityRenderDispatcher().getRenderer(p);
            st = (net.minecraft.client.renderer.entity.state.EntityRenderState) r.createRenderState(p, 1f);
        } finally {
            Capes.previewId = null;
        }
        st.shadowPieces.clear();
        st.outlineColor = 0;
        if (st instanceof net.minecraft.client.renderer.entity.state.AvatarRenderState av && id.equals("none"))
            av.showCape = false;
        if (st instanceof net.minecraft.client.renderer.entity.state.LivingEntityRenderState l) {
            /* a slow turn that lingers on the back, where the cape is */
            float t = Draw.now() - openedAt;
            l.bodyRot = 180f + (float) (Math.sin(t * 0.7) * 70.0) + 180f;
            l.yRot = 0;
            l.xRot = 0;
            l.boundingBoxWidth /= l.scale;
            l.boundingBoxHeight /= l.scale;
            l.scale = 1;
        }
        int size = Math.max(20, Math.min((int) (h * 0.36f), (int) (w * 0.8f)));
        org.joml.Quaternionf rot = new org.joml.Quaternionf().rotateZ((float) Math.PI);
        org.joml.Quaternionf cam = new org.joml.Quaternionf().rotateX((float) Math.toRadians(-8));
        rot.mul(cam);
        org.joml.Vector3f tr = new org.joml.Vector3f(0, st.boundingBoxHeight / 2f + 0.0625f, 0);
        g.entity(st, size, tr, rot, cam, x + 2, y + 2, x + w - 2, y + h - 2);
        String name = id.equals("official") ? "Mojang" : id.equals("none") ? "None" : Capes.ALL.getOrDefault(id, id);
        g.centeredText(font, name, x + w / 2, y + h - 12, Theme.alpha(hoverCape != null ? Theme.subtext : Theme.fg, al));
    }

    private void capes(GuiGraphicsExtractor g, int x, int y, int w, int h, int mx, int my, float al, float dt) {
        /* the preview on the left, the capes on the right */
        int pvw = Math.min(110, w / 3);
        capePreview(g, x, y + 16, pvw, h - 16, al);
        x += pvw + 10;
        w -= pvw + 10;
        String hovered = null;
        g.text(font, font.plainSubstrByWidth(Config.cape.equals("image") && Config.capeImage.isBlank()
                ? "Picture: choose the image in the launcher (Home, client cape)." : Config.sync ? "Other tux-mc players see these too (Settings, Sharing)." : "Only you see these: sharing is off in Settings.", w), x, y,
                Theme.alpha(Theme.dim, al), false);
        List<String> ids = new ArrayList<>();
        ids.add("official");
        ids.addAll(Capes.ALL.keySet());
        ids.add("none");
        int cols = 4, gap = 6, rows = (ids.size() + cols - 1) / cols;
        int bw = (w - gap * (cols - 1)) / cols, bh = Math.min(82, (h - 16 - gap * (rows - 1)) / rows);
        int i = 0;
        for (String id : ids) {
            int bx = x + (i % cols) * (bw + gap), by = y + 16 + (i / cols) * (bh + gap);
            boolean hov = mx >= bx && mx < bx + bw && my >= by && my < by + bh;
            if (hov)
                hovered = id;
            boolean on = Config.cape.equals(id);
            float a = anim("cape" + id, hov ? 1 : 0, dt, 14);
            float s = anim("capesel" + id, on ? 1 : 0, dt, 10);
            Draw.panel(g, bx, by, bw, bh, Theme.alpha(Theme.mix(Theme.bgAlt, Theme.selection, Math.max(a, s * 0.6f)), al),
                    Theme.alpha(Theme.mix(Theme.surface, Theme.accentBright, Math.max(a * 0.6f, s)), al));
            int px = bh >= 76 ? 3 : 2;
            int cw = 10 * px, chh = 16 * px;
            int ox = bx + (bw - cw) / 2, oy = by + Math.max(4, (bh - 14 - chh) / 2) - Math.round(a * 2);
            /* painted at px times the vanilla size: one GUI pixel per cape pixel */
            int[] pv = Capes.ALL.containsKey(id) ? Capes.preview(id, px) : null;
            if (pv != null) {
                int pw = 10 * px;
                for (int yy = 0; yy < 16 * px; yy++)
                    for (int xx = 0; xx < pw; xx++)
                        g.fill(ox + xx, oy + yy, ox + xx + 1, oy + yy + 1, Theme.alpha(pv[yy * pw + xx], al));
            } else {
                g.outline(ox, oy, cw, chh, Theme.alpha(Theme.muted, al));
                String t = id.equals("none") ? "✕" : "M";
                g.centeredText(font, t, ox + cw / 2, oy + chh / 2 - 4, Theme.alpha(Theme.subtext, al));
            }
            String name = id.equals("official") ? "Mojang" : id.equals("none") ? "None" : Capes.ALL.get(id);
            g.centeredText(font, name, bx + bw / 2, by + bh - 13, Theme.alpha(on ? Theme.accentBright : Theme.fg, al));
            hits.add(new Hit(bx, by, bw, bh, () -> {
                Config.cape = id;
                Config.save();
            }));
            i++;
        }
        hoverCape = hovered;
    }

    /* ---------------------------------------------------------------- settings */

    private int row;

    private void settings(GuiGraphicsExtractor g, int x, int y, int w, int h, int mx, int my, float al, float dt) {
        scroll = Draw.approach(scroll, scrollTarget, 16, dt);
        clipTop = y;
        clipBottom = y + h;
        row = 0;
        int y0 = y - Math.round(scroll);
        section(g, x, y0, "HUD", al);
        toggle(g, x, y0, w, mx, my, al, dt, "Info HUD", "fps, ping, tps, queue", Config.hud,
                () -> Config.hud = !Config.hud);
        toggle(g, x, y0, w, mx, my, al, dt, "Coordinates", "position, nether coords, facing", Config.hudCoords,
                () -> Config.hudCoords = !Config.hudCoords);
        toggle(g, x, y0, w, mx, my, al, dt, "Armor and totems", "durability bars and totem count by the hotbar",
                Config.hudArmor, () -> Config.hudArmor = !Config.hudArmor);
        toggle(g, x, y0, w, mx, my, al, dt, "Server line", "TPS and queue position in the HUD", Config.hudServer,
                () -> Config.hudServer = !Config.hudServer);
        toggle(g, x, y0, w, mx, my, al, dt, "Facing", "north / south ... after the coordinates", Config.hudFacing,
                () -> Config.hudFacing = !Config.hudFacing);
        cycle(g, x, y0, w, mx, my, al, dt, "Corner", "where the info box sits",
                new String[] {"top left", "top right", "bottom left", "bottom right"}[Config.hudCorner & 3],
                () -> Config.hudCorner = (Config.hudCorner + 1) & 3);
        cycle(g, x, y0, w, mx, my, al, dt, "Size", "scale of the info box", Math.round(Config.hudScale * 100) + "%",
                () -> Config.hudScale = Config.hudScale >= 1.5f ? 0.75f : Config.hudScale + 0.25f);
        cycle(g, x, y0, w, mx, my, al, dt, "Background", "info box backdrop (none = text with shadow)",
                Config.hudOpacity <= 0 ? "none" : Math.round(Config.hudOpacity * 100) + "%",
                () -> Config.hudOpacity = Config.hudOpacity <= 0 ? 0.4f : Config.hudOpacity < 0.6f ? 0.72f
                        : Config.hudOpacity < 0.85f ? 0.92f : 0f);
        toggle(g, x, y0, w, mx, my, al, dt, "Waypoint arrow", "top centre, for the tracked waypoint", Config.waypointArrow,
                () -> Config.waypointArrow = !Config.waypointArrow);
        toggle(g, x, y0, w, mx, my, al, dt, "Streamer mode", "hide coordinates everywhere", Config.streamer,
                () -> Config.streamer = !Config.streamer);
        section(g, x, y0, "Servers", al);
        toggle(g, x, y0, w, mx, my, al, dt, "Distant Horizons on servers", "with shaders it crashes AMD drivers on join",
                Config.dhOnServers, () -> Config.dhOnServers = !Config.dhOnServers);
        cycle(g, x, y0, w, mx, my, al, dt, "Queue alert", "notify (also on the desktop) at this position",
                "at " + Config.queueAlert, () -> Config.queueAlert = switch (Config.queueAlert) {
                    case 10 -> 25;
                    case 25 -> 50;
                    case 50 -> 100;
                    default -> 10;
                });
        toggle(g, x, y0, w, mx, my, al, dt, "Visual range", "alert when a player comes into view", Config.visualRange,
                () -> Config.visualRange = !Config.visualRange);
        cycle(g, x, y0, w, mx, my, al, dt, "Near radius", "how close a player has to be to count as near",
                Config.nearRadius <= 0 ? "render distance" : Config.nearRadius + " blocks",
                () -> Config.nearRadius = switch (Config.nearRadius) {
                    case 0 -> 16;
                    case 16 -> 32;
                    case 32 -> 64;
                    case 64 -> 128;
                    case 128 -> 256;
                    default -> 0;
                });
        toggle(g, x, y0, w, mx, my, al, dt, "Mentions", "alert when chat says your name", Config.mentions,
                () -> Config.mentions = !Config.mentions);
        toggle(g, x, y0, w, mx, my, al, dt, "Alert sound", "a ping with alerts", Config.alertSound,
                () -> Config.alertSound = !Config.alertSound);
        toggle(g, x, y0, w, mx, my, al, dt, "Desktop alerts", "also notify the desktop while the game is in the background",
                Config.desktopAlerts, () -> Config.desktopAlerts = !Config.desktopAlerts);
        cycle(g, x, y0, w, mx, my, al, dt, "Toast time", "how long alerts stay on screen",
                (Config.toastTime % 1 == 0 ? (int) Config.toastTime + "" : Config.toastTime + "") + " s",
                () -> Config.toastTime = Config.toastTime < 3 ? 3 : Config.toastTime < 4.5f ? 4.5f
                        : Config.toastTime < 7 ? 7 : Config.toastTime < 10 ? 10 : 2);
        toggle(g, x, y0, w, mx, my, al, dt, "Auto reconnect", "after a kick, reconnect on its own", Config.autoReconnect,
                () -> Config.autoReconnect = !Config.autoReconnect);
        cycle(g, x, y0, w, mx, my, al, dt, "Reconnect delay", "seconds before reconnecting",
                Config.reconnectDelay + " s", () -> Config.reconnectDelay = switch (Config.reconnectDelay) {
                    case 3 -> 5;
                    case 5 -> 10;
                    case 10 -> 30;
                    default -> 3;
                });
        toggle(g, x, y0, w, mx, my, al, dt, "Death points", "save a waypoint where you die", Config.deathPoints,
                () -> Config.deathPoints = !Config.deathPoints);
        section(g, x, y0, "Sharing", al);
        toggle(g, x, y0, w, mx, my, al, dt, "Share capes and emotes",
                "other tux-mc players see them: " + dev.tux.mc.Sync.status(), Config.sync,
                () -> Config.sync = !Config.sync);
        toggle(g, x, y0, w, mx, my, al, dt, "Share pictures",
                "your Picture cape and other people's (more internet use)", Config.syncPicture,
                () -> Config.syncPicture = !Config.syncPicture);
        toggle(g, x, y0, w, mx, my, al, dt, "Other people's hats", "show the hats other tux-mc players made",
                Config.syncHats, () -> Config.syncHats = !Config.syncHats);
        toggle(g, x, y0, w, mx, my, al, dt, "Hat off under a helmet", "your hat and theirs hide while a helmet is on",
                Config.hatHideHelmet, () -> Config.hatHideHelmet = !Config.hatHideHelmet);
        toggle(g, x, y0, w, mx, my, al, dt, "Title screen picture", "the one set in the launcher; off = the panorama",
                !Config.menuBg.isBlank(), () -> {
                    if (Config.menuBg.isBlank())
                        Config.menuBg = menuBgOff;
                    else {
                        menuBgOff = Config.menuBg;
                        Config.menuBg = "";
                    }
                });
        section(g, x, y0, "Combat", al);
        toggle(g, x, y0, w, mx, my, al, dt, "Hit delay fix", "clicks right after a miss still hit (1.8 PvP, Hypixel)",
                Config.hitFix, () -> Config.hitFix = !Config.hitFix);
        section(g, x, y0, "View", al);
        toggle(g, x, y0, w, mx, my, al, dt, "Fullbright", "see in the dark (client-side lighting)", Config.fullbright,
                () -> Config.fullbright = !Config.fullbright);
        cycle(g, x, y0, w, mx, my, al, dt, "Zoom", "how far it zooms",
                Config.zoomFov >= 0.45f ? "2x" : Config.zoomFov >= 0.2f ? "4x" : "8x",
                () -> Config.zoomFov = Config.zoomFov >= 0.45f ? 0.25f : Config.zoomFov >= 0.2f ? 0.125f : 0.5f);
        cycle(g, x, y0, w, mx, my, al, dt, "Zoom key", "hold it, or press once to zoom and again to stop",
                Config.zoomToggle ? "toggle" : "hold", () -> Config.zoomToggle = !Config.zoomToggle);
        toggle(g, x, y0, w, mx, my, al, dt, "Smooth zoom", "ease in and out", Config.zoomSmooth,
                () -> Config.zoomSmooth = !Config.zoomSmooth);
        int total = rowY(0) + 6;
        scrollMax = Math.max(0, total - h);
        if (scrollMax > 0) {                       /* scrollbar */
            int bh = Math.max(16, h * h / total);
            int by = y + Math.round((h - bh) * (scroll / scrollMax));
            g.fill(x + w + 2, by, x + w + 4, by + bh, Theme.alpha(Theme.muted, al));
        }
        clipTop = 0;
        clipBottom = Integer.MAX_VALUE;
    }

    private int sections;

    private void section(GuiGraphicsExtractor g, int x, int y, String t, float al) {
        if (row == 0)
            sections = 0;
        int ry = y + row * 27 + sections * 16;
        g.text(font, t.toUpperCase(), x + 2, ry + 4, Theme.alpha(Theme.accent, al), false);
        g.fill(x + 6 + font.width(t.toUpperCase()), ry + 8, x + 300, ry + 9, Theme.alpha(Theme.surface, al));
        sections++;
    }

    /* ---------------------------------------------------------------- server */

    private void server(GuiGraphicsExtractor g, int x, int y, int w, int h, float al) {
        int half = (w - 8) / 2;
        String name = minecraft.getCurrentServer() != null ? minecraft.getCurrentServer().name : minecraft.isLocalServer() ? "Singleplayer" : "not connected";
        String ip = minecraft.getCurrentServer() != null ? minecraft.getCurrentServer().ip : "";
        card(g, x, y, half, 36, name, ip.isEmpty() ? "-" : ip, al);
        int ping = TuxMc.ping();
        card(g, x + half + 8, y, half, 36, "Ping / players near", (ping < 0 ? "-" : ping + " ms") + "   "
                + ServerTools.nearCount() + " near", al);

        int ry = y + 44;
        graph(g, x, ry, half, 64, "TPS", ServerTools.tpsKnown ? String.format("%.1f", ServerTools.tps) : "-",
                ServerTools.tpsHist, ServerTools.tpsSamples, ServerTools.tps < 15 ? Theme.urgent : Theme.accent, al);
        /* queue */
        Draw.panel(g, x + half + 8, ry, half, 64, Theme.alpha(Theme.bgAlt, al), Theme.alpha(Theme.surface, al));
        int qx = x + half + 16;
        g.text(font, "Queue", qx, ry + 7, Theme.alpha(Theme.dim, al), false);
        if (ServerTools.inQueue()) {
            var m = g.pose();
            m.pushMatrix();
            m.translate(qx, ry + 20);
            m.scale(2, 2);
            g.text(font, "" + ServerTools.queuePos, 0, 0, Theme.alpha(Theme.accentBright, al), false);
            m.popMatrix();
            double eta = ServerTools.queueEta();
            g.text(font, eta > 0 ? "ETA " + dev.tux.mc.Hud.eta(eta) : "measuring speed\u2026", qx, ry + 42,
                    Theme.alpha(Theme.fg, al), false);
            g.text(font, String.format("%.1f / min, alert at %d", ServerTools.queueRate(), Config.queueAlert), qx, ry + 52,
                    Theme.alpha(Theme.dim, al), false);
        } else {
            g.text(font, "not in a queue", qx, ry + 22, Theme.alpha(Theme.fg, al), false);
            g.text(font, font.plainSubstrByWidth("server queues are picked up", half - 16), qx, ry + 34,
                    Theme.alpha(Theme.dim, al), false);
            g.text(font, font.plainSubstrByWidth("automatically, alert at " + Config.queueAlert, half - 16), qx, ry + 44,
                    Theme.alpha(Theme.dim, al), false);
        }

        ry += 72;
        g.text(font, "Visual range", x, ry, Theme.alpha(Theme.fgBright, al), false);
        ry += 12;
        if (ServerTools.sightings.isEmpty())
            g.text(font, "nobody seen yet", x, ry, Theme.alpha(Theme.dim, al), false);
        for (int i = 0; i < ServerTools.sightings.size() && ry < y + h - 9; i++, ry += 11) {
            var s = ServerTools.sightings.get(i);
            g.text(font, s.time(), x, ry, Theme.alpha(Theme.muted, al), false);
            g.text(font, s.name(), x + 50, ry, Theme.alpha(s.entered() ? Theme.fgBright : Theme.dim, al), false);
            String what = s.entered() ? (Config.streamer ? "entered" : "entered at " + s.x() + " " + s.y() + " " + s.z())
                    : "left";
            g.text(font, what, x + 160, ry, Theme.alpha(s.entered() ? Theme.urgent : Theme.dim, al), false);
        }
    }

    /* ---------------------------------------------------------------- waypoints */

    private void waypoints(GuiGraphicsExtractor g, int x, int y, int w, int h, int mx, int my, float al, float dt) {
        int bw = font.width("+ Add here") + 16;
        boolean hov = mx >= x && mx < x + bw && my >= y && my < y + 16;
        Draw.panel(g, x, y, bw, 16, Theme.alpha(hov ? Theme.accentBright : Theme.accent, al), 0);
        g.text(font, "+ Add here", x + 8, y + 4, Theme.alpha(Theme.bg, al), false);
        hits.add(new Hit(x, y, bw, 16, () -> {
            var p = Waypoints.add(null, false);
            if (p != null)
                Notify.show("Waypoint saved", p.name, Theme.accent, false, false);
        }));
        g.text(font, "on " + Waypoints.serverKey() + ", you're in the " + Waypoints.dim(), x + bw + 8, y + 4,
                Theme.alpha(Theme.dim, al), false);
        int ry = y + 24;
        var list = Waypoints.here();
        if (list.isEmpty())
            g.text(font, "no waypoints here yet (deaths are saved on their own)", x, ry, Theme.alpha(Theme.dim, al), false);
        for (int i = list.size() - 1; i >= 0 && ry < y + h - 20; i--, ry += 26) {
            var p = list.get(i);
            boolean rh = mx >= x && mx < x + w && my >= ry && my < ry + 24;
            float a = anim("wp" + p.created, rh ? 1 : 0, dt, 16);
            Draw.panel(g, x, ry, w, 24, Theme.alpha(Theme.mix(Theme.bgAlt, Theme.selection, a), al),
                    Theme.alpha(p.tracked ? Theme.accentBright : Theme.surface, al));
            g.fill(x, ry + 2, x + 2, ry + 22, Theme.alpha(p.death ? Theme.urgent : Theme.accent, al));
            g.text(font, p.name, x + 8, ry + 4, Theme.alpha(Theme.fgBright, al), false);
            String where;
            if (Config.streamer)
                where = p.dim + "  coords hidden";
            else {
                String other = p.dim.equals("overworld") ? String.format("  nether %d %d", p.x / 8, p.z / 8)
                        : p.dim.equals("nether") ? String.format("  overworld %d %d", p.x * 8, p.z * 8) : "";
                where = p.dim + "  " + p.x + " " + p.y + " " + p.z + other;
            }
            g.text(font, font.plainSubstrByWidth(where, w - 120), x + 8, ry + 14, Theme.alpha(Theme.dim, al), false);
            double[] at = Waypoints.inMyDim(p);
            if (at != null && minecraft.player != null) {
                double d = Math.hypot(at[0] - minecraft.player.getX(), at[2] - minecraft.player.getZ());
                String ds = d >= 1000 ? String.format("%.1fk m", d / 1000) : (int) d + " m";
                g.text(font, ds, x + w - 110 - font.width(ds), ry + 8, Theme.alpha(Theme.subtext, al), false);
            }
            String tl = p.tracked ? "Tracking" : "Track";
            int tx = x + w - 96;
            Draw.panel(g, tx, ry + 4, 56, 16, Theme.alpha(p.tracked ? Theme.accent : Theme.selection, al),
                    Theme.alpha(Theme.accent, al));
            g.centeredText(font, tl, tx + 28, ry + 8, Theme.alpha(p.tracked ? Theme.bg : Theme.fgBright, al));
            final var pp = p;
            hits.add(new Hit(tx, ry + 4, 56, 16, () -> Waypoints.track(pp)));
            int dx = x + w - 34;
            Draw.panel(g, dx, ry + 4, 28, 16, Theme.alpha(Theme.bgAlt, al), Theme.alpha(Theme.urgent, al * 0.8f));
            g.centeredText(font, "\u2715", dx + 14, ry + 8, Theme.alpha(Theme.urgent, al));
            hits.add(new Hit(dx, ry + 4, 28, 16, () -> Waypoints.remove(pp)));
        }
    }

    private int rowY(int y) { return y + row * 27 + sections * 16; }

    private void toggle(GuiGraphicsExtractor g, int x, int y, int w, int mx, int my, float al, float dt, String title,
            String desc, boolean on, Runnable flip) {
        int ry = rowY(y);
        boolean hov = rowLabel(g, x, ry, w, mx, my, al, dt, title, desc);
        float k = anim("tg" + title, on ? 1 : 0, dt, 16);
        int tx = x + w - 30, tyy = ry + 6;
        Draw.panel(g, tx, tyy, 26, 12, Theme.alpha(Theme.mix(Theme.surface, Theme.accent, k), al),
                Theme.alpha(hov ? Theme.accentBright : Theme.muted, al));
        Draw.disc(g, tx + 6 + k * 14, tyy + 6, 4, Theme.alpha(Theme.mix(Theme.dim, Theme.bg, k), al));
        hit(x, ry, w, 26, () -> {
            flip.run();
            Config.save();
        });
        row++;
    }

    private void cycle(GuiGraphicsExtractor g, int x, int y, int w, int mx, int my, float al, float dt, String title,
            String desc, String value, Runnable next) {
        int ry = rowY(y);
        boolean hov = rowLabel(g, x, ry, w, mx, my, al, dt, title, desc);
        int vw = font.width(value) + 16;
        Draw.panel(g, x + w - vw - 4, ry + 4, vw, 16, Theme.alpha(Theme.selection, al),
                Theme.alpha(hov ? Theme.accentBright : Theme.accent, al));
        g.text(font, value, x + w - vw + 4, ry + 8, Theme.alpha(Theme.fgBright, al), false);
        hit(x, ry, w, 26, () -> {
            next.run();
            Config.save();
        });
        row++;
    }

    private boolean rowLabel(GuiGraphicsExtractor g, int x, int ry, int w, int mx, int my, float al, float dt,
            String title, String desc) {
        boolean hov = mx >= x && mx < x + w && my >= ry && my < ry + 26;
        float a = anim("row" + title, hov ? 1 : 0, dt, 16);
        if (a > 0.01f)
            Draw.panel(g, x - 4, ry, w + 8, 26, Theme.alpha(Theme.bgAlt, al * a), 0);
        g.text(font, title, x + 2, ry + 4, Theme.alpha(Theme.mix(Theme.fg, Theme.fgBright, a), al), false);
        g.text(font, font.plainSubstrByWidth(desc, w - 100), x + 2, ry + 14, Theme.alpha(Theme.dim, al), false);
        return hov;
    }
}
