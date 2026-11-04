package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import dev.tux.mc.Emotes;
import net.minecraft.client.model.player.PlayerModel;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;

@Mixin(PlayerModel.class)
public class PlayerModelMixin {
    @Inject(method = "setupAnim(Lnet/minecraft/client/renderer/entity/state/AvatarRenderState;)V", at = @At("TAIL"))
    private void tuxmc$pose(AvatarRenderState s, CallbackInfo ci) {
        Emotes.Holder h = (Emotes.Holder) s;
        if (h.tuxmc$emote() != null)
            Emotes.pose((PlayerModel) (Object) this, h.tuxmc$emote(), h.tuxmc$time());
    }
}
