package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

import dev.tux.mc.Config;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.MultiPlayerGameMode;

/**
 * Clicking air sets missTime = 10, and startAttack drops every click for the
 * next half second. In 1.8-style PvP (Hypixel) you miss a lot while strafing,
 * so most hits never get sent. With hitFix on, a miss leaves missTime alone.
 */
@Mixin(Minecraft.class)
public class HitDelayMixin {
    @Redirect(method = "startAttack()Z", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/client/multiplayer/MultiPlayerGameMode;hasMissTime()Z"))
    private boolean tuxmc$noMissTime(MultiPlayerGameMode gm) {
        return !Config.hitFix && gm.hasMissTime();
    }
}
