package dev.tux.mc;

import java.lang.management.GarbageCollectorMXBean;
import java.lang.management.ManagementFactory;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.Arrays;

import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;

/**
 * Frame timing for the adaptive mode, the startup log and the benchmark.
 *
 * Every frame costs one nanoTime() and an array store. The rest only runs
 * when asked for: TUXMC_BENCH=<seconds> turns the camera round at a fixed
 * speed after a warm-up, writes config/tuxmc/bench-<time>.txt and quits.
 */
public final class Perf {
    /* the last 256 frame times in ns, for the adaptive mode and the Session tab */
    private static final long[] ring = new long[256];
    private static int ringPos;
    private static long last;
    /** smoothed frame time in ms; 0 until the first frames */
    public static float frameMs;
    /** set when frames are slow (see Config.adaptive): skip work nobody would miss */
    public static boolean pressure;
    private static int slowFrames, fastFrames;

    /* startup marks, ms since the JVM started */
    private static final long jvmStart = ManagementFactory.getRuntimeMXBean().getStartTime();
    private static long clicked = parse(System.getenv("TUXMC_T0"));
    private static long initAt, titleAt, joinAt, playableAt;

    private static long parse(String s) {
        try {
            return s == null ? 0 : Long.parseLong(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    public static void modInit() { initAt = System.currentTimeMillis(); }

    public static void titleReady() {
        if (titleAt == 0)
            titleAt = System.currentTimeMillis();
    }

    public static void joined() {
        joinAt = System.currentTimeMillis();
        playableAt = 0;
    }

    /** called at the start of every frame (Minecraft.runTick) */
    public static void frame() {
        long now = System.nanoTime();
        if (last != 0) {
            long d = now - last;
            ring[ringPos++ & 255] = d;
            float ms = d / 1e6f;
            frameMs = frameMs == 0 ? ms : frameMs + (ms - frameMs) * 0.05f;
            adapt(ms);
            if (bench != null)
                bench.frame(d);
        }
        last = now;
        if (playableAt == 0 && joinAt != 0) {
            Minecraft mc = Minecraft.getInstance();
            if (mc.player != null && mc.level != null && mc.gui.screen() == null) {
                playableAt = System.currentTimeMillis();
                writeStartup();
            }
        }
        if (bench == null && BENCH_SECONDS > 0)
            bench = new Bench();
    }

    /* pressure: on after ~1 s of frames slower than the target, off after ~3 s of good ones */
    private static void adapt(float ms) {
        if (!Config.adaptive || !Config.perfOn()) {
            pressure = false;
            return;
        }
        float target = 1000f / Math.max(20, Config.adaptiveFps);
        if (ms > target) {
            fastFrames = 0;
            if (++slowFrames > Math.max(10, Config.adaptiveFps))
                pressure = true;
        } else {
            slowFrames = 0;
            if (++fastFrames > 3 * Math.max(10, Config.adaptiveFps))
                pressure = false;
        }
    }

    /** a copy of the recent frame times, oldest first, in ms */
    public static float[] recent() {
        float[] r = new float[ring.length];
        for (int i = 0; i < r.length; i++)
            r[i] = ring[(ringPos + i) & 255] / 1e6f;
        return r;
    }

    private static long lastTested, lastCulled;
    private static final long[] sortBuf = new long[256];

    /** Settings, Performance, Diagnostics: two lines for the info box (built once a tick) */
    static void diagnostics(java.util.List<String> lines, java.util.List<Integer> cols) {
        System.arraycopy(ring, 0, sortBuf, 0, 256);
        Arrays.sort(sortBuf);
        double sum = 0, worst = 0;
        int n = 0;
        for (long v : sortBuf)
            if (v > 0) {
                sum += v;
                n++;
            }
        for (int i = 253; i < 256; i++)       /* the slowest 1% of the last 256 frames */
            worst += sortBuf[i];
        long t = EntityCull.tested - lastTested, c = EntityCull.culled - lastCulled;
        lastTested = EntityCull.tested;
        lastCulled = EntityCull.culled;
        lines.add(String.format("frame %.2f ms  1%% low %.1f ms%s", n == 0 ? 0 : sum / n / 1e6, worst / 3 / 1e6,
                pressure ? "  adaptive" : ""));
        cols.add(pressure ? Theme.warn : Theme.subtext);
        Runtime rt = Runtime.getRuntime();
        lines.add(String.format("early cull %d/%d  heap %d MB", c, t, (rt.totalMemory() - rt.freeMemory()) >> 20));
        cols.add(Theme.subtext);
    }

    private static void writeStartup() {
        StringBuilder sb = new StringBuilder();
        sb.append(java.time.LocalDateTime.now().withNano(0)).append("  ");
        if (clicked > 0)
            sb.append("click→jvm ").append(jvmStart - clicked).append(" ms  ");
        sb.append("jvm→mods ").append(initAt - jvmStart).append(" ms  ");
        if (titleAt > 0)
            sb.append("jvm→title ").append(titleAt - jvmStart).append(" ms  ");
        sb.append("join→playable ").append(playableAt - joinAt).append(" ms  ");
        sb.append("jvm→playable ").append(playableAt - jvmStart).append(" ms");
        if (clicked > 0)
            sb.append("  click→playable ").append(playableAt - clicked).append(" ms");
        String line = sb.toString();
        TuxMc.LOG.info("tuxmc: startup {}", line);
        clicked = 0;               /* only the first world counts from the click */
        try {
            Path f = dir().resolve("startup.log");
            Files.writeString(f, line + "\n", StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (Exception e) {
            TuxMc.LOG.warn("tuxmc: startup log: {}", e.toString());
        }
    }

    private static Path dir() throws java.io.IOException {
        Path d = FabricLoader.getInstance().getConfigDir().resolve("tuxmc");
        Files.createDirectories(d);
        return d;
    }

    /* ---------------- benchmark ---------------- */

    private static final int BENCH_SECONDS = (int) parse(System.getenv("TUXMC_BENCH"));
    /* warm-up: at least BENCH_WARM s, then until every section is built (at most BENCH_WARM_MAX s) */
    private static final int BENCH_WARM = System.getenv("TUXMC_BENCH_WARM") != null
            ? (int) parse(System.getenv("TUXMC_BENCH_WARM")) : 25;
    private static final int BENCH_WARM_MAX = System.getenv("TUXMC_BENCH_WARM_MAX") != null
            ? (int) parse(System.getenv("TUXMC_BENCH_WARM_MAX")) : 240;
    /* optional render distance for the run; the player's own is put back before the game quits */
    private static final int BENCH_RD = (int) parse(System.getenv("TUXMC_BENCH_RD"));
    private static Bench bench;

    private static final class Bench {
        final long[] t = new long[400_000];
        int n;
        long startWall, startCpu, startRenderCpu, gcCount0, gcMs0;
        float yaw0;
        long inWorld, loadedAt;
        int savedRd = -1;
        boolean running, done;
        final GpuSampler gpu = new GpuSampler();

        void frame(long d) {
            if (done)
                return;
            Minecraft mc = Minecraft.getInstance();
            if (mc.player == null || mc.gui.screen() != null) {
                inWorld = 0;
                return;
            }
            long now = System.nanoTime();
            /* no input during a bench: keep the AFK limiter (30 fps after a minute) out of it */
            mc.getFramerateLimitTracker().onInputReceived();
            if (inWorld == 0) {
                inWorld = now;
                if (BENCH_RD > 0 && savedRd < 0) {
                    savedRd = mc.options.renderDistance().get();
                    mc.options.renderDistance().set(BENCH_RD);
                }
                TuxMc.LOG.info("tuxmc: bench: in the world, {} s warm-up", BENCH_WARM);
            }
            if (!running) {
                mc.player.setXRot(0);
                long warm = now - inWorld;
                if (loadedAt == 0 && mc.levelRenderer.hasRenderedAllSections())
                    loadedAt = now;
                else if (loadedAt != 0 && !mc.levelRenderer.hasRenderedAllSections())
                    loadedAt = 0;                     /* more work turned up: not settled yet */
                boolean settled = loadedAt != 0 && now - loadedAt > 3_000_000_000L;
                if (warm < BENCH_WARM * 1_000_000_000L
                        || !settled && warm < BENCH_WARM_MAX * 1_000_000_000L)
                    return;
                warmS = warm / 1e9;
                allBuilt = settled;
                running = true;
                startWall = now;
                startCpu = processCpu();
                startRenderCpu = ManagementFactory.getThreadMXBean().getCurrentThreadCpuTime();
                long[] gc = gc();
                gcCount0 = gc[0];
                gcMs0 = gc[1];
                yaw0 = mc.player.getYRot();
                gpu.start();
                TuxMc.LOG.info("tuxmc: bench: measuring {} s", BENCH_SECONDS);
                return;
            }
            if (n < t.length)
                t[n++] = d;
            double secs = (now - startWall) / 1e9;
            /* one full turn over the run, level gaze: the same view sequence every time */
            float yaw = yaw0 + (float) (360.0 * secs / BENCH_SECONDS);
            mc.player.setYRot(yaw);
            mc.player.yRotO = yaw;
            mc.player.setXRot(0);
            mc.player.xRotO = 0;
            if (secs >= BENCH_SECONDS) {
                done = true;
                finish(mc, now);
            }
        }

        double warmS;
        boolean allBuilt;

        void finish(Minecraft mc, long now) {
            double wall = (now - startWall) / 1e9;
            double cpu = (processCpu() - startCpu) / 1e9;
            double rcpu = (ManagementFactory.getThreadMXBean().getCurrentThreadCpuTime() - startRenderCpu) / 1e9;
            long[] gc = gc();
            double gpuBusy = gpu.stop();
            long[] s = Arrays.copyOf(t, n);
            Arrays.sort(s);
            double sum = 0;
            for (long x : s)
                sum += x;
            double avgMs = sum / n / 1e6;
            Runtime rt = Runtime.getRuntime();
            StringBuilder sb = new StringBuilder();
            sb.append("frames ").append(n).append('\n');
            sb.append(String.format("warmup_s %.1f%s%n", warmS, allBuilt ? "" : " (sections still building)"));
            sb.append(String.format("avg_fps %.1f%n", 1000 / avgMs));
            sb.append(String.format("low1_fps %.1f%n", 1000 / avgSlowest(s, 0.01)));
            sb.append(String.format("low01_fps %.1f%n", 1000 / avgSlowest(s, 0.001)));
            sb.append(String.format("median_ms %.3f%n", s[n / 2] / 1e6));
            sb.append(String.format("p99_ms %.3f%n", s[(int) (n * 0.99)] / 1e6));
            sb.append(String.format("max_ms %.3f%n", s[n - 1] / 1e6));
            sb.append(String.format("render_thread_cpu %.0f%%%n", 100 * rcpu / wall));
            sb.append(String.format("process_cpu_cores %.2f%n", cpu / wall));
            sb.append(String.format("gpu_busy %.0f%%%n", gpuBusy));
            sb.append("gc_count ").append(gc[0] - gcCount0).append('\n');
            sb.append("gc_ms ").append(gc[1] - gcMs0).append('\n');
            sb.append(String.format("heap_used_mb %d%n", (rt.totalMemory() - rt.freeMemory()) >> 20));
            sb.append(String.format("heap_committed_mb %d%n", rt.totalMemory() >> 20));
            sb.append("render_distance ").append(mc.options.renderDistance().get()).append('\n');
            sb.append("fps_limit ").append(mc.options.framerateLimit().get()).append('\n');
            sb.append("vsync ").append(mc.options.enableVsync().get()).append('\n');
            sb.append("window ").append(mc.getWindow().getWidth()).append('x').append(mc.getWindow().getHeight()).append('\n');
            sb.append("tuxmc_perf ").append(Config.perfOn()).append('\n');
            sb.append("full_detail ").append(FarView.cap()).append('\n');
            sb.append("threads_niced ").append(ThreadTuner.counts).append('\n');
            sb.append("entity_cull_tested ").append(EntityCull.tested).append(" culled ").append(EntityCull.culled).append('\n');
            if (mc.level != null) {
                java.util.Map<String, Integer> types = new java.util.HashMap<>();
                int all = 0;
                for (var e : mc.level.entitiesForRendering()) {
                    types.merge(e.getType().toShortString(), 1, Integer::sum);
                    all++;
                }
                sb.append("entities ").append(all).append('\n');
                types.entrySet().stream().sorted((a, b) -> b.getValue() - a.getValue()).limit(8)
                        .forEach(t -> sb.append("  ").append(t.getKey()).append(' ').append(t.getValue()).append('\n'));
            }
            String out = sb.toString();
            TuxMc.LOG.info("tuxmc: bench:\n{}", out);
            try {
                String name = System.getenv("TUXMC_BENCH_OUT");
                Path f = name != null ? Path.of(name)
                        : dir().resolve("bench-" + System.currentTimeMillis() / 1000 + ".txt");
                Files.writeString(f, out);
                /* raw frame times, one per line in µs, for plots */
                StringBuilder raw = new StringBuilder(n * 6);
                for (int i = 0; i < n; i++)
                    raw.append(t[i] / 1000).append('\n');
                Files.writeString(Path.of(f + ".frames"), raw);
            } catch (Exception e) {
                TuxMc.LOG.warn("tuxmc: bench write: {}", e.toString());
            }
            if (savedRd > 0)
                mc.options.renderDistance().set(savedRd);
            if (System.getenv("TUXMC_BENCH_STAY") == null)
                mc.stop();
        }

        /* mean frame time of the slowest share of frames (the usual "1% low"), in ms */
        static double avgSlowest(long[] sorted, double share) {
            int k = Math.max(1, (int) (sorted.length * share));
            double sum = 0;
            for (int i = sorted.length - k; i < sorted.length; i++)
                sum += sorted[i];
            return sum / k / 1e6;
        }
    }

    private static long processCpu() {
        var os = ManagementFactory.getOperatingSystemMXBean();
        return os instanceof com.sun.management.OperatingSystemMXBean o ? o.getProcessCpuTime() : 0;
    }

    private static long[] gc() {
        long c = 0, ms = 0;
        for (GarbageCollectorMXBean b : ManagementFactory.getGarbageCollectorMXBeans()) {
            c += Math.max(0, b.getCollectionCount());
            ms += Math.max(0, b.getCollectionTime());
        }
        return new long[] {c, ms};
    }

    /* AMD/Intel-xe expose gpu_busy_percent; sampled 10x a second on its own thread while benching */
    private static final class GpuSampler {
        private volatile boolean on;
        private long sum, count;
        private Thread th;

        void start() {
            Path f = find();
            if (f == null)
                return;
            on = true;
            th = new Thread(() -> {
                while (on) {
                    try {
                        sum += Long.parseLong(Files.readString(f).trim());
                        count++;
                        Thread.sleep(100);
                    } catch (Exception e) {
                        return;
                    }
                }
            }, "tuxmc-gpu-sampler");
            th.setDaemon(true);
            th.start();
        }

        double stop() {
            on = false;
            try {
                if (th != null)
                    th.join(500);
            } catch (InterruptedException ignored) {
            }
            return count == 0 ? -1 : (double) sum / count;
        }

        private static Path find() {
            try (var s = Files.list(Path.of("/sys/class/drm"))) {
                return s.map(p -> p.resolve("device/gpu_busy_percent")).filter(Files::isReadable).findFirst().orElse(null);
            } catch (Exception e) {
                return null;
            }
        }
    }

    private Perf() {}
}
