package dev.tux.mc;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;
import java.net.InetAddress;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import net.fabricmc.loader.api.FabricLoader;

/**
 * libtuxmc (native/ in the source tree): cape painting and the socket list,
 * called through Java's FFM API. The jar carries a Linux .so and a Windows .dll;
 * the right one is copied to config/tuxmc/ on first use.
 */
public final class Native {
    public static final boolean WINDOWS = System.getProperty("os.name", "").toLowerCase().startsWith("windows");
    public static final boolean LINUX = System.getProperty("os.name", "").toLowerCase().startsWith("linux");
    public static final boolean OK;
    private static MethodHandle cape, net;

    static {
        boolean ok = false;
        try {
            Path dir = FabricLoader.getInstance().getConfigDir().resolve("tuxmc");
            Files.createDirectories(dir);
            String file = WINDOWS ? "tuxmc.dll" : "libtuxmc.so";
            String res = WINDOWS ? "/natives/windows-x86_64/tuxmc.dll" : "/natives/linux-x86_64/libtuxmc.so";
            Path lib = dir.resolve(file);
            byte[] want;
            try (var in = Native.class.getResourceAsStream(res)) {
                if (in == null)
                    throw new IllegalStateException("no " + res + " in the jar");
                want = in.readAllBytes();
            }
            if (!Files.exists(lib) || !Arrays.equals(Files.readAllBytes(lib), want)) {
                Path tmp = Files.createTempFile(dir, "tuxmc", WINDOWS ? ".dll" : ".so");
                Files.write(tmp, want);
                try {
                    Files.move(tmp, lib, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
                } catch (Exception e) {
                    /* Windows: a second game still has the old one loaded; use the fresh copy */
                    lib = tmp;
                }
            }
            SymbolLookup l = SymbolLookup.libraryLookup(lib, Arena.global());
            Linker ln = Linker.nativeLinker();
            cape = ln.downcallHandle(l.find("tuxmc_cape").orElseThrow(),
                    FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT, ADDRESS, ADDRESS, ADDRESS));
            net = ln.downcallHandle(l.find("tuxmc_net").orElseThrow(),
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT));
            ok = true;
        } catch (Throwable t) {
            TuxMc.LOG.warn("tuxmc: native library unavailable, client capes and the net map are off: {}", t.toString());
        }
        OK = ok;
    }

    /**
     * A cape, ARGB, (64*s) x (32*s). pal: accent, selection, bg, fgBright, warn.
     * img/iw/ih: the logo or picture the cape uses, or null.
     */
    public static int[] cape(String id, int s, int[] pal, int[] img, int iw, int ih) {
        if (!OK)
            return null;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment im = MemorySegment.NULL;
            if (img != null) {
                im = a.allocate(16, 8);
                im.set(ADDRESS, 0, a.allocateFrom(JAVA_INT, img));
                im.set(JAVA_INT, 8, iw);
                im.set(JAVA_INT, 12, ih);
            }
            MemorySegment out = a.allocate(JAVA_INT, 64L * s * 32 * s);
            cape.invokeExact(a.allocateFrom(id), s, a.allocateFrom(JAVA_INT, pal), im, out);
            return out.toArray(JAVA_INT);
        } catch (Throwable t) {
            TuxMc.LOG.warn("tuxmc: cape {}: {}", id, t.toString());
            return null;
        }
    }

    public record Conn(String proto, String ip, int port, long sent, long recv) {}

    /** this game's connected sockets, or null when the library isn't there */
    public static List<Conn> sockets() {
        if (!OK)
            return null;
        try (Arena a = Arena.ofConfined()) {
            int max = 256;
            MemorySegment buf = a.allocate(40L * max, 8);
            int n = (int) net.invokeExact(buf, max);
            List<Conn> l = new ArrayList<>();
            for (int i = 0; i < n; i++) {
                MemorySegment c = buf.asSlice(40L * i, 40);
                int fam = c.get(JAVA_BYTE, 1);
                byte[] addr = c.asSlice(8, fam == 4 ? 4 : 16).toArray(JAVA_BYTE);
                String ip = InetAddress.getByAddress(addr).getHostAddress();
                if (ip.startsWith("::ffff:"))
                    ip = ip.substring(7);
                l.add(new Conn(c.get(JAVA_BYTE, 0) == 6 ? "tcp" : "udp", ip, c.get(JAVA_SHORT, 2) & 0xFFFF,
                        c.get(JAVA_LONG, 24), c.get(JAVA_LONG, 32)));
            }
            return l;
        } catch (Throwable t) {
            TuxMc.LOG.debug("tuxmc: sockets: {}", t.toString());
            return null;
        }
    }

    private Native() {}
}
