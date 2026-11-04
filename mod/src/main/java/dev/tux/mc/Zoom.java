package dev.tux.mc;

import dev.tux.mc.ui.Draw;

/** hold the zoom key (C): FOV eases down to Config.zoomFov */
public final class Zoom {
    public static boolean held;
    private static float k = 1f;
    private static long last = System.nanoTime();

    public static float factor() {
        long n = System.nanoTime();
        float dt = Math.min(0.1f, (n - last) / 1e9f);
        last = n;
        float target = held ? Config.zoomFov : 1f;
        k = Config.zoomSmooth ? Draw.approach(k, target, 14f, dt) : target;
        if (Math.abs(k - target) < 0.002f)
            k = target;
        return k;
    }

    private Zoom() {}
}
