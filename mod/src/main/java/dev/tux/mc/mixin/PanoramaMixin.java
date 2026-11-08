package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import dev.tux.mc.MenuBg;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.renderer.Panorama;

/** your own picture instead of the spinning panorama behind the menus */
@Mixin(Panorama.class)
public class PanoramaMixin {
    @Inject(method = "extractRenderState(Lnet/minecraft/client/gui/GuiGraphicsExtractor;II)V", at = @At("HEAD"), cancellable = true)
    private void tuxmc$picture(GuiGraphicsExtractor g, int w, int h, CallbackInfo ci) {
        if (MenuBg.draw(g, w, h))
            ci.cancel();
    }
}
