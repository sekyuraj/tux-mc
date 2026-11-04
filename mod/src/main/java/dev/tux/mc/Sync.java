package dev.tux.mc;

import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.PublicKey;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import javax.imageio.ImageIO;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.AbstractClientPlayer;
import net.minecraft.util.Crypt;
import net.minecraft.util.SignatureValidator;
import net.minecraft.util.Signer;
import net.minecraft.world.entity.player.ProfileKeyPair;
import net.minecraft.world.entity.player.ProfilePublicKey;

/**
 * Capes and emotes for other tux-mc players, over public Nostr relays. No server
 * of our own, and players never connect to each other, so nobody sees anyone
 * else's IP (the relays see yours, like any website).
 *
 * - cape: one stored event (kind 30078, tagged with your UUID), sent once per
 *   server you join and when you change cape. Others ask for the UUIDs they see.
 * - emote: one ephemeral event (kind 20078) with the emote number, tagged with a
 *   hash of the server address, so only people on the same server get it.
 *
 * Every event carries your Mojang chat key (the one the game already uses to
 * sign chat) and a signature made with it. Receivers check Mojang signed that key
 * for that UUID, so nobody can put a cape or an emote on someone else.
 */
public final class Sync {
    public static final int KIND_CAPE = 30078, KIND_EMOTE = 20078;
    private static final String TAG = "tuxmc";
    private static final int MAX_PICTURE = 60_000;          /* base64 chars */
    /* test: take your own events back as if another player sent them */
    private static final boolean SELF = System.getenv("TUXMC_SYNC_SELF") != null;

    public record RemoteCape(String id, int[] pal, int[] img, int w, int h, long at) {}

    public record RemoteEmote(Emotes.Emote emote, long started, double[] at) {
        public float time() { return (System.nanoTime() - started) / 1e9f; }
    }

    private static final Map<UUID, RemoteCape> capes = new ConcurrentHashMap<>();
    private static final Map<UUID, RemoteEmote> emotes = new ConcurrentHashMap<>();
    private static final Map<UUID, long[]> rate = new ConcurrentHashMap<>();
    private static final Map<String, PublicKey> trusted = new ConcurrentHashMap<>();
    /* ids of the last events seen, oldest dropped first */
    private static final Set<String> seen = java.util.Collections.newSetFromMap(java.util.Collections.synchronizedMap(
            new java.util.LinkedHashMap<String, Boolean>(512, 0.75f, false) {
                @Override
                protected boolean removeEldestEntry(Map.Entry<String, Boolean> e) { return size() > 2000; }
            }));
    private static final Set<UUID> wanted = new LinkedHashSet<>();
    private static final List<Relay> relays = new ArrayList<>();
    private static final ExecutorService work = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "tuxmc-sync");
        t.setDaemon(true);
        return t;
    });
    private static final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(8)).build();

    private static byte[] secret;
    private static String pub;
    private static CompletableFuture<java.util.Optional<ProfileKeyPair>> keyFuture;
    private static volatile ProfileKeyPair keys;
    private static volatile boolean noKeys;
    private static long keyRetry;
    private static volatile String server;        /* hashed address of the server we're on, null = none */
    private static String capeSent = "";
    private static volatile String capeMsg;           /* our last signed cape event */
    private static final long[] emoteTimes = new long[6];
    private static long capeDue, askDue;
    private static int askedCount;

    /* ---------------------------------------------------------------- state */

    /** another player's synced cape, or null */
    public static RemoteCape cape(UUID id) {
        return Config.sync ? capes.get(id) : null;
    }

    public static RemoteEmote emote(UUID id) {
        return Config.sync ? emotes.get(id) : null;
    }

    /** for the settings page */
    public static String status() {
        if (!Config.sync)
            return "off";
        if (noKeys)
            return "needs a Microsoft account";
        if (server == null)
            return "starts when you join a server";
        long up = relays.stream().filter(r -> r.open).count();
        return up + "/" + relays.size() + " relays, " + capes.size() + (capes.size() == 1 ? " player" : " players");
    }

    /* ----------------------------------------------------------------- tick */

    public static void tick(Minecraft mc) {
        String now = Config.sync && mc.player != null && mc.level != null && mc.getCurrentServer() != null
                ? serverTag(mc.getCurrentServer().ip) : null;
        if (now == null) {
            if (server != null)
                leave();
            return;
        }
        if (!loadKeys(mc))
            return;
        if (!now.equals(server)) {
            leave();
            server = now;
            capeSent = "";
            capeDue = 0;
            connect();
        }
        long t = System.currentTimeMillis();
        for (Relay r : relays)
            if (!r.open && !r.connecting && t >= r.retryAt)
                r.connect();

        /* cape: once per server, and again when you change it */
        String c = capeKey();
        if (!c.equals(capeSent) && t >= capeDue) {
            capeSent = c;
            capeDue = t + 3000;
            publishCape();
        }

        /* ask for the capes of players we see */
        boolean grew = false;
        for (AbstractClientPlayer p : mc.level.players())
            if (p != mc.player && wanted.add(p.getUUID()))
                grew = true;
        if (SELF && wanted.add(uuid()))
            grew = true;
        if (SELF && grew)
            TuxMc.LOG.info("tuxmc: sync: asking for {} capes ({} relays open)", wanted.size(), relays.stream().filter(r -> r.open).count());
        if (grew && t >= askDue) {
            askDue = t + 2000;
            askCapes();
        }

        /* remote emotes end like yours: timed ones run out, any ends when they walk off */
        for (var e : emotes.entrySet()) {
            RemoteEmote re = e.getValue();
            var p = SELF && e.getKey().equals(uuid()) ? mc.player : mc.level.getPlayerByUUID(e.getKey());
            if (p == null) {
                if (re.time() > 30)
                    emotes.remove(e.getKey());
                continue;
            }
            if (re.at[0] != re.at[0]) {              /* NaN: first tick we see them */
                re.at[0] = p.getX();
                re.at[1] = p.getZ();
            }
            double moved = Math.hypot(p.getX() - re.at[0], p.getZ() - re.at[1]);
            if ((re.emote.length > 0 && re.time() > re.emote.length) || moved > 0.35)
                emotes.remove(e.getKey());
        }
    }

    /** test switch on: your own player is drawn from the synced data, like anyone else */
    public static java.util.UUID selfTest(java.util.UUID entity) {
        return SELF ? uuid() : null;
    }

    private static void leave() {
        capeMsg = null;
        for (Relay r : relays)
            r.close();
        relays.clear();
        server = null;
        emotes.clear();
        wanted.clear();
        askedCount = 0;
    }

    private static void connect() {
        for (String u : Config.syncRelays.split(",")) {
            u = u.trim();
            if (u.startsWith("wss://") || u.startsWith("ws://"))
                relays.add(new Relay(u));
        }
    }

    /**
     * Your Mojang chat key, fetched straight from Mojang (POST player/certificates,
     * what the game's own key manager does). Done ourselves because No Chat Reports
     * and similar mods switch the game's key manager off; this key only signs
     * tux-mc events and never goes to a Minecraft server.
     */
    private static boolean loadKeys(Minecraft mc) {
        ProfileKeyPair k = keys;
        if (k != null && !k.dueRefresh())
            return true;
        if (noKeys || keyFuture != null && !keyFuture.isDone())
            return k != null;
        if (System.currentTimeMillis() < keyRetry)
            return k != null;
        String token = mc.getUser().getAccessToken();
        var req = java.net.http.HttpRequest.newBuilder(URI.create("https://api.minecraftservices.com/player/certificates"))
                .header("Authorization", "Bearer " + token).header("Content-Type", "application/json")
                .timeout(Duration.ofSeconds(15)).POST(java.net.http.HttpRequest.BodyPublishers.ofString("")).build();
        keyFuture = http.sendAsync(req, java.net.http.HttpResponse.BodyHandlers.ofString()).thenApply(r -> {
            if (r.statusCode() == 401 || r.statusCode() == 403) {
                noKeys = true;
                TuxMc.LOG.info("tuxmc: sync off, Mojang gave no chat key ({}): offline account?", r.statusCode());
                return java.util.Optional.<ProfileKeyPair>empty();
            }
            if (r.statusCode() != 200)
                throw new IllegalStateException("player/certificates " + r.statusCode());
            try {
                JsonObject o = JsonParser.parseString(r.body()).getAsJsonObject();
                JsonObject kp = o.getAsJsonObject("keyPair");
                ProfilePublicKey.Data d = new ProfilePublicKey.Data(Instant.parse(o.get("expiresAt").getAsString()),
                        Crypt.stringToRsaPublicKey(kp.get("publicKey").getAsString()),
                        Base64.getDecoder().decode(o.get("publicKeySignatureV2").getAsString()));
                ProfileKeyPair p = new ProfileKeyPair(Crypt.stringToPemRsaPrivateKey(kp.get("privateKey").getAsString()),
                        new ProfilePublicKey(d), Instant.parse(o.get("refreshedAfter").getAsString()));
                keys = p;
                return java.util.Optional.of(p);
            } catch (Exception e) {
                throw new IllegalStateException(e);
            }
        });
        keyFuture.whenComplete((v, err) -> {
            if (err != null) {
                keyRetry = System.currentTimeMillis() + 60_000;
                TuxMc.LOG.warn("tuxmc: sync: chat key: {}", err.toString());
            }
        });
        if (secret == null)
            loadSecret();
        return k != null;
    }

    private static void loadSecret() {
        Path f = FabricLoader.getInstance().getConfigDir().resolve("tuxmc").resolve("nostr.key");
        try {
            if (Files.exists(f))
                secret = Nostr.unhex(Files.readString(f).trim());
        } catch (Exception e) {
            secret = null;
        }
        if (secret == null || secret.length != 32) {
            secret = Nostr.newSecret();
            try {
                Files.createDirectories(f.getParent());
                Files.writeString(f, Nostr.hex(secret) + "\n");
            } catch (Exception e) {
                TuxMc.LOG.warn("tuxmc: sync key: {}", e.toString());
            }
        }
        pub = Nostr.hex(Nostr.pubkey(secret));
    }

    /** sha256 of the address, so the tag doesn't spell out the server */
    static String serverTag(String ip) {
        String a = ip.trim().toLowerCase();
        if (a.endsWith(":25565"))
            a = a.substring(0, a.length() - 6);
        if (a.endsWith("."))
            a = a.substring(0, a.length() - 1);
        return Nostr.hex(Nostr.sha256(("tuxmc-server:" + a).getBytes(StandardCharsets.UTF_8))).substring(0, 32);
    }

    /* ------------------------------------------------------------- sending */

    private static String capeKey() {
        String c = Config.cape;
        if (c.equals("image"))
            return c + Config.syncPicture + Config.capeImage + "@" + mtime(Config.capeImage);
        return c;
    }

    private static void publishCape() {
        JsonObject d = new JsonObject();
        String c = Config.cape;
        d.addProperty("c", Capes.ALL.containsKey(c) || c.equals("official") || c.equals("none") ? c : "official");
        String path = Config.capeImage;
        boolean picture = c.equals("image") && Config.syncPicture && !path.isBlank();
        work.execute(() -> {
            if (picture) {
                String png = facePng(path);
                if (png == null)
                    return;
                d.addProperty("i", png);
            }
            send(KIND_CAPE, List.of(new String[] {"d", "tuxmc-cape"}, new String[] {"t", TAG},
                    new String[] {"u", uuid().toString()}), d.toString());
        });
    }

    /** called by Emotes.play / stop; null = stopped */
    public static void sendEmote(Emotes.Emote e) {
        if (!Config.sync || server == null || keys == null)
            return;
        /* at most 6 emotes per 10 s (a stop always goes out, so nobody gets stuck dancing) */
        long now = System.currentTimeMillis();
        if (e != null) {
            int oldest = 0;
            for (int i = 1; i < emoteTimes.length; i++)
                if (emoteTimes[i] < emoteTimes[oldest])
                    oldest = i;
            if (now - emoteTimes[oldest] < 10_000)
                return;
            emoteTimes[oldest] = now;
        }
        JsonObject d = new JsonObject();
        d.addProperty("e", e == null ? -1 : e.ordinal());
        d.addProperty("s", server);
        String s = server;
        work.execute(() -> send(KIND_EMOTE, List.of(new String[] {"t", TAG}, new String[] {"s", s}), d.toString()));
    }

    private static UUID uuid() {
        return Minecraft.getInstance().getUser().getProfileId();
    }

    /** runs on the sync thread */
    private static void send(int kind, List<String[]> tags, String d) {
        ProfileKeyPair k = keys;
        if (k == null || secret == null)
            return;
        long at = Instant.now().getEpochSecond();
        UUID me = uuid();
        try {
            byte[] sig = Signer.from(k.privateKey(), "SHA256withRSA").sign(signed(kind, me, pub, at, d));
            ProfilePublicKey.Data kd = k.publicKey().data();
            JsonObject o = new JsonObject();
            o.addProperty("v", 1);
            o.addProperty("u", me.toString());
            o.addProperty("d", d);
            o.addProperty("k", Base64.getEncoder().encodeToString(kd.key().getEncoded()));
            o.addProperty("x", kd.expiresAt().toEpochMilli());
            o.addProperty("ks", Base64.getEncoder().encodeToString(kd.keySignature()));
            o.addProperty("sg", Base64.getEncoder().encodeToString(sig));
            String msg = "[\"EVENT\"," + Nostr.event(secret, at, kind, tags, o.toString()) + "]";
            if (kind == KIND_CAPE)
                capeMsg = msg;
            Minecraft.getInstance().execute(() -> {
                for (Relay r : relays)
                    r.send(msg);
            });
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: sync send: {}", e.toString());
        }
    }

    private static byte[] signed(int kind, UUID u, String nostrPub, long at, String d) {
        return ("tuxmc1\n" + kind + "\n" + u + "\n" + nostrPub + "\n" + at + "\n" + d).getBytes(StandardCharsets.UTF_8);
    }

    /** your picture, cover-fitted to the cape's 10:16 face and kept small */
    private static String facePng(String path) {
        try {
            BufferedImage src = ImageIO.read(Path.of(path.replaceFirst("^~", System.getProperty("user.home"))).toFile());
            if (src == null)
                return null;
            for (int w = 80; w >= 20; w /= 2) {
                int h = w * 16 / 10;
                BufferedImage out = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
                double face = 10.0 / 16, ia = (double) src.getWidth() / src.getHeight();
                double cw = ia > face ? src.getHeight() * face : src.getWidth();
                double ch = ia > face ? src.getHeight() : src.getWidth() / face;
                double cx = (src.getWidth() - cw) / 2, cy = (src.getHeight() - ch) / 2;
                var g = out.createGraphics();
                g.setRenderingHint(java.awt.RenderingHints.KEY_INTERPOLATION,
                        java.awt.RenderingHints.VALUE_INTERPOLATION_BILINEAR);
                g.drawImage(src, 0, 0, w, h, (int) cx, (int) cy, (int) (cx + cw), (int) (cy + ch), null);
                g.dispose();
                ByteArrayOutputStream b = new ByteArrayOutputStream();
                ImageIO.write(out, "png", b);
                String s = Base64.getEncoder().encodeToString(b.toByteArray());
                if (s.length() <= MAX_PICTURE)
                    return s;
            }
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: sync picture {}: {}", path, e.toString());
        }
        return null;
    }

    private static void askCapes() {
        /* one subscription; sending REQ again with the same id replaces its filter */
        List<UUID> all = new ArrayList<>(wanted);
        if (all.size() > 200)
            all = all.subList(all.size() - 200, all.size());
        askedCount = all.size();
        JsonArray u = new JsonArray();
        for (UUID id : all)
            u.add(id.toString());
        JsonObject f = new JsonObject();
        JsonArray kinds = new JsonArray();
        kinds.add(KIND_CAPE);
        f.add("kinds", kinds);
        f.add("#u", u);
        JsonArray t = new JsonArray();
        t.add(TAG);
        f.add("#t", t);
        String msg = "[\"REQ\",\"tuxmc-capes\"," + f + "]";
        for (Relay r : relays)
            r.capeReq = msg;
        for (Relay r : relays)
            r.send(msg);
    }

    private static String emoteReq() {
        return "[\"REQ\",\"tuxmc-emotes\",{\"kinds\":[" + KIND_EMOTE + "],\"#s\":[\"" + server + "\"],\"since\":"
                + (Instant.now().getEpochSecond() - 3) + "}]";
    }

    /* ----------------------------------------------------------- receiving */

    /** runs on a relay thread */
    private static void received(String msg) {
        try {
            JsonArray a = JsonParser.parseString(msg).getAsJsonArray();
            if (SELF && !a.get(0).getAsString().equals("EVENT"))
                TuxMc.LOG.info("tuxmc: sync: relay says {}", msg.length() > 200 ? msg.substring(0, 200) : msg);
            if (a.size() < 3 || !a.get(0).getAsString().equals("EVENT"))
                return;
            JsonObject ev = a.get(2).getAsJsonObject();
            int kind = ev.get("kind").getAsInt();
            if (kind != KIND_CAPE && kind != KIND_EMOTE)
                return;
            /* every relay sends us the same event: only the first copy counts */
            if (!seen.add(ev.get("id").getAsString()))
                return;
            if (SELF)
                TuxMc.LOG.info("tuxmc: sync: got kind {} on {}", kind, a.get(1).getAsString());
            String evPub = ev.get("pubkey").getAsString();
            long at = ev.get("created_at").getAsLong();
            String uTag = null, sTag = null;
            boolean ours = false;
            for (var tg : ev.getAsJsonArray("tags")) {
                JsonArray t = tg.getAsJsonArray();
                if (t.size() < 2)
                    continue;
                String k = t.get(0).getAsString(), v = t.get(1).getAsString();
                if (k.equals("t") && v.equals(TAG))
                    ours = true;
                else if (k.equals("u"))
                    uTag = v;
                else if (k.equals("s"))
                    sTag = v;
            }
            if (!ours)
                return;
            JsonObject o = JsonParser.parseString(ev.get("content").getAsString()).getAsJsonObject();
            UUID u = UUID.fromString(o.get("u").getAsString());
            if (u.equals(uuid()) && !SELF)
                return;
            String d = o.get("d").getAsString();
            if (kind == KIND_CAPE && !u.toString().equals(uTag))
                return;
            if (kind == KIND_EMOTE && (Math.abs(Instant.now().getEpochSecond() - at) > 15 || !String.valueOf(server).equals(sTag)))
                return;
            if (!verify(u, o, kind, evPub, at, d)) {
                if (SELF)
                    TuxMc.LOG.info("tuxmc: sync: signature check failed for {}", u);
                return;
            }
            JsonObject dj = JsonParser.parseString(d).getAsJsonObject();
            if (kind == KIND_CAPE)
                gotCape(u, dj, at);
            else
                gotEmote(u, dj);
        } catch (Exception e) {
            if (SELF)
                TuxMc.LOG.info("tuxmc: sync: dropped event: {}", e.toString());
        }
    }

    private static boolean verify(UUID u, JsonObject o, int kind, String evPub, long at, String d) throws Exception {
        String k = o.get("k").getAsString();
        String cacheKey = u + "/" + k;
        PublicKey key = trusted.get(cacheKey);
        if (key == null) {
            ProfilePublicKey.Data data = new ProfilePublicKey.Data(Instant.ofEpochMilli(o.get("x").getAsLong()),
                    Crypt.byteToPublicKey(Base64.getDecoder().decode(k)), Base64.getDecoder().decode(o.get("ks").getAsString()));
            if (data.hasExpired(Duration.ofDays(7)))
                return false;
            var services = Minecraft.getInstance().services();
            if (!services.canValidateProfileKeys())
                return false;
            ProfilePublicKey.createValidated(services.profileKeySignatureValidator(), u, data);   /* throws if Mojang didn't sign it */
            key = data.key();
            trusted.put(cacheKey, key);
        }
        byte[] sig = Base64.getDecoder().decode(o.get("sg").getAsString());
        return SignatureValidator.from(key, "SHA256withRSA").validate(signed(kind, u, evPub, at, d), sig);
    }

    private static void gotCape(UUID u, JsonObject d, long at) throws Exception {
        RemoteCape old = capes.get(u);
        if (old != null && old.at >= at)
            return;
        String c = d.get("c").getAsString();
        int[] pal = null, img = null;
        int w = 0, h = 0;
        if (c.equals("image")) {
            if (!Config.syncPicture || !d.has("i"))
                c = "official";
            else {
                String b64 = d.get("i").getAsString();
                BufferedImage im = b64.length() > MAX_PICTURE ? null
                        : ImageIO.read(new ByteArrayInputStream(Base64.getDecoder().decode(b64)));
                if (im == null || im.getWidth() > 160 || im.getHeight() > 256)
                    c = "official";
                else {
                    w = im.getWidth();
                    h = im.getHeight();
                    img = im.getRGB(0, 0, w, h, null, 0, w);
                }
            }
        }
        if (!c.equals("official") && !c.equals("none") && !Capes.ALL.containsKey(c))
            c = "official";              /* a cape from a newer version we don't have */
        if (old == null || !old.id.equals(c))
            TuxMc.LOG.info("tuxmc: sync: {} wears {}", u, c);
        capes.put(u, new RemoteCape(c, pal, img, w, h, at));
    }

    private static void gotEmote(UUID u, JsonObject d) {
        long now = System.currentTimeMillis();
        long[] r = rate.computeIfAbsent(u, x -> new long[2]);
        if (now - r[0] > 1000) {
            r[0] = now;
            r[1] = 0;
        }
        if (++r[1] > 4)
            return;
        int e = d.get("e").getAsInt();
        Emotes.Emote[] all = Emotes.Emote.values();
        if (SELF)
            TuxMc.LOG.info("tuxmc: sync: {} emote {}", u, e < 0 || e >= all.length ? "stop" : all[e]);
        if (e < 0 || e >= all.length)
            emotes.remove(u);
        else
            emotes.put(u, new RemoteEmote(all[e], System.nanoTime(), new double[] {Double.NaN, Double.NaN}));
    }

    private static long mtime(String p) {
        try {
            return p.isBlank() ? 0 : Files.getLastModifiedTime(Path.of(p.replaceFirst("^~", System.getProperty("user.home")))).toMillis();
        } catch (Exception e) {
            return 0;
        }
    }

    /* --------------------------------------------------------------- relays */

    private static final class Relay implements WebSocket.Listener {
        final String url;
        volatile WebSocket ws;
        volatile boolean open, connecting;
        long retryAt, backoff = 5000;
        String capeReq;
        private final StringBuilder buf = new StringBuilder();
        private CompletableFuture<?> chain = CompletableFuture.completedFuture(null);

        Relay(String url) { this.url = url; }

        void connect() {
            connecting = true;
            http.newWebSocketBuilder().buildAsync(URI.create(url), this).whenComplete((w, err) -> {
                connecting = false;
                if (err != null) {
                    retryAt = System.currentTimeMillis() + backoff;
                    backoff = Math.min(backoff * 2, 120_000);
                    return;
                }
                Minecraft.getInstance().execute(() -> {
                    if (!relays.contains(this)) {
                        w.abort();
                        return;
                    }
                    ws = w;
                    open = true;
                    backoff = 5000;
                    if (server != null)
                        send(emoteReq());
                    if (capeReq != null)
                        send(capeReq);
                    if (capeMsg != null)           /* only this relay is new: give it the same event */
                        send(capeMsg);
                    if (SELF)
                        TuxMc.LOG.info("tuxmc: sync: {} open, cape request {}", url, capeReq == null ? "none" : "sent");
                });
            });
        }

        /** WebSocket allows one send at a time: queue them */
        synchronized void send(String msg) {
            WebSocket w = ws;
            if (!open || w == null)
                return;
            chain = chain.handle((x, e) -> null).thenCompose(x -> w.sendText(msg, true));
        }

        void close() {
            open = false;
            WebSocket w = ws;
            if (w != null)
                w.abort();
        }

        private void down() {
            open = false;
            ws = null;
            retryAt = System.currentTimeMillis() + backoff;
            backoff = Math.min(backoff * 2, 120_000);
        }

        @Override
        public CompletionStage<?> onText(WebSocket w, CharSequence data, boolean last) {
            buf.append(data);
            if (last) {
                String m = buf.toString();
                buf.setLength(0);
                if (m.length() < 200_000)
                    received(m);
            }
            w.request(1);
            return null;
        }

        @Override
        public CompletionStage<?> onClose(WebSocket w, int code, String reason) {
            down();
            return null;
        }

        @Override
        public void onError(WebSocket w, Throwable err) {
            down();
        }
    }

    private Sync() {}
}
