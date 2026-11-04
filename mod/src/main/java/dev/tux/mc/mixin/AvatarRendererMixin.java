package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import com.mojang.blaze3d.vertex.PoseStack;

import dev.tux.mc.Capes;
import dev.tux.mc.Emotes;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.entity.player.AvatarRenderer;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.world.entity.Avatar;
import net.minecraft.world.entity.player.PlayerSkin;

/** your own player: emote state + the custom cape */
@Mixin(AvatarRenderer.class)
public class AvatarRendererMixin {
    @Inject(method = "extractRenderState(Lnet/minecraft/world/entity/Avatar;Lnet/minecraft/client/renderer/entity/state/AvatarRenderState;F)V",
            at = @At("TAIL"))
    private void tuxmc$extract(Avatar entity, AvatarRenderState s, float partial, CallbackInfo ci) {
        Emotes.Holder h = (Emotes.Holder) s;
        boolean me = entity == Minecraft.getInstance().player;
        if (me && Emotes.current != null) {
            h.tuxmc$set(Emotes.current, Emotes.time());
            Emotes.extract(s, Emotes.current, Emotes.time());
        } else {
            h.tuxmc$set(null, 0);
        }
        if (me && s.skin != null) {
            var cape = Capes.current();
            if (cape != null) {
                PlayerSkin k = s.skin;
                s.skin = new PlayerSkin(k.body(), cape, k.elytra(), k.model(), k.secure());
                s.showCape = true;
            }
        }
    }

    @Inject(method = "setupRotations(Lnet/minecraft/client/renderer/entity/state/AvatarRenderState;Lcom/mojang/blaze3d/vertex/PoseStack;FF)V",
            at = @At("TAIL"))
    private void tuxmc$rotate(AvatarRenderState s, PoseStack ps, float bodyRot, float scale, CallbackInfo ci) {
        Emotes.Holder h = (Emotes.Holder) s;
        if (h.tuxmc$emote() != null)
            Emotes.rotate(ps, h.tuxmc$emote(), h.tuxmc$time());
    }
}
