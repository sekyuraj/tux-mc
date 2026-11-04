package dev.tux.mc;

import java.io.Reader;
import java.io.Writer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.reflect.TypeToken;

import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import net.minecraft.world.level.Level;

/** waypoints per server (or singleplayer world), in config/tuxmc-waypoints.json */
public final class Waypoints {
    public static final class Point {
        public String server, name, dim;
        public int x, y, z;
        public boolean death, tracked;
        public long created;
    }

    private static final Path FILE = FabricLoader.getInstance().getConfigDir().resolve("tuxmc-waypoints.json");
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static List<Point> all;

    private static List<Point> all() {
        if (all == null) {
            all = new ArrayList<>();
            try (Reader r = Files.newBufferedReader(FILE)) {
                List<Point> l = GSON.fromJson(r, new TypeToken<List<Point>>() {}.getType());
                if (l != null)
                    all.addAll(l);
            } catch (Exception ignored) {
            }
        }
        return all;
    }

    private static void save() {
        try (Writer w = Files.newBufferedWriter(FILE)) {
            GSON.toJson(all(), w);
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: waypoints: {}", e.toString());
        }
    }

    public static String serverKey() {
        Minecraft mc = Minecraft.getInstance();
        if (mc.getCurrentServer() != null)
            return mc.getCurrentServer().ip.toLowerCase();
        if (mc.getSingleplayerServer() != null)
            return "sp:" + mc.getSingleplayerServer().getWorldData().getLevelName();
        return "?";
    }

    public static String dim() {
        Minecraft mc = Minecraft.getInstance();
        if (mc.level == null)
            return "overworld";
        var d = mc.level.dimension();
        return d == Level.NETHER ? "nether" : d == Level.END ? "end" : "overworld";
    }

    public static List<Point> here() {
        String k = serverKey();
        List<Point> l = new ArrayList<>();
        for (Point p : all())
            if (k.equals(p.server))
                l.add(p);
        return l;
    }

    public static Point add(String name, boolean death) {
        Minecraft mc = Minecraft.getInstance();
        if (mc.player == null)
            return null;
        Point p = new Point();
        p.server = serverKey();
        p.dim = dim();
        p.x = mc.player.getBlockX();
        p.y = mc.player.getBlockY();
        p.z = mc.player.getBlockZ();
        p.death = death;
        p.created = System.currentTimeMillis();
        p.name = name != null ? name
                : (death ? "Death " : "Point ") + LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm"));
        all().add(p);
        save();
        return p;
    }

    public static void remove(Point p) {
        all().remove(p);
        save();
    }

    public static void track(Point p) {
        for (Point q : all())
            q.tracked = q == p && !q.tracked;
        save();
    }

    public static Point tracked() {
        String k = serverKey();
        for (Point p : all())
            if (p.tracked && k.equals(p.server))
                return p;
        return null;
    }

    /** the point's position in the dimension you're in now (nether = /8), or null across the End */
    public static double[] inMyDim(Point p) {
        String d = dim();
        double s = 1;
        if (p.dim.equals(d))
            s = 1;
        else if (p.dim.equals("overworld") && d.equals("nether"))
            s = 1 / 8.0;
        else if (p.dim.equals("nether") && d.equals("overworld"))
            s = 8;
        else
            return null;
        return new double[] {p.x * s, p.y, p.z * s};
    }

    private static boolean wasDead;

    /** client tick: a death point when you die */
    public static void tick(Minecraft mc) {
        boolean dead = mc.player != null && mc.player.isDeadOrDying();
        if (dead && !wasDead && Config.deathPoints) {
            Point p = add(null, true);
            if (p != null)
                Notify.show("Death point saved", Config.streamer ? p.name : p.x + " " + p.y + " " + p.z + " (" + p.dim + ")",
                        Theme.urgent, false, false);
        }
        wasDead = dead;
    }

    private Waypoints() {}
}
