package dev.tux.mc;

import com.mojang.blaze3d.vertex.PoseStack;

import org.joml.Matrix4f;

import net.minecraft.client.Minecraft;
import net.minecraft.client.model.player.PlayerModel;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;

/**
 * Emotes on player models. Yours play locally; Sync sends the emote number so
 * other tux-mc players see it too, and plays theirs on their models here.
 */
public final class Emotes {
    public enum Emote {
        WAVE("Wave", 0), DANCE("Dance", 0), SIT("Sit", 0), LAY("Lay", 0),
        CLAP("Clap", 0), SPIN("Spin", 0), BOW("Bow", 2.2f), DAB("Dab", 1.8f);

        public final String title;
        public final float length;        /* seconds, 0 = keeps going until you move */

        Emote(String title, float length) {
            this.title = title;
            this.length = length;
        }
    }

    /** render-state side: set per frame, for you and for synced players */
    public interface Holder {
        Emote tuxmc$emote();
        float tuxmc$time();
        void tuxmc$set(Emote e, float t);
    }

    public static Emote current;
    private static long started;
    private static double sx, sz;
    /** spin speed, degrees per second, after a short wind-up */
    private static final float SPIN_SPEED = 900f;

    public static void play(Emote e) {
        Minecraft mc = Minecraft.getInstance();
        if (mc.player == null)
            return;
        if (current == e) {                 /* same one again toggles it off */
            stop();
            return;
        }
        current = e;
        started = System.nanoTime();
        Sync.sendEmote(e);
        sx = mc.player.getX();
        sz = mc.player.getZ();
        /* the camera stays as it is: F5 if you want to watch yourself */
    }

    public static void stop() {
        if (current != null)
            Sync.sendEmote(null);
        current = null;
    }

    public static float time() {
        return (System.nanoTime() - started) / 1e9f;
    }

    /** client tick: timed emotes run out; the rest keep going until you move,
     *  jump, sneak, sprint, or press Esc (being pushed away ends them too) */
    public static void tick(Minecraft mc) {
        if (current == null)
            return;
        if (mc.player == null) {
            stop();
            return;
        }
        var o = mc.options;
        boolean keys = o.keyUp.isDown() || o.keyDown.isDown() || o.keyLeft.isDown() || o.keyRight.isDown()
                || o.keyJump.isDown() || o.keyShift.isDown() || o.keySprint.isDown()
                || mc.gui.screen() instanceof net.minecraft.client.gui.screens.PauseScreen;
        double moved = Math.hypot(mc.player.getX() - sx, mc.player.getZ() - sz);
        if (keys || (current.length > 0 && time() > current.length) || moved > 0.35
                || mc.player.getDeltaMovement().y > 0.2 || mc.player.isPassenger())
            stop();
    }

    /** extract time: pose flags vanilla already knows how to draw */
    public static void extract(AvatarRenderState s, Emote e, float t) {
        if (e == Emote.SIT)
            s.isPassenger = true;
        if (e == Emote.BOW)
            s.isCrouching = bowAmount(t) > 0.5f;
    }

    private static float bowAmount(float t) {
        return (float) Math.max(0, Math.sin(Math.min(t, 2.0f) / 2.0f * Math.PI));
    }

    /** after PlayerModel.setupAnim */
    public static void pose(PlayerModel m, Emote e, float t) {
        float ease = Math.min(1, t * 5);       /* blend in over 0.2 s */
        switch (e) {
        case WAVE -> {
            m.rightArm.xRot = 0;
            m.rightArm.yRot = 0;
            m.rightArm.zRot = lerp(m.rightArm.zRot, 2.55f + (float) Math.sin(t * 11) * 0.35f, ease);
            m.head.zRot = (float) Math.sin(t * 5.5) * 0.08f;
        }
        case DANCE -> {
            float b = (float) Math.sin(t * 7);
            m.rightArm.xRot = 0;
            m.leftArm.xRot = 0;
            m.rightArm.zRot = 0.3f + Math.max(0, b) * 2.3f;
            m.leftArm.zRot = -0.3f - Math.max(0, -b) * 2.3f;
            m.body.yRot = (float) Math.sin(t * 3.5) * 0.35f;
            m.head.zRot = b * 0.18f;
            m.rightLeg.xRot = Math.max(0, b) * -0.5f;
            m.leftLeg.xRot = Math.max(0, -b) * -0.5f;
        }
        case CLAP -> {
            float g = 0.25f + 0.35f * (float) Math.abs(Math.cos(t * 7.5));
            m.rightArm.xRot = -1.25f;
            m.leftArm.xRot = -1.25f;
            m.rightArm.yRot = -g;
            m.leftArm.yRot = g;
            m.rightArm.zRot = 0;
            m.leftArm.zRot = 0;
        }
        case SPIN -> {
            m.rightArm.zRot = 1.45f;
            m.leftArm.zRot = -1.45f;
            m.rightArm.xRot = 0;
            m.leftArm.xRot = 0;
        }
        case BOW -> {
            float a = bowAmount(t);
            m.head.xRot = lerp(m.head.xRot, 0.7f, a);
            m.rightArm.xRot = lerp(m.rightArm.xRot, 0.3f, a);
            m.leftArm.xRot = lerp(m.leftArm.xRot, 0.3f, a);
            m.rightArm.yRot = -0.4f * a;
        }
        case DAB -> {
            m.head.xRot = 0.55f * ease;
            m.head.yRot = -0.65f * ease;
            m.rightArm.xRot = -0.35f;
            m.rightArm.yRot = 0;
            m.rightArm.zRot = 2.2f * ease;
            m.leftArm.xRot = -1.7f * ease;
            m.leftArm.yRot = 0.95f * ease;
            m.leftArm.zRot = 0;
        }
        case SIT -> {
            m.rightArm.xRot = -0.35f;
            m.leftArm.xRot = -0.35f;
            m.rightArm.zRot = 0.12f;
            m.leftArm.zRot = -0.12f;
        }
        case LAY -> {
            m.head.xRot = 0;
            m.rightArm.xRot = 0;
            m.leftArm.xRot = 0;
            m.rightArm.zRot = 0.18f;
            m.leftArm.zRot = -0.18f;
            m.rightLeg.xRot = 0;
            m.leftLeg.xRot = 0;
            m.rightLeg.zRot = 0.06f;
            m.leftLeg.zRot = -0.06f;
        }
        }
    }

    /** after AvatarRenderer.setupRotations: whole-body moves (feet are the origin) */
    public static void rotate(PoseStack ps, Emote e, float t) {
        switch (e) {
        case SIT -> ps.translate(0, -0.62f, 0);
        case LAY -> {
            float a = Math.min(1, t * 2.5f);
            float k = 1 - (1 - a) * (1 - a);
            ps.translate(0, 0.14f * k, 0);
            ps.mulPose(new Matrix4f().rotationX((float) Math.toRadians(90 * k)));
        }
        case SPIN -> ps.mulPose(new Matrix4f().rotationY((float) Math.toRadians(spinAngle(t) % 360)));
        case DANCE -> ps.translate(0, Math.abs(Math.sin(t * 7)) * 0.07f, 0);
        default -> {}
        }
    }

    /** constant fast turning; it winds up over the first 0.4 s (same speed where they meet) */
    private static float spinAngle(float t) {
        return t < 0.4f ? SPIN_SPEED * t * t / 0.8f : SPIN_SPEED * (t - 0.2f);
    }

    /**
     * First person: your own arms, moved for the emote, before the arm is put in
     * place (view space, x right, y up, z towards you). right = which arm.
     * Returns false to leave that arm out.
     */
    public static boolean firstPersonArm(PoseStack ps, Emote e, float t, boolean right) {
        float s = right ? 1 : -1, ease = Math.min(1, t * 5);
        switch (e) {
        case WAVE -> {
            if (right) {
                ps.translate(0.02f, 0.42f * ease, 0.05f);
                roll(ps, (float) Math.sin(t * 11) * 22f * ease, 0.56f, -0.2f, -0.72f);
            } else {
                ps.translate(0, -0.12f * ease, 0);
            }
        }
        case CLAP -> {
            float g = (float) Math.abs(Math.cos(t * 7.5));
            ps.translate(-s * (0.36f - 0.16f * g) * ease, 0.14f * ease, -0.05f * ease);
            roll(ps, -s * 12f * ease, 0.6f * s, -0.6f, -0.72f);
        }
        case DANCE -> {
            float b = (float) Math.sin(t * 7), up = Math.max(0, s * b);
            ps.translate(0, (up * 0.45f - 0.05f) * ease, 0);
            roll(ps, s * up * 28f * ease, 0.6f * s, -0.6f, -0.72f);
        }
        case SPIN -> {
            ps.translate(s * 0.22f * ease, (0.12f + (float) Math.sin(t * 9) * 0.03f) * ease, 0);
            roll(ps, -s * 24f * ease, 0.6f * s, -0.6f, -0.72f);
        }
        case SIT -> ps.translate(0, -0.15f * ease, 0.04f * ease);
        case LAY -> ps.translate(0, -0.3f * ease, 0.06f * ease);
        case BOW -> ps.translate(0, -0.3f * bowAmount(t), 0.1f * bowAmount(t));
        case DAB -> {
            if (right) {
                ps.translate(-0.42f * ease, 0.3f * ease, 0.05f * ease);
                roll(ps, 35f * ease, 0.6f, -0.6f, -0.72f);
            } else {
                ps.translate(-0.08f * ease, 0.36f * ease, 0);
                roll(ps, -40f * ease, -0.6f, -0.6f, -0.72f);
            }
        }
        }
        return true;
    }

    /** turn around the view axis at a point (where the arm sits) */
    private static void roll(PoseStack ps, float deg, float px, float py, float pz) {
        ps.translate(px, py, pz);
        ps.mulPose(new Matrix4f().rotationZ((float) Math.toRadians(deg)));
        ps.translate(-px, -py, -pz);
    }

    private static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    private Emotes() {}
}
