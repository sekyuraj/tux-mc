package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import com.mojang.blaze3d.vertex.PoseStack;

import dev.tux.mc.Capes;
import dev.tux.mc.Emotes;
import dev.tux.mc.Sync;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.entity.player.AvatarRenderer;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.world.entity.Avatar;
import net.minecraft.world.entity.player.PlayerSkin;

/** emotes + client capes: yours, and other tux-mc players' (Sync) */
@Mixin(AvatarRenderer.class)
public class AvatarRendererMixin {
    @Inject(method = "extractRenderState(Lnet/minecraft/world/entity/Avatar;Lnet/minecraft/client/renderer/entity/state/AvatarRenderState;F)V",
            at = @At("TAIL"))
    private void tuxmc$extract(Avatar entity, AvatarRenderState s, float partial, CallbackInfo ci) {
        Emotes.Holder h = (Emotes.Holder) s;
        boolean me = entity == Minecraft.getInstance().player;
        java.util.UUID self = me ? Sync.selfTest(entity.getUUID()) : null;
        if (self != null)
            me = false;                          /* test: draw yourself the way others see you */
        java.util.UUID id = self != null ? self : entity.getUUID();
        /* adaptive mode under pressure: players 48+ blocks away skip emote poses and hats (capes stay) */
        boolean far = !me && dev.tux.mc.Perf.pressure && Minecraft.getInstance().player != null
                && entity.distanceToSqr(Minecraft.getInstance().player) > 48 * 48;
        Sync.RemoteEmote re = me || far ? null : Sync.emote(id);
        if (me && Emotes.current != null) {
            h.tuxmc$set(Emotes.current, Emotes.time());
            Emotes.extract(s, Emotes.current, Emotes.time());
        } else if (re != null) {
            h.tuxmc$set(re.emote(), re.time());
            Emotes.extract(s, re.emote(), re.time());
        } else {
            h.tuxmc$set(null, 0);
        }
        ((dev.tux.mc.Hats.Holder) s).tuxmc$setHat(me ? dev.tux.mc.Hats.own() : far ? null : dev.tux.mc.Hats.remote(id));
        if (s.skin != null) {
            var cape = me ? Capes.current() : Capes.remote(id, Sync.cape(id));
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
