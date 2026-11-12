package dev.tux.mc;

import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.invoke.MethodHandle;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Linux: background thread pools give way to the frame.
 *
 * Java thread priorities do nothing on Linux (Distant Horizons' "threadPriority"
 * included), so its LOD builders, the world generation workers and the chunk
 * mesh builders all compete with the render thread as equals. This lowers
 * their scheduling priority with setpriority() per thread (a positive nice:
 * no root needed, nothing outside the game is touched). When the CPU has room
 * they run as fast as before; when it's full, the render thread goes first.
 * Measured in a render distance 102 world: 1% lows 68 -> 88 fps, terrain
 * loading about the same.
 *
 * Matched by the kernel's thread name (/proc/self/task/<tid>/comm, 15 chars).
 * The render, server, GC, JIT, network and sound threads are never touched.
 * An unprivileged thread can't take a nice back, so switching this off counts
 * from the next start. (Pinning the frame and the pools to separate L3 caches
 * was tried too: no steady gain, so it's not here.)
 */
public final class ThreadTuner {
    /* comm prefix -> nice; first match wins */
    private static final String[][] RULES = {
        {"DH-", "10"},                 /* Distant Horizons: LOD generation, building, saving */
        {"Worker-Main-", "5"},         /* vanilla worker pool: world generation, reloads */
        {"IO-Worker-", "5"},           /* region file reads/writes */
        {"Chunk Render T", "2"},       /* Sodium chunk meshing: what you see, so only a little */
    };

    private static MethodHandle setpriority;
    private static final Set<Integer> done = new HashSet<>();
    public static final Map<String, Integer> counts = new HashMap<>();

    public static void start() {
        if (!Native.LINUX || "0".equals(System.getenv("TUXMC_THREADS")) || !Config.perfOn() || !Config.perfThreads)
            return;
        try {
            Linker l = Linker.nativeLinker();
            setpriority = l.downcallHandle(l.defaultLookup().find("setpriority").orElseThrow(),
                    FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT));
        } catch (Throwable t) {
            TuxMc.LOG.warn("tuxmc: thread tuning off: {}", t.toString());
            return;
        }
        Thread t = new Thread(ThreadTuner::loop, "tuxmc-sched");
        t.setDaemon(true);
        t.start();
    }

    private static void loop() {
        boolean logged = false;
        while (true) {
            try {
                scan();
                if (!logged && !counts.isEmpty()) {
                    logged = true;
                    TuxMc.LOG.info("tuxmc: background threads now yield to the frame: {}", counts);
                }
                Thread.sleep(5000);
            } catch (InterruptedException e) {
                return;
            } catch (Throwable e) {
                TuxMc.LOG.warn("tuxmc: thread tuning stopped: {}", e.toString());
                return;
            }
        }
    }

    /* new threads get their nice; threads that ended are forgotten (tids get reused) */
    private static void scan() throws Throwable {
        Set<Integer> alive = new HashSet<>();
        try (var s = Files.list(Path.of("/proc/self/task"))) {
            for (Path p : (Iterable<Path>) s::iterator) {
                int tid;
                try {
                    tid = Integer.parseInt(p.getFileName().toString());
                } catch (NumberFormatException e) {
                    continue;
                }
                alive.add(tid);
                if (done.contains(tid))
                    continue;
                String comm;
                try {
                    comm = Files.readString(p.resolve("comm")).trim();
                } catch (Exception e) {
                    continue;
                }
                for (String[] r : RULES)
                    if (comm.startsWith(r[0])) {
                        /* PRIO_PROCESS (0) with a thread id = that one thread on Linux */
                        if ((int) setpriority.invokeExact(0, tid, Integer.parseInt(r[1])) == 0)
                            counts.merge(r[0], 1, Integer::sum);
                        break;
                    }
                done.add(tid);         /* threads that match nothing are looked at once */
            }
        }
        done.retainAll(alive);
    }

    private ThreadTuner() {}
}
