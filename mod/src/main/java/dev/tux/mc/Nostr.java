package dev.tux.mc;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.HexFormat;
import java.util.List;

/**
 * The bits of Nostr the sync needs: BIP340 Schnorr signatures on secp256k1
 * (Java has no secp256k1 any more) and NIP-01 events. Plain BigInteger maths:
 * a signature takes a few milliseconds, and we sign at most one per emote.
 * The key is a throwaway one per install (config/tuxmc/nostr.key); who you are
 * is proven separately with your Mojang key, see Sync.
 */
public final class Nostr {
    private static final BigInteger P = new BigInteger("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F", 16);
    private static final BigInteger N = new BigInteger("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", 16);
    private static final BigInteger[] G = {
            new BigInteger("79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798", 16),
            new BigInteger("483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8", 16)};
    private static final BigInteger THREE = BigInteger.valueOf(3), SEVEN = BigInteger.valueOf(7);
    private static final SecureRandom RNG = new SecureRandom();
    private static final HexFormat HEX = HexFormat.of();

    /* ---- curve: affine points, null = infinity ---- */
    private static BigInteger[] add(BigInteger[] a, BigInteger[] b) {
        if (a == null)
            return b;
        if (b == null)
            return a;
        BigInteger l;
        if (a[0].equals(b[0])) {
            if (!a[1].equals(b[1]) || a[1].signum() == 0)
                return null;
            l = THREE.multiply(a[0]).multiply(a[0]).multiply(a[1].shiftLeft(1).modInverse(P)).mod(P);
        } else {
            l = b[1].subtract(a[1]).multiply(b[0].subtract(a[0]).modInverse(P)).mod(P);
        }
        BigInteger x = l.multiply(l).subtract(a[0]).subtract(b[0]).mod(P);
        return new BigInteger[] {x, l.multiply(a[0].subtract(x)).subtract(a[1]).mod(P)};
    }

    private static BigInteger[] mul(BigInteger[] pt, BigInteger k) {
        BigInteger[] r = null;
        for (int i = k.bitLength() - 1; i >= 0; i--) {
            r = add(r, r);
            if (k.testBit(i))
                r = add(r, pt);
        }
        return r;
    }

    /** the point with x and an even y, or null (BIP340 lift_x) */
    private static BigInteger[] liftX(BigInteger x) {
        if (x.compareTo(P) >= 0)
            return null;
        BigInteger c = x.modPow(THREE, P).add(SEVEN).mod(P);
        BigInteger y = c.modPow(P.add(BigInteger.ONE).shiftRight(2), P);
        if (!y.modPow(BigInteger.TWO, P).equals(c))
            return null;
        return new BigInteger[] {x, y.testBit(0) ? P.subtract(y) : y};
    }

    private static byte[] b32(BigInteger v) {
        byte[] b = v.toByteArray(), o = new byte[32];
        int n = Math.min(32, b.length);
        System.arraycopy(b, b.length - n, o, 32 - n, n);
        return o;
    }

    private static BigInteger num(byte[] b) { return new BigInteger(1, b); }

    static byte[] sha256(byte[]... parts) {
        try {
            MessageDigest d = MessageDigest.getInstance("SHA-256");
            for (byte[] p : parts)
                d.update(p);
            return d.digest();
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    private static byte[] tagged(String tag, byte[]... parts) {
        byte[] t = sha256(tag.getBytes(StandardCharsets.UTF_8));
        byte[][] all = new byte[parts.length + 2][];
        all[0] = t;
        all[1] = t;
        System.arraycopy(parts, 0, all, 2, parts.length);
        return sha256(all);
    }

    /* ---- BIP340 ---- */
    public static byte[] pubkey(byte[] secret) { return b32(mul(G, num(secret))[0]); }

    public static byte[] sign(byte[] secret, byte[] msg, byte[] aux) {
        BigInteger d = num(secret);
        if (d.signum() == 0 || d.compareTo(N) >= 0)
            throw new IllegalArgumentException("bad secret key");
        BigInteger[] pp = mul(G, d);
        if (pp[1].testBit(0))
            d = N.subtract(d);
        byte[] t = b32(d.xor(num(tagged("BIP0340/aux", aux))));
        byte[] px = b32(pp[0]);
        BigInteger k = num(tagged("BIP0340/nonce", t, px, msg)).mod(N);
        if (k.signum() == 0)
            throw new IllegalStateException("bad nonce");
        BigInteger[] r = mul(G, k);
        if (r[1].testBit(0))
            k = N.subtract(k);
        byte[] rx = b32(r[0]);
        BigInteger e = num(tagged("BIP0340/challenge", rx, px, msg)).mod(N);
        byte[] sig = new byte[64];
        System.arraycopy(rx, 0, sig, 0, 32);
        System.arraycopy(b32(k.add(e.multiply(d)).mod(N)), 0, sig, 32, 32);
        return sig;
    }

    public static boolean verify(byte[] pub, byte[] msg, byte[] sig) {
        if (pub.length != 32 || sig.length != 64)
            return false;
        BigInteger[] pp = liftX(num(pub));
        BigInteger r = num(java.util.Arrays.copyOf(sig, 32)), s = num(java.util.Arrays.copyOfRange(sig, 32, 64));
        if (pp == null || r.compareTo(P) >= 0 || s.compareTo(N) >= 0)
            return false;
        BigInteger e = num(tagged("BIP0340/challenge", b32(r), pub, msg)).mod(N);
        BigInteger[] rr = add(mul(G, s), mul(pp, N.subtract(e)));
        return rr != null && !rr[1].testBit(0) && rr[0].equals(r);
    }

    public static byte[] newSecret() {
        while (true) {
            byte[] k = new byte[32];
            RNG.nextBytes(k);
            BigInteger v = num(k);
            if (v.signum() > 0 && v.compareTo(N) < 0)
                return k;
        }
    }

    /* ---- NIP-01 events ---- */
    /** NIP-01 string escaping: only these get escaped, everything else stays as UTF-8 */
    static String str(String s) {
        StringBuilder b = new StringBuilder(s.length() + 2).append('"');
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
            case '"' -> b.append("\\\"");
            case '\\' -> b.append("\\\\");
            case '\n' -> b.append("\\n");
            case '\r' -> b.append("\\r");
            case '\t' -> b.append("\\t");
            case '\b' -> b.append("\\b");
            case '\f' -> b.append("\\f");
            default -> {
                if (c < 0x20)
                    b.append(String.format("\\u%04x", (int) c));
                else
                    b.append(c);
            }
            }
        }
        return b.append('"').toString();
    }

    private static String tags(List<String[]> tags) {
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < tags.size(); i++) {
            if (i > 0)
                b.append(',');
            b.append('[');
            String[] t = tags.get(i);
            for (int j = 0; j < t.length; j++)
                b.append(j > 0 ? "," : "").append(str(t[j]));
            b.append(']');
        }
        return b.append(']').toString();
    }

    /** a signed event, as the JSON object relays want */
    public static String event(byte[] secret, long createdAt, int kind, List<String[]> tags, String content) {
        String pub = HEX.formatHex(pubkey(secret));
        String tg = tags(tags);
        String ser = "[0," + str(pub) + "," + createdAt + "," + kind + "," + tg + "," + str(content) + "]";
        byte[] id = sha256(ser.getBytes(StandardCharsets.UTF_8));
        byte[] aux = new byte[32];
        RNG.nextBytes(aux);
        String sig = HEX.formatHex(sign(secret, id, aux));
        return "{\"id\":\"" + HEX.formatHex(id) + "\",\"pubkey\":\"" + pub + "\",\"created_at\":" + createdAt
                + ",\"kind\":" + kind + ",\"tags\":" + tg + ",\"content\":" + str(content) + ",\"sig\":\"" + sig + "\"}";
    }

    /** checks id and signature of a received event (fields as the relay sent them) */
    public static boolean check(String id, String pub, long createdAt, int kind, List<String[]> tags, String content,
            String sig) {
        try {
            String ser = "[0," + str(pub) + "," + createdAt + "," + kind + "," + tags(tags) + "," + str(content) + "]";
            byte[] want = sha256(ser.getBytes(StandardCharsets.UTF_8));
            return java.util.Arrays.equals(want, HEX.parseHex(id)) && verify(HEX.parseHex(pub), want, HEX.parseHex(sig));
        } catch (Exception e) {
            return false;
        }
    }

    public static String hex(byte[] b) { return HEX.formatHex(b); }

    public static byte[] unhex(String s) { return HEX.parseHex(s); }

    private Nostr() {}
}
