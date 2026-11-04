package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import dev.tux.mc.ServerTools;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.network.protocol.game.ClientboundSetActionBarTextPacket;
import net.minecraft.network.protocol.game.ClientboundSetSubtitleTextPacket;
import net.minecraft.network.protocol.game.ClientboundSetTimePacket;
import net.minecraft.network.protocol.game.ClientboundSetTitleTextPacket;
import net.minecraft.network.protocol.game.ClientboundTabListPacket;

/* TAIL: handlers first bounce from the network thread to the game thread; TAIL only runs on the second pass */
@Mixin(ClientPacketListener.class)
public class PacketListenerMixin {
    @Inject(method = "handleSetTime", at = @At("TAIL"))
    private void tuxmc$time(ClientboundSetTimePacket p, CallbackInfo ci) { ServerTools.time(p.gameTime()); }

    @Inject(method = "setTitleText", at = @At("TAIL"))
    private void tuxmc$title(ClientboundSetTitleTextPacket p, CallbackInfo ci) { ServerTools.text(p.text()); }

    @Inject(method = "setSubtitleText", at = @At("TAIL"))
    private void tuxmc$subtitle(ClientboundSetSubtitleTextPacket p, CallbackInfo ci) { ServerTools.text(p.text()); }

    @Inject(method = "setActionBarText", at = @At("TAIL"))
    private void tuxmc$actionbar(ClientboundSetActionBarTextPacket p, CallbackInfo ci) { ServerTools.text(p.text()); }

    @Inject(method = "handleTabListCustomisation", at = @At("TAIL"))
    private void tuxmc$tab(ClientboundTabListPacket p, CallbackInfo ci) {
        ServerTools.text(p.header());
        ServerTools.text(p.footer());
    }
}
