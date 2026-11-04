package dev.tux.mc;

import com.mojang.blaze3d.vertex.PoseStack;

import org.joml.Matrix4f;

import net.minecraft.client.CameraType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.model.player.PlayerModel;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;

/**
 * Emotes on your own player model. Purely visual and client-side: other players
 * don't see them (that would need a server plugin).
 */
public final class Emotes {
    public enum Emote {
        WAVE("Wave", 2.6f), DANCE("Dance", 0), SIT("Sit", 0), LAY("Lay", 0),
        CLAP("Clap", 2.4f), SPIN("Spin", 1.5f), BOW("Bow", 2.2f), DAB("Dab", 1.8f);

        public final String title;
        public final float length;        /* seconds, 0 = until you move */

        Emote(String title, float length) {
            this.title = title;
            this.length = length;
        }
    }

    /** render-state side: set per frame for the local player only */
    public interface Holder {
        Emote tuxmc$emote();
        float tuxmc$time();
        void tuxmc$set(Emote e, float t);
    }

    public static Emote current;
    private static long started;
    private static CameraType restoreCamera;
    private static double sx, sz;

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
        sx = mc.player.getX();
        sz = mc.player.getZ();
        if (Config.emoteCamera && restoreCamera == null && mc.options.getCameraType() == CameraType.FIRST_PERSON) {
            restoreCamera = CameraType.FIRST_PERSON;
            mc.options.setCameraType(CameraType.THIRD_PERSON_FRONT);
        }
    }

    public static void stop() {
        current = null;
        if (restoreCamera != null) {
            Minecraft.getInstance().options.setCameraType(restoreCamera);
            restoreCamera = null;
        }
    }

    public static float time() {
        return (System.nanoTime() - started) / 1e9f;
    }

    /** client tick: timed emotes end, any emote ends when you walk off or jump */
    public static void tick(Minecraft mc) {
        if (current == null)
            return;
        if (mc.player == null) {
            stop();
            return;
        }
        double moved = Math.hypot(mc.player.getX() - sx, mc.player.getZ() - sz);
        if ((current.length > 0 && time() > current.length) || moved > 0.35
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
        case SPIN -> {
            float a = t / Emote.SPIN.length;
            float k = a < 0.5f ? 2 * a * a : 1 - (float) Math.pow(-2 * a + 2, 2) / 2;
            ps.mulPose(new Matrix4f().rotationY((float) Math.toRadians(720 * k)));
        }
        case DANCE -> ps.translate(0, Math.abs(Math.sin(t * 7)) * 0.07f, 0);
        default -> {}
        }
    }

    private static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    private Emotes() {}
}
