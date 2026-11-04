package dev.tux.mc.ui;

import net.minecraft.client.gui.GuiGraphicsExtractor;

/** shapes on top of GuiGraphicsExtractor.fill (which only does axis-aligned rects) */
public final class Draw {
    public static float now() {
        return (System.nanoTime() / 1_000_000L % 10_000_000L) / 1000f;
    }

    public static float easeOut(float t) {
        t = clamp01(t);
        return 1 - (1 - t) * (1 - t) * (1 - t);
    }

    public static float easeInOut(float t) {
        t = clamp01(t);
        return t < .5f ? 4 * t * t * t : 1 - (float) Math.pow(-2 * t + 2, 3) / 2;
    }

    public static float clamp01(float t) { return Math.max(0, Math.min(1, t)); }

    /** frame-rate independent approach: value moves toward target */
    public static float approach(float v, float target, float speed, float dt) {
        return v + (target - v) * (1 - (float) Math.exp(-speed * dt));
    }

    /** a straight line of given thickness (rotated rect) */
    public static void line(GuiGraphicsExtractor g, float x1, float y1, float x2, float y2, float th, int argb) {
        float dx = x2 - x1, dy = y2 - y1;
        float len = (float) Math.hypot(dx, dy);
        if (len < 0.01f || (argb >>> 24) == 0)
            return;
        var m = g.pose();
        m.pushMatrix();
        m.translate(x1, y1);
        m.rotate((float) Math.atan2(dy, dx));
        m.scale(len / 64f, th / 2f);
        g.fill(0, -1, 64, 1, argb);
        m.popMatrix();
    }

    /** cubic bezier point */
    public static float bez(float a, float b, float c, float d, float t) {
        float u = 1 - t;
        return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
    }

    /** bezier from (x0,y0) to (x3,y3), drawn up to fraction 'upto' (draw-in animation) */
    public static void curve(GuiGraphicsExtractor g, float x0, float y0, float x1, float y1, float x2, float y2,
            float x3, float y3, float th, int argb, float upto) {
        int n = 28;
        float px = x0, py = y0;
        for (int i = 1; i <= n; i++) {
            float t = i / (float) n;
            if (t > upto)
                t = upto;
            float x = bez(x0, x1, x2, x3, t), y = bez(y0, y1, y2, y3, t);
            line(g, px, py, x, y, th, argb);
            px = x;
            py = y;
            if (t >= upto)
                break;
        }
    }

    public static void disc(GuiGraphicsExtractor g, float cx, float cy, float r, int argb) {
        /* rows at 4x the radius resolution: smooth at any GUI scale */
        int n = Math.max(16, Math.round(r * 2));
        var m = g.pose();
        m.pushMatrix();
        m.translate(cx, cy);
        float s = r / n;
        m.scale(s / 4, s);
        for (int y = -n; y < n; y++) {
            float yy = y + .5f;
            int w = Math.round((float) Math.sqrt((float) n * n - yy * yy) * 4);
            g.fill(-w, y, w, y + 1, argb);
        }
        m.popMatrix();
    }

    public static void ring(GuiGraphicsExtractor g, float cx, float cy, float r, float th, int argb) {
        int n = Math.max(24, (int) (r * 1.6f));
        for (int i = 0; i < n; i++) {
            double a0 = i * Math.PI * 2 / n, a1 = (i + 1) * Math.PI * 2 / n;
            line(g, cx + (float) Math.cos(a0) * r, cy + (float) Math.sin(a0) * r,
                    cx + (float) Math.cos(a1) * r, cy + (float) Math.sin(a1) * r, th, argb);
        }
    }

    /** arc from angle a0 to a1 (radians) */
    public static void arc(GuiGraphicsExtractor g, float cx, float cy, float r, double a0, double a1, float th, int argb) {
        int n = Math.max(6, (int) (Math.abs(a1 - a0) * (r + th / 2) / 2));
        /* thick bands: stretch each piece so the outer edge has no gaps (use opaque colours) */
        double pad = th > 3 ? (a1 - a0) / n * (th / 2) / Math.max(1, r) * 1.2 : 0;
        for (int i = 0; i < n; i++) {
            double s = a0 + (a1 - a0) * i / n - (i > 0 ? pad : 0), e = a0 + (a1 - a0) * (i + 1) / n + (i < n - 1 ? pad : 0);
            line(g, cx + (float) Math.cos(s) * r, cy + (float) Math.sin(s) * r,
                    cx + (float) Math.cos(e) * r, cy + (float) Math.sin(e) * r, th, argb);
        }
    }

    /** rect with 2px cut corners */
    public static void panel(GuiGraphicsExtractor g, int x, int y, int w, int h, int fill, int border) {
        g.fill(x + 2, y, x + w - 2, y + h, fill);
        g.fill(x, y + 2, x + 2, y + h - 2, fill);
        g.fill(x + w - 2, y + 2, x + w, y + h - 2, fill);
        g.fill(x + 1, y + 1, x + 2, y + 2, fill);
        g.fill(x + w - 2, y + 1, x + w - 1, y + 2, fill);
        g.fill(x + 1, y + h - 2, x + 2, y + h - 1, fill);
        g.fill(x + w - 2, y + h - 2, x + w - 1, y + h - 1, fill);
        if ((border >>> 24) == 0)
            return;
        g.fill(x + 2, y, x + w - 2, y + 1, border);
        g.fill(x + 2, y + h - 1, x + w - 2, y + h, border);
        g.fill(x, y + 2, x + 1, y + h - 2, border);
        g.fill(x + w - 1, y + 2, x + w, y + h - 2, border);
        g.fill(x + 1, y + 1, x + 2, y + 2, border);
        g.fill(x + w - 2, y + 1, x + w - 1, y + 2, border);
        g.fill(x + 1, y + h - 2, x + 2, y + h - 1, border);
        g.fill(x + w - 2, y + h - 2, x + w - 1, y + h - 1, border);
    }

    public static String bytes(double b) {
        if (b < 1024)
            return (int) b + " B";
        if (b < 1024 * 1024)
            return String.format("%.1f KB", b / 1024);
        if (b < 1024L * 1024 * 1024)
            return String.format("%.1f MB", b / 1048576);
        return String.format("%.2f GB", b / 1073741824);
    }

    public static String rate(double b) {
        if (b < 1)
            return "idle";
        return bytes(b) + "/s";
    }

    private Draw() {}
}
