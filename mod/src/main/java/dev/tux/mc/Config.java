package dev.tux.mc;

import java.io.Reader;
import java.io.Writer;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

import net.fabricmc.loader.api.FabricLoader;

/**
 * config/tuxmc.properties: every public static field below is a key. The
 * launcher edits this file too (cape=), and it's re-read every 2 s.
 */
public final class Config {
    /* HUD */
    public static boolean hud = true;
    public static boolean hudCoords = true;
    public static boolean hudArmor = true;          /* armor durability, totems, elytra */
    public static boolean hudServer = true;         /* TPS + queue line */
    public static boolean streamer = false;         /* hide coordinates everywhere */
    public static boolean dhOnServers = false;      /* Distant Horizons on servers (see DhCompat) */
    public static int hudCorner = 0;                /* 0 top left, 1 top right, 2 bottom left, 3 bottom right */
    public static float hudScale = 1f;
    public static float hudOpacity = 0.72f;         /* background of the info box */
    public static boolean hudFacing = true;
    public static boolean waypointArrow = true;
    /* view */
    public static boolean fullbright = false;
    public static boolean zoomSmooth = true;
    public static float zoomFov = 0.25f;
    public static boolean zoomToggle = false;       /* press once instead of holding */
    public static String cape = "tux";              /* a Capes id, "official" or "none" */
    public static String capeImage = "";            /* png for the "image" cape */
    public static String hat = "";                  /* worn hat (config/tuxmc/hat.tuxhat), "" = none; the launcher sets it */
    public static boolean hatHideHelmet = true;     /* no hat while a helmet or a head is worn */
    /* title screen: a picture instead of the panorama (the launcher's Settings, Title screen) */
    public static String menuBg = "";
    public static int menuBlur = 0;                 /* pixels at 1080p */
    public static float menuDim = 0.25f;
    public static boolean menuParallax = true;
    /* sharing: capes + emotes for other tux-mc players, over public Nostr relays */
    public static boolean sync = true;
    public static boolean syncPicture = true;       /* send your Picture cape, and show other people's */
    public static boolean syncHats = true;          /* show other people's hats */
    public static String syncRelays = "wss://relay.damus.io,wss://nos.lol,wss://relay.primal.net";
    /* combat */
    public static boolean hitFix = true;            /* no half-second click block after a miss */
    /* servers */
    public static int queueAlert = 25;              /* notify at this queue position */
    public static boolean visualRange = true;
    public static int nearRadius = 0;               /* blocks; 0 = anyone the server sends (render distance) */
    public static boolean mentions = true;
    public static String mentionWords = "";         /* comma separated, besides your name */
    public static boolean alertSound = true;
    public static boolean desktopAlerts = true;     /* notify-send while the game is in the background */
    public static float toastTime = 4.5f;
    public static boolean autoReconnect = false;
    public static int reconnectDelay = 5;
    public static boolean deathPoints = true;
    /* set once the menu key has been moved off an old "unbound" save */
    public static boolean menuKeyChecked = false;

    private static final Path FILE = FabricLoader.getInstance().getConfigDir().resolve("tuxmc.properties");
    private static long stamp = -1;

    private static Field[] fields() {
        return java.util.Arrays.stream(Config.class.getDeclaredFields())
                .filter(f -> Modifier.isPublic(f.getModifiers()) && Modifier.isStatic(f.getModifiers())
                        && !Modifier.isFinal(f.getModifiers()))
                .toArray(Field[]::new);
    }

    public static void load() {
        try {
            long m = Files.exists(FILE) ? Files.getLastModifiedTime(FILE).toMillis() : 0;
            if (m == stamp)
                return;
            stamp = m;
            if (m == 0) {
                save();
                return;
            }
            Properties p = new Properties();
            try (Reader r = Files.newBufferedReader(FILE)) {
                p.load(r);
            }
            for (Field f : fields()) {
                String v = p.getProperty(f.getName());
                if (v == null)
                    continue;
                v = v.trim();
                try {
                    if (f.getType() == boolean.class)
                        f.setBoolean(null, Boolean.parseBoolean(v));
                    else if (f.getType() == int.class)
                        f.setInt(null, Integer.parseInt(v));
                    else if (f.getType() == float.class)
                        f.setFloat(null, Float.parseFloat(v));
                    else
                        f.set(null, v);
                } catch (NumberFormatException ignored) {
                }
            }
            /* capes that were removed in 1.2: wear Tux instead of nothing */
            if (java.util.Set.of("theme", "aurora", "ember", "ocean").contains(cape)) {
                cape = "tux";
                save();
            }
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: config: {}", e.toString());
        }
    }

    public static void save() {
        Properties p = new Properties();
        try {
            for (Field f : fields())
                p.setProperty(f.getName(), String.valueOf(f.get(null)));
        } catch (IllegalAccessException ignored) {
        }
        try (Writer w = Files.newBufferedWriter(FILE)) {
            p.store(w, "tuxmc (also edited by the tux-mc launcher)");
            stamp = Files.getLastModifiedTime(FILE).toMillis();
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: config save: {}", e.toString());
        }
    }

    private Config() {}
}
