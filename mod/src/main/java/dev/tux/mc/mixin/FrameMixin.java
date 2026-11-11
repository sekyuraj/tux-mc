package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import dev.tux.mc.Perf;
import net.minecraft.client.Minecraft;

/** frame timing (Perf) and the "title screen is up" startup mark */
@Mixin(Minecraft.class)
public class FrameMixin {
    @Inject(method = "runTick(Z)V", at = @At("HEAD"))
    private void tuxmc$frame(boolean advance, CallbackInfo ci) {
        Perf.frame();
    }

    @Inject(method = "onGameLoadFinished", at = @At("TAIL"))
    private void tuxmc$loaded(CallbackInfo ci) {
        Perf.titleReady();
    }
}
