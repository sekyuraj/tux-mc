package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import dev.tux.mc.FarView;

/** Distant Horizons starts its LODs where full detail ends (FarView.cap()) */
@Pseudo
@Mixin(targets = "com.seibel.distanthorizons.common.wrappers.minecraft.MinecraftRenderWrapper", remap = false)
public class DhFarMixin {
    @Inject(method = "getRenderDistance()I", at = @At("RETURN"), cancellable = true, require = 0)
    private void tuxmc$far(CallbackInfoReturnable<Integer> cir) {
        int c = FarView.cap();
        if (c > 0 && cir.getReturnValueI() > c)
            cir.setReturnValue(c);
    }
}
