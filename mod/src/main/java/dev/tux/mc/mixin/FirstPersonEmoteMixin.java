package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import com.mojang.blaze3d.vertex.PoseStack;

import dev.tux.mc.Emotes;
import net.minecraft.client.renderer.FirstPersonHandsAndItemsRenderer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.state.level.FirstPersonHandsAndItemsRenderState;
import net.minecraft.client.renderer.state.level.PlayerRenderState;
import net.minecraft.world.entity.HumanoidArm;

/** while you emote in first person: both bare arms, moving with the emote, instead of held items */
@Mixin(FirstPersonHandsAndItemsRenderer.class)
public abstract class FirstPersonEmoteMixin {
    @Shadow
    private void renderPlayerArm(PoseStack ps, SubmitNodeCollector c, int light, float equip, float attack,
                                 HumanoidArm arm, PlayerRenderState state) {}

    @Inject(method = "submitHandsWithItems", at = @At("HEAD"), cancellable = true)
    private void tuxmc$emoteArms(float partial, PoseStack ps, SubmitNodeCollector c, PlayerRenderState state,
                                 FirstPersonHandsAndItemsRenderState hands, CallbackInfo ci) {
        Emotes.Emote e = Emotes.current;
        if (e == null || state.avatarRenderState == null)
            return;
        float t = Emotes.time();
        int light = state.avatarRenderState.lightCoords;
        for (HumanoidArm arm : HumanoidArm.values()) {
            ps.pushPose();
            if (Emotes.firstPersonArm(ps, e, t, arm == HumanoidArm.RIGHT))
                renderPlayerArm(ps, c, light, 0f, 0f, arm, state);
            ps.popPose();
        }
        ci.cancel();
    }
}
