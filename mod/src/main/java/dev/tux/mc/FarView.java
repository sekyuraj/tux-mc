package dev.tux.mc;

import net.fabricmc.loader.api.FabricLoader;

/**
 * "Full detail up to N chunks" (Config.fullDetail): with Distant Horizons
 * drawing the far terrain, Sodium only walks, meshes and draws chunks up to N,
 * and DH is told the full-detail area ends at N so its LODs start right there.
 * The render distance itself stays (the game keeps that many chunks loaded and
 * DH builds its LODs from them). At render distance 102 the walk over ~42,000
 * chunk columns was most of the frame: in the Creative world 414 fps at 102,
 * 1160 fps with full detail to 32.
 *
 * Only while DH is actually drawing (not on servers where DhCompat turned it
 * off, not without DH): otherwise there would be nothing past N.
 */
public final class FarView {
    private static final boolean DH = FabricLoader.getInstance().isModLoaded("distanthorizons");
    private static volatile int cap;           /* chunks; 0 = off */
    private static int ticks;
    private static boolean logged;

    /** the full-detail limit in chunks, or 0 */
    public static int cap() { return cap; }

    public static void tick() {
        if (++ticks % 20 != 1)
            return;
        int c = Config.perfOn() ? Config.fullDetail : 0;
        if (System.getenv("TUXMC_FULL_DETAIL") != null)
            c = Integer.parseInt(System.getenv("TUXMC_FULL_DETAIL").trim());
        boolean drawing = c > 0 && (System.getenv("TUXMC_FULL_DETAIL_FORCE") != null || DH && DhCompat.drawing());
        int now = drawing ? c : 0;
        if (now != cap || c > 0 && !logged) {
            logged = true;
            TuxMc.LOG.info("tuxmc: full detail {} (asked {}, DH {}, drawing {})", now == 0 ? "off" : now + " chunks",
                    c, DH, drawing);
        }
        cap = now;
    }

    private FarView() {}
}
