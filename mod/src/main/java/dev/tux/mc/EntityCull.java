package dev.tux.mc;

import java.lang.reflect.Method;

import dev.tux.mc.mixin.FrustumAccessor;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.client.renderer.entity.EntityRenderDispatcher;
import net.minecraft.client.renderer.entity.EntityRenderer;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Leashable;
import net.minecraft.world.phys.AABB;

/**
 * The first entity test of every frame, for every entity in the level.
 *
 * Vanilla (and Sodium in front of it) build a fresh interpolated, inflated
 * box per entity per frame, look up its section and test the frustum. With a
 * few thousand entities that is most of the render thread. Here the same box
 * is tested against the same frustum without allocating anything, and only a
 * "certainly outside" answer is acted on: everything else goes on to the
 * normal checks unchanged.
 *
 * It only answers for cases where vanilla's result is known to be "hidden"
 * when the box is outside the frustum: a plain Frustum (not a shader pack's
 * shadow frustum), a renderer that keeps EntityRenderer's own culling, an
 * entity that keeps Entity's own interpolated box, and no leash (a leash is
 * drawn when its holder is on screen).
 */
public final class EntityCull {
    /** Config.perfOn() && Config.perfEntities, refreshed with the config */
    public static boolean on = true;
    public static long tested, culled;     /* counters for the diagnostics HUD */

    /* how far past the hitbox a renderer's culling box can reach: 0.5 (vanilla's inflate), 1.0 for
     * LivingEntityRenderer (it adds 0.5 for a dragon head), or -1 = its own culling, leave it alone */
    private static final ClassValue<Double> RENDERER_MARGIN = new ClassValue<>() {
        @Override protected Double computeValue(Class<?> c) {
            if (!declaredIn(c, EntityRenderer.class, "affectedByCulling", Entity.class)
                    || !declaredIn(c, EntityRenderer.class, "shouldRender", Entity.class, Frustum.class,
                            double.class, double.class, double.class, float.class))
                return -1.0;
            if (declaredIn(c, EntityRenderer.class, "getBoundingBoxForCulling", Entity.class, float.class))
                return 0.5;
            if (LivingEntityRenderer.class.isAssignableFrom(c)
                    && declaredIn(c, LivingEntityRenderer.class, "getBoundingBoxForCulling", Entity.class, float.class))
                return 1.0;
            return -1.0;
        }
    };
    private static final ClassValue<Boolean> PLAIN_ENTITY = new ClassValue<>() {
        @Override protected Boolean computeValue(Class<?> c) {
            return declaredIn(c, Entity.class, "getInterpolatedBoundingBox", float.class);
        }
    };

    /* true when no class between c and base declares its own version (a generic override counts too) */
    private static boolean declaredIn(Class<?> c, Class<?> base, String name, Class<?>... args) {
        for (Class<?> k = c; k != null && k != base; k = k.getSuperclass())
            for (Method m : k.getDeclaredMethods())
                if (m.getName().equals(name) && m.getParameterCount() == args.length)
                    return false;
        return base.isAssignableFrom(c);
    }

    public static boolean hidden(EntityRenderDispatcher d, Entity e, Frustum f, float partial) {
        if (f.getClass() != Frustum.class || !PLAIN_ENTITY.get(e.getClass()))
            return false;
        if (e instanceof Leashable l && l.getLeashHolder() != null)
            return false;
        EntityRenderer<?, ?> r = d.getRenderer(e);
        double m = r == null ? -1 : RENDERER_MARGIN.get(r.getClass());
        if (m < 0)
            return false;
        m += 0.01;                               /* float rounding in the frustum test */
        AABB bb = e.getBoundingBox();
        double dx = 0, dy = 0, dz = 0;
        double x = e.getX(), y = e.getY(), z = e.getZ();
        if (e.xOld != x || e.yOld != y || e.zOld != z) {
            dx = (e.xOld - x) * (1 - partial);   /* lerp(partial, old, x) - x */
            dy = (e.yOld - y) * (1 - partial);
            dz = (e.zOld - z) * (1 - partial);
        }
        /* NaN/empty boxes get vanilla's own fallback */
        double minX = bb.minX + dx - m, minY = bb.minY + dy - m, minZ = bb.minZ + dz - m;
        double maxX = bb.maxX + dx + m, maxY = bb.maxY + dy + m, maxZ = bb.maxZ + dz + m;
        if (Double.isNaN(minX + minY + minZ + maxX + maxY + maxZ) || bb.getSize() == 0)
            return false;
        tested++;
        double cx = f.getCamX(), cy = f.getCamY(), cz = f.getCamZ();
        boolean in = ((FrustumAccessor) f).tuxmc$intersection().testAab((float) (minX - cx), (float) (minY - cy),
                (float) (minZ - cz), (float) (maxX - cx), (float) (maxY - cy), (float) (maxZ - cz));
        if (!in)
            culled++;
        return !in;
    }

    private EntityCull() {}
}
