package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import dev.tux.mc.Config;
import net.minecraft.client.renderer.LightmapRenderStateExtractor;
import net.minecraft.client.renderer.state.LightmapRenderState;

/** fullbright = permanent night vision in the lightmap only (no potion, nothing sent) */
@Mixin(LightmapRenderStateExtractor.class)
public class LightmapMixin {
    @Inject(method = "extract", at = @At("TAIL"))
    private void tuxmc$fullbright(LightmapRenderState s, float partial, CallbackInfo ci) {
        if (!Config.fullbright)
            return;
        s.nightVisionEffectIntensity = 1f;
        s.nightVisionColor = LightmapRenderStateExtractor.WHITE;
        s.darknessEffectScale = 0f;
        s.needsUpdate = true;
    }
}
