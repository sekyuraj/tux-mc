package dev.tux.mc;

import net.minecraft.client.Minecraft;

/**
 * Distant Horizons with a shader pack crashes the AMD (Mesa radeonsi) driver
 * a moment after joining a server: DH's client-only server mode plus Iris's
 * pipeline reload on join (any pack: Bliss and Complementary both did it;
 * singleplayer is fine). So on servers DH's rendering is switched off through
 * its API (a temporary override, the config file stays as it is) unless
 * Config.dhOnServers. Back in singleplayer the override is cleared.
 * Reflection, so tuxmc doesn't need DH.
 */
public final class DhCompat {
    private static boolean overridden;

    private static Object renderingEnabled() throws Exception {
        Class<?> delayed = Class.forName("com.seibel.distanthorizons.api.DhApi$Delayed");
        Object configs = delayed.getField("configs").get(null);
        if (configs == null)
            return null;
        Object graphics = configs.getClass().getMethod("graphics").invoke(configs);
        return graphics.getClass().getMethod("renderingEnabled").invoke(graphics);
    }

    /** connecting somewhere: off on servers, as configured in singleplayer */
    /* DH's config values, through the API interface (the classes behind it aren't public) */
    private static Object call(Object v, String name, Object... args) throws Exception {
        Class<?> i = Class.forName("com.seibel.distanthorizons.api.interfaces.config.IDhApiConfigValue");
        return args.length == 0 ? i.getMethod(name).invoke(v) : i.getMethod(name, Object.class).invoke(v, args[0]);
    }

    /* what renderingEnabled was before we changed it; null = we didn't.
     * Its setValue also lands in DistantHorizons.toml, so leaving a server
     * writes this back first: otherwise DH stayed off in the file for good. */
    private static Boolean before;

    private static void set(boolean on, String why) {
        try {
            Object v = renderingEnabled();
            if (v == null)
                return;
            Object was = call(v, "getValue");
            if (before == null)
                before = Boolean.TRUE.equals(was);
            call(v, "setValue", on);
            TuxMc.LOG.info("tuxmc: Distant Horizons {} {}", on ? "on" : "off", why);
        } catch (ClassNotFoundException e) {
            /* no DH */
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: couldn't switch Distant Horizons {}: {}", on ? "on" : "off", e.toString());
        }
    }

    /** connecting somewhere: off on servers, as configured in singleplayer */
    public static void connecting(Minecraft mc) {
        /* TUXMC_BENCH_DH=0/1: off / on for one benchmark run */
        String benchDh = System.getenv("TUXMC_BENCH_DH");
        if ("1".equals(benchDh) || "0".equals(benchDh)) {
            set("1".equals(benchDh), "for this benchmark");
            overridden = "0".equals(benchDh);
            return;
        }
        if (mc.getSingleplayerServer() != null || Config.dhOnServers)
            return;
        set(false, "on this server (shaders + DH crash the driver here)");
        overridden = before != null;
    }

    public static void disconnected() {
        if (before == null)
            return;
        boolean was = before;
        before = null;
        overridden = false;
        try {
            Object v = renderingEnabled();
            if (v != null) {
                call(v, "setValue", was);
                call(v, "clearValue");
            }
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: couldn't put Distant Horizons back: {}", e.toString());
        }
    }

    /** is DH drawing? (installed, rendering on in its config, not switched off here for a server) */
    public static boolean drawing() {
        if (overridden)
            return false;
        try {
            Object v = renderingEnabled();
            if (v == null)
                return false;
            return Boolean.TRUE.equals(call(v, "getValue"));
        } catch (Throwable e) {
            if (!drawingWarned) {
                drawingWarned = true;
                TuxMc.LOG.warn("tuxmc: can't ask Distant Horizons whether it draws: {}", e.toString());
            }
            return false;
        }
    }

    private static boolean drawingWarned;

    private DhCompat() {}
}
