package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import dev.tux.mc.Zoom;
import net.minecraft.client.Minecraft;
import net.minecraft.client.MouseHandler;

/** the wheel zooms instead of switching hotbar slots while a zoom key is held */
@Mixin(MouseHandler.class)
public class ScrollZoomMixin {
    @Inject(method = "onScroll(JDD)V", at = @At("HEAD"), cancellable = true)
    private void tuxmc$scrollZoom(long window, double dx, double dy, CallbackInfo ci) {
        if (Zoom.active() && Minecraft.getInstance().gui.screen() == null && dy != 0) {
            Zoom.scroll(dy);
            ci.cancel();
        }
    }
}
