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
    public static void connecting(Minecraft mc) {
        if (mc.getSingleplayerServer() != null || Config.dhOnServers)
            return;
        try {
            Object v = renderingEnabled();
            if (v == null)
                return;
            v.getClass().getMethod("setValue", Object.class).invoke(v, Boolean.FALSE);
            overridden = true;
            TuxMc.LOG.info("tuxmc: Distant Horizons off on this server (shaders + DH crash the driver here)");
        } catch (ClassNotFoundException e) {
            /* no DH */
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: couldn't switch Distant Horizons off: {}", e.toString());
        }
    }

    public static void disconnected() {
        if (!overridden)
            return;
        overridden = false;
        try {
            Object v = renderingEnabled();
            if (v != null)
                v.getClass().getMethod("clearValue").invoke(v);
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: couldn't switch Distant Horizons back on: {}", e.toString());
        }
    }

    private DhCompat() {}
}
