package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import dev.tux.mc.FarView;

/** Sodium searches, meshes and draws chunks only up to FarView.cap() (blocks = chunks * 16) */
@Pseudo
@Mixin(targets = "net.caffeinemc.mods.sodium.client.render.chunk.RenderSectionManager", remap = false)
public class SodiumFarMixin {
    @Inject(method = "getRenderDistance()F", at = @At("RETURN"), cancellable = true, require = 0)
    private void tuxmc$far(CallbackInfoReturnable<Float> cir) {
        int c = FarView.cap();
        if (c > 0 && cir.getReturnValueF() > c * 16f)
            cir.setReturnValue(c * 16f);
    }
}
