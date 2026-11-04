package dev.tux.mc;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ServerData;

/**
 * Every socket this game process has open (libtuxmc reads them from the
 * kernel; `ss` if that's missing), sampled once a second while the menu is open.
 */
public final class NetMonitor {
    public static final class Peer {
        public final String key;          /* proto|remote ip:port */
        public String ip, proto, label, host;
        public int port;
        public long sent, recv;           /* bytes so far */
        public float upRate, downRate;    /* bytes/s, smoothed */
        public long lastSeen;             /* ms; gone peers fade out */
        public boolean game;
        public float appear;              /* 0..1, the menu animates this */

        Peer(String key) { this.key = key; }
    }

    private static final Map<String, Peer> peers = new ConcurrentHashMap<>();
    private static final Map<String, String> dns = new ConcurrentHashMap<>();
    private static final Pattern BYTES = Pattern.compile("bytes_(sent|received):(\\d+)");
    private static volatile long wantedUntil;
    private static Thread thread;
    private static final long PID = ProcessHandle.current().pid();

    /** keep sampling for a few more seconds (called every frame the map is visible) */
    public static synchronized void want() {
        wantedUntil = System.currentTimeMillis() + 3000;
        resolveKnown();
        if (thread != null && thread.isAlive())
            return;
        thread = new Thread(NetMonitor::loop, "tuxmc-net");
        thread.setDaemon(true);
        thread.start();
    }

    /** live peers plus ones that closed in the last 4 s */
    public static List<Peer> peers() {
        long now = System.currentTimeMillis();
        List<Peer> l = new ArrayList<>();
        for (Peer p : peers.values())
            if (now - p.lastSeen < 4000)
                l.add(p);
            else
                peers.remove(p.key);
        l.sort((a, b) -> a.game != b.game ? (a.game ? -1 : 1) : a.key.compareTo(b.key));
        return l;
    }

    public static boolean alive(Peer p) {
        return System.currentTimeMillis() - p.lastSeen < 1600;
    }

    private static void loop() {
        while (System.currentTimeMillis() < wantedUntil) {
            try {
                sample();
                Thread.sleep(1000);
            } catch (InterruptedException e) {
                return;
            } catch (Exception e) {
                TuxMc.LOG.debug("tuxmc net: {}", e.toString());
                try { Thread.sleep(2000); } catch (InterruptedException ie) { return; }
            }
        }
    }

    private static void sample() throws Exception {
        List<Native.Conn> conns = Native.sockets();
        if (conns == null)
            conns = ss();
        String gameAddr = gameRemote();
        long now = System.currentTimeMillis();
        for (Native.Conn c : conns) {
            Peer p = peers.computeIfAbsent(c.proto() + "|" + c.ip() + ":" + c.port(), Peer::new);
            boolean fresh = p.lastSeen == 0;
            p.proto = c.proto();
            p.ip = c.ip();
            p.port = c.port();
            if (!fresh) {
                float dt = Math.max(0.2f, (now - p.lastSeen) / 1000f);
                p.upRate = p.upRate * 0.4f + Math.max(0, c.sent() - p.sent) / dt * 0.6f;
                p.downRate = p.downRate * 0.4f + Math.max(0, c.recv() - p.recv) / dt * 0.6f;
            }
            p.sent = c.sent();
            p.recv = c.recv();
            p.lastSeen = now;
            p.game = gameAddr != null && gameAddr.equals(p.ip + ":" + p.port);
            p.host = dns.computeIfAbsent(p.ip, NetMonitor::lookup);
            p.label = label(p);
        }
    }

    /** fallback when libtuxmc couldn't load: parse `ss -tunpi` */
    private static List<Native.Conn> ss() throws Exception {
        Process pr = new ProcessBuilder("ss", "-tunpiH").redirectErrorStream(true).start();
        List<Native.Conn> out = new ArrayList<>();
        String proto = null, ip = null;
        int port = 0;
        long[] b = null;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(pr.getInputStream()))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (!line.isEmpty() && !Character.isWhitespace(line.charAt(0))) {
                    if (b != null)
                        out.add(new Native.Conn(proto, ip, port, b[0], b[1]));
                    b = null;
                    if (!line.contains("pid=" + PID + ","))
                        continue;
                    String[] f = line.trim().split("\\s+");
                    if (f.length < 6)
                        continue;
                    String remote = f[5];
                    int c = remote.lastIndexOf(':');
                    if (c < 0 || remote.startsWith("*"))
                        continue;
                    ip = remote.substring(0, c).replace("[", "").replace("]", "");
                    if (ip.startsWith("::ffff:"))
                        ip = ip.substring(7);
                    if (ip.equals("0.0.0.0") || ip.equals("::"))
                        continue;
                    proto = f[0];
                    port = Integer.parseInt(remote.substring(c + 1));
                    b = new long[2];
                } else if (b != null) {
                    Matcher m = BYTES.matcher(line);
                    while (m.find())
                        b[m.group(1).equals("sent") ? 0 : 1] = Long.parseLong(m.group(2));
                }
            }
        }
        if (b != null)
            out.add(new Native.Conn(proto, ip, port, b[0], b[1]));
        pr.waitFor();
        return out;
    }

    private static String gameRemote() {
        try {
            var c = Minecraft.getInstance().getConnection();
            if (c == null)
                return null;
            SocketAddress a = c.getConnection().getRemoteAddress();
            if (a instanceof InetSocketAddress i && i.getAddress() != null) {
                String ip = i.getAddress().getHostAddress();
                if (ip.startsWith("::ffff:"))
                    ip = ip.substring(7);
                return ip + ":" + i.getPort();
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    /* forward-resolve the hosts Minecraft and common mods talk to, so their IPs get real names */
    private static final String[][] KNOWN = {
        {"session.minecraft.net", "Mojang session"}, {"sessionserver.mojang.com", "Mojang session"},
        {"textures.minecraft.net", "Mojang skins"}, {"api.minecraftservices.com", "Minecraft services"},
        {"api.mojang.com", "Mojang API"}, {"pc.realms.minecraft.net", "Realms"},
        {"resources.download.minecraft.net", "Mojang assets"}, {"piston-meta.mojang.com", "Mojang meta"},
        {"libraries.minecraft.net", "Mojang libraries"}, {"login.microsoftonline.com", "Microsoft login"},
        {"user.auth.xboxlive.com", "Xbox Live auth"}, {"xsts.auth.xboxlive.com", "Xbox Live auth"},
        {"api.modrinth.com", "Modrinth"}, {"cdn.modrinth.com", "Modrinth CDN"},
        {"meta.fabricmc.net", "Fabric meta"}, {"maven.fabricmc.net", "Fabric maven"},
        {"api.curseforge.com", "CurseForge"}, {"api.github.com", "GitHub"},
        {"raw.githubusercontent.com", "GitHub"}, {"aka.ms", "Microsoft"},
    };
    private static final Map<String, String> known = new ConcurrentHashMap<>();
    private static volatile boolean knownStarted;

    private static void resolveKnown() {
        if (knownStarted)
            return;
        knownStarted = true;
        Thread t = new Thread(() -> {
            for (String[] h : KNOWN)
                try {
                    for (InetAddress a : InetAddress.getAllByName(h[0]))
                        known.putIfAbsent(a.getHostAddress(), h[1] + "|" + h[0]);
                } catch (Exception ignored) {
                }
        }, "tuxmc-known");
        t.setDaemon(true);
        t.start();
    }

    /** big providers by address block, for IPs without a useful reverse name */
    private static String provider(String ip) {
        String[] cf = {"104.16.", "104.17.", "104.18.", "104.19.", "104.20.", "104.21.", "104.22.", "104.23.",
                "104.24.", "104.25.", "104.26.", "104.27.", "172.64.", "172.65.", "172.66.", "172.67.",
                "188.114.9", "162.158.", "162.159.", "141.101.", "108.162.", "173.245.", "2606:4700"};
        for (String p : cf)
            if (ip.startsWith(p))
                return "Cloudflare";
        String[] ms = {"20.", "40.", "13.", "52.", "150.171.", "2603:"};
        for (String p : ms)
            if (ip.startsWith(p))
                return "Microsoft / Azure";
        if (ip.startsWith("3.") || ip.startsWith("18.") || ip.startsWith("54.") || ip.startsWith("35.1"))
            return "AWS";
        if (ip.startsWith("34.") || ip.startsWith("35.") || ip.startsWith("142.250.") || ip.startsWith("2a00:1450"))
            return "Google Cloud";
        return null;
    }

    private static String lookup(String ip) {
        Thread t = new Thread(() -> {
            try {
                String h = InetAddress.getByName(ip).getCanonicalHostName();
                dns.put(ip, h.equals(ip) ? "" : h);
            } catch (Exception e) {
                dns.put(ip, "");
            }
        }, "tuxmc-dns");
        t.setDaemon(true);
        t.start();
        return "";
    }

    private static String label(Peer p) {
        if (p.game) {
            ServerData s = Minecraft.getInstance().getCurrentServer();
            return s != null && s.name != null && !s.name.isBlank() ? s.name : "Game server";
        }
        String k = known.get(p.ip);
        if (k != null) {
            p.host = k.substring(k.indexOf('|') + 1);
            return k.substring(0, k.indexOf('|'));
        }
        String h = p.host == null ? "" : p.host.toLowerCase();
        if (h.isEmpty()) {
            String prov = provider(p.ip);
            if (prov != null)
                return prov + (p.port == 443 ? " (HTTPS)" : "");
        }
        if (h.contains("mojang") || h.contains("minecraft"))
            return "Mojang";
        if (h.contains("microsoft") || h.contains("xboxlive") || h.contains("live.com"))
            return "Microsoft";
        if (h.contains("amazonaws") || h.contains("cloudfront"))
            return "AWS";
        if (h.contains("akamai"))
            return "Akamai CDN";
        if (h.contains("1e100") || h.contains("google"))
            return "Google";
        if (h.contains("cloudflare"))
            return "Cloudflare";
        if (p.port == 25565)
            return "Minecraft server";
        if (p.port == 53)
            return "DNS";
        if (p.proto.equals("udp"))
            return "Voice / UDP";
        if (p.port == 443 || p.port == 80)
            return "Web API";
        return "Port " + p.port;
    }

    private NetMonitor() {}
}
