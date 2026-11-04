package dev.tux.mc;

import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.ConnectScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.client.gui.screens.multiplayer.JoinMultiplayerScreen;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.multiplayer.resolver.ServerAddress;
import net.minecraft.client.player.AbstractClientPlayer;
import net.minecraft.network.chat.Component;

/** things for big anarchy servers: queue, TPS, visual range, mentions, reconnect */
public final class ServerTools {
    /* ---- queue ---- */
    private static final Pattern QUEUE = Pattern.compile("(?i)position in queue:?\\s*(\\d+)");
    public static int queuePos = -1;
    public static long queueSeen;
    private static boolean queueAlerted, inAlerted;
    private static final Deque<long[]> queueHist = new ArrayDeque<>();   /* {ms, pos} */

    public static boolean inQueue() {
        return queuePos >= 0 && System.currentTimeMillis() - queueSeen < 60_000;
    }

    /** minutes left, or -1 while there's not enough history */
    public static double queueEta() {
        if (queueHist.size() < 2)
            return -1;
        long[] a = queueHist.peekFirst(), b = queueHist.peekLast();
        double min = (b[0] - a[0]) / 60000.0, moved = a[1] - b[1];
        if (min < 0.5 || moved <= 0)
            return -1;
        return b[1] / (moved / min);
    }

    /** positions per minute */
    public static double queueRate() {
        if (queueHist.size() < 2)
            return 0;
        long[] a = queueHist.peekFirst(), b = queueHist.peekLast();
        double min = (b[0] - a[0]) / 60000.0;
        return min > 0 ? (a[1] - b[1]) / min : 0;
    }

    /** every chat line, action bar, title and tab header goes through here */
    public static void text(Component c) {
        if (c == null)
            return;
        String s = c.getString();
        Matcher m = QUEUE.matcher(s);
        if (m.find()) {
            int pos = Integer.parseInt(m.group(1));
            long now = System.currentTimeMillis();
            if (pos != queuePos) {
                queueHist.addLast(new long[] {now, pos});
                while (queueHist.size() > 2 && now - queueHist.peekFirst()[0] > 15 * 60_000)
                    queueHist.removeFirst();
            }
            if (queuePos < 0)
                queueAlerted = inAlerted = false;
            queuePos = pos;
            queueSeen = now;
            if (!queueAlerted && pos <= Config.queueAlert) {
                queueAlerted = true;
                Notify.show("Queue: position " + pos, "almost in, get ready", Theme.accentBright, true, true);
                Notify.desktop("Minecraft queue: " + pos, "You're almost in.");
            }
            return;
        }
        if (inQueue() && !inAlerted && s.toLowerCase().contains("connected to the server")) {
            inAlerted = true;
            queuePos = -1;
            queueHist.clear();
            Notify.show("You're in!", "queue done", Theme.accentBright, true, true);
            Notify.desktop("Minecraft: you're in!", "The queue is done.");
        }
    }

    public static void chat(Component c) {
        text(c);
        if (!Config.mentions || c == null)
            return;
        Minecraft mc = Minecraft.getInstance();
        if (mc.player == null)
            return;
        String s = c.getString().toLowerCase();
        String me = mc.player.getName().getString().toLowerCase();
        /* skip our own messages ("<name> ...") */
        if (s.startsWith("<" + me + ">"))
            return;
        boolean hit = s.contains(me);
        for (String w : Config.mentionWords.split(","))
            if (!w.isBlank() && s.contains(w.trim().toLowerCase()))
                hit = true;
        if (hit)
            Notify.show("Mentioned", c.getString().length() > 60 ? c.getString().substring(0, 60) + "…" : c.getString(),
                    Theme.warn, true, true);
    }

    /* ---- TPS from the server's time updates (one per second at 20 TPS) ---- */
    private static long lastGameTime = -1, lastReal;
    public static float tps = 20;
    public static boolean tpsKnown;
    public static final float[] tpsHist = new float[90];
    public static int tpsSamples;

    public static void time(long gameTime) {
        long now = System.nanoTime();
        if (lastGameTime >= 0) {
            double secs = (now - lastReal) / 1e9;
            long ticks = gameTime - lastGameTime;
            if (secs > 0.3 && ticks >= 0 && ticks < 2000) {
                float t = (float) Math.min(20, ticks / secs);
                tps = tpsKnown ? tps * 0.6f + t * 0.4f : t;
                tpsKnown = true;
                System.arraycopy(tpsHist, 1, tpsHist, 0, tpsHist.length - 1);
                tpsHist[tpsHist.length - 1] = tps;
                tpsSamples = Math.min(tpsHist.length, tpsSamples + 1);
            }
        }
        lastGameTime = gameTime;
        lastReal = now;
    }

    /* ---- visual range ---- */
    public record Sighting(String name, String time, boolean entered, int x, int y, int z) {}

    public static final List<Sighting> sightings = new ArrayList<>();
    private static final Map<UUID, String> near = new HashMap<>();
    private static final DateTimeFormatter HM = DateTimeFormatter.ofPattern("HH:mm:ss");

    public static int nearCount() { return near.size(); }

    public static void tick(Minecraft mc) {
        if (mc.level == null || mc.player == null) {
            near.clear();
            lastGameTime = -1;
            tpsKnown = false;
            return;
        }
        rememberServer(mc);
        Map<UUID, String> now = new HashMap<>();
        int r = Config.nearRadius;
        for (AbstractClientPlayer p : mc.level.players()) {
            if (p == mc.player)
                continue;
            if (r > 0) {
                /* 4 blocks of slack for someone already near, so standing on the edge doesn't spam */
                double lim = near.containsKey(p.getUUID()) ? r + 4 : r;
                if (p.distanceToSqr(mc.player) > lim * lim)
                    continue;
            }
            now.put(p.getUUID(), p.getName().getString());
        }
        for (var e : now.entrySet())
            if (!near.containsKey(e.getKey())) {
                AbstractClientPlayer p = (AbstractClientPlayer) mc.level.getPlayerByUUID(e.getKey());
                log(e.getValue(), true, p);
                if (Config.visualRange)
                    Notify.show(e.getValue(), r > 0 ? "within " + r + " blocks" : "entered visual range", Theme.urgent, true, true);
            }
        for (var e : near.entrySet())
            if (!now.containsKey(e.getKey()))
                log(e.getValue(), false, null);
        near.clear();
        near.putAll(now);
    }

    private static void log(String name, boolean entered, AbstractClientPlayer p) {
        sightings.add(0, new Sighting(name, LocalTime.now().format(HM), entered,
                p == null ? 0 : p.getBlockX(), p == null ? 0 : p.getBlockY(), p == null ? 0 : p.getBlockZ()));
        while (sightings.size() > 60)
            sightings.remove(sightings.size() - 1);
    }

    /* ---- reconnect ---- */
    public static ServerData lastServer;

    private static void rememberServer(Minecraft mc) {
        if (mc.getCurrentServer() != null)
            lastServer = mc.getCurrentServer();
    }

    public static void reconnect() {
        if (lastServer == null)
            return;
        Minecraft mc = Minecraft.getInstance();
        ConnectScreen.startConnecting(new JoinMultiplayerScreen(new TitleScreen()), mc,
                ServerAddress.parseString(lastServer.ip), lastServer, false, null);
    }

    private ServerTools() {}
}
