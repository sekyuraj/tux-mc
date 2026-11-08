package dev.tux.mc;

import java.io.ByteArrayOutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.zip.Inflater;

import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.VertexConsumer;

import net.fabricmc.loader.api.FabricLoader;

/**
 * Head models made on the launcher's Hats page. The file is the launcher's
 * (launcher/src/mc-hat.c): zlib of "TUXHAT1\0", N, name, palette, N^3 cells.
 * The grid covers 16x16x16 skin pixels: x and z -8..8 around the head's
 * middle, y from 2 pixels under the head to 6 above it, the face at high z.
 *
 * Yours is config/tuxmc/hat.tuxhat while Config.hat names one; other players'
 * come in through Sync.
 */
public final class Hats {
    public static final int SHARE_MAX = 45_000;     /* bytes, the launcher warns above this */

    /** set on the render state by AvatarRendererMixin, read by HatLayer */
    public interface Holder {
        Hat tuxmc$hat();
        void tuxmc$setHat(Hat h);
    }

    /** exposed faces, ready to draw: 4 corners each, in head space (blocks, y down) */
    public record Hat(float[] pos, int[] colour, byte[] dir) {
        private static final float[][] NORMAL = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};

        public void emit(PoseStack.Pose pose, VertexConsumer vc, int light, int overlay) {
            for (int f = 0; f < colour.length; f++) {
                float[] n = NORMAL[dir[f]];
                for (int k = 0; k < 4; k++) {
                    int i = (f * 4 + k) * 3;
                    vc.addVertex(pose, pos[i], pos[i + 1], pos[i + 2]).setColor(colour[f]).setUv(0.5f, 0.5f)
                            .setOverlay(overlay).setLight(light).setNormal(pose, n[0], n[1], n[2]);
                }
            }
        }
    }

    private static final Path FILE = FabricLoader.getInstance().getConfigDir().resolve("tuxmc").resolve("hat.tuxhat");
    private static Hat own;
    private static String ownKey = "";
    private static long checked;
    private static final Map<UUID, Hat> remote = new ConcurrentHashMap<>();
    private static final Map<UUID, Long> remoteAt = new ConcurrentHashMap<>();

    /** yours, re-read when the launcher changes it */
    public static Hat own() {
        long t = System.currentTimeMillis();
        if (t - checked > 2000) {
            checked = t;
            String key = Config.hat.isBlank() ? "" : Config.hat + "@" + stamp();
            if (!key.equals(ownKey)) {
                ownKey = key;
                byte[] b = ownBytes();
                own = b == null ? null : decode(b);
            }
        }
        return own;
    }

    /** the file as it's shared, or null when you wear none */
    public static byte[] ownBytes() {
        if (Config.hat.isBlank())
            return null;
        try {
            byte[] b = Files.readAllBytes(FILE);
            return b.length > 0 && b.length <= 200_000 ? b : null;
        } catch (Exception e) {
            return null;
        }
    }

    public static long stamp() {
        try {
            return Files.getLastModifiedTime(FILE).toMillis();
        } catch (Exception e) {
            return 0;
        }
    }

    public static Hat remote(UUID u) {
        return Config.sync && Config.syncHats ? remote.get(u) : null;
    }

    /** from Sync; bytes null = they took it off */
    static void putRemote(UUID u, byte[] bytes, long at) {
        Long old = remoteAt.get(u);
        if (old != null && old >= at)
            return;
        remoteAt.put(u, at);
        Hat h = bytes == null || bytes.length > SHARE_MAX ? null : decode(bytes);
        if (h == null)
            remote.remove(u);
        else
            remote.put(u, h);
    }

    static int remoteCount() { return remote.size(); }

    public static Hat decode(byte[] z) {
        try {
            Inflater inf = new Inflater();
            inf.setInput(z);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int max = 8 + 2 + 64 + 1 + 255 * 3 + 32 * 32 * 32;
            while (!inf.finished() && out.size() <= max) {
                int k = inf.inflate(buf);
                if (k == 0 && (inf.needsInput() || inf.needsDictionary()))
                    break;
                out.write(buf, 0, k);
            }
            inf.end();
            byte[] r = out.toByteArray();
            if (r.length < 11 || !new String(r, 0, 7, java.nio.charset.StandardCharsets.US_ASCII).equals("TUXHAT1"))
                return null;
            int n = r[8] & 255, nl = r[9] & 255;
            if ((n != 16 && n != 32) || nl > 63 || r.length < 11 + nl)
                return null;
            int pc = r[10 + nl] & 255, off = 11 + nl + pc * 3;
            if (pc < 1 || r.length != off + n * n * n)
                return null;
            int[] pal = new int[pc + 1];
            for (int k = 0; k < pc; k++) {
                int p = 11 + nl + k * 3;
                pal[k + 1] = 0xFF000000 | (r[p] & 255) << 16 | (r[p + 1] & 255) << 8 | (r[p + 2] & 255);
            }
            int[] cell = new int[n * n * n];
            for (int i = 0; i < cell.length; i++) {
                int k = r[off + i] & 255;
                cell[i] = k <= pc ? pal[k] : 0;
            }
            return mesh(n, cell);
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: hat: {}", e.toString());
            return null;
        }
    }

    private static final int[] DX = {0, 0, -1, 1, 0, 0}, DY = {0, 0, 0, 0, 1, -1}, DZ = {1, -1, 0, 0, 0, 0};

    /** corners per direction, in the launcher's space (y up, face at +z), TL TR BR BL */
    private static Hat mesh(int n, int[] cell) {
        var pos = new java.util.ArrayList<float[]>();
        var col = new java.util.ArrayList<Integer>();
        var dir = new java.util.ArrayList<Byte>();
        float s = 16f / n;
        for (int y = 0; y < n; y++)
            for (int z = 0; z < n; z++)
                for (int x = 0; x < n; x++) {
                    int c = cell[(y * n + z) * n + x];
                    if (c == 0)
                        continue;
                    for (int d = 0; d < 6; d++) {
                        int X = x + DX[d], Y = y + DY[d], Z = z + DZ[d];
                        if (X >= 0 && Y >= 0 && Z >= 0 && X < n && Y < n && Z < n && cell[(Y * n + Z) * n + X] != 0)
                            continue;
                        float x0 = -8 + x * s, x1 = x0 + s, y0 = -2 + y * s, y1 = y0 + s, z0 = -8 + z * s, z1 = z0 + s;
                        float[][] q = switch (d) {
                            case 0 -> new float[][] {{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}};
                            case 1 -> new float[][] {{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}};
                            case 2 -> new float[][] {{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}};
                            case 3 -> new float[][] {{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}};
                            case 4 -> new float[][] {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
                            default -> new float[][] {{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}};
                        };
                        float[] f = new float[12];
                        for (int k = 0; k < 4; k++) {
                            /* to the game's head space: y down, the face toward -z, 16 pixels a block */
                            f[k * 3] = q[k][0] / 16f;
                            f[k * 3 + 1] = -q[k][1] / 16f;
                            f[k * 3 + 2] = -q[k][2] / 16f;
                        }
                        pos.add(f);
                        col.add(c);
                        dir.add((byte) d);
                    }
                }
        float[] p = new float[pos.size() * 12];
        int[] c = new int[col.size()];
        byte[] dd = new byte[dir.size()];
        for (int i = 0; i < c.length; i++) {
            System.arraycopy(pos.get(i), 0, p, i * 12, 12);
            c[i] = col.get(i);
            dd[i] = dir.get(i);
        }
        return new Hat(p, c, dd);
    }

    private Hats() {}
}
