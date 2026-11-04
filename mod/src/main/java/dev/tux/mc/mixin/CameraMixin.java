package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import dev.tux.mc.Zoom;
import net.minecraft.client.Camera;

@Mixin(Camera.class)
public class CameraMixin {
    @Inject(method = "calculateFov(F)F", at = @At("RETURN"), cancellable = true)
    private void tuxmc$zoom(float partial, CallbackInfoReturnable<Float> cir) {
        float k = Zoom.factor();
        if (k != 1f)
            cir.setReturnValue(cir.getReturnValueF() * k);
    }
}
