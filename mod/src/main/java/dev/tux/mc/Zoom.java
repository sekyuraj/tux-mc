package dev.tux.mc;

import dev.tux.mc.ui.Draw;

/**
 * Zoom: hold C for Config.zoomFov, or hold ` (grave) and scroll to zoom in
 * and out. Scrolling while either is held changes how far, and doesn't
 * touch the hotbar. Letting go goes back to normal.
 */
public final class Zoom {
    public static boolean held;           /* C (or toggled on) */
    public static boolean scrollHeld;     /* ` */
    private static float k = 1f;
    private static float level = -1;      /* this hold's zoom, -1 = not set yet */
    private static long last = System.nanoTime();

    public static boolean active() { return held || scrollHeld; }

    /** a wheel step while zooming: up = closer */
    public static void scroll(double dy) {
        if (level < 0)
            level = held ? Config.zoomFov : 1f;
        level *= (float) Math.pow(0.85, dy);
        level = Math.max(0.02f, Math.min(1f, level));
    }

    public static float factor() {
        long n = System.nanoTime();
        float dt = Math.min(0.1f, (n - last) / 1e9f);
        last = n;
        if (!active())
            level = -1;
        float target = !active() ? 1f : level >= 0 ? level : held ? Config.zoomFov : 1f;
        k = Config.zoomSmooth ? Draw.approach(k, target, 14f, dt) : target;
        if (Math.abs(k - target) < 0.002f)
            k = target;
        return k;
    }

    private Zoom() {}
}
