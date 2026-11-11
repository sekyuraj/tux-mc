package dev.tux.mc.mixin;

import org.joml.FrustumIntersection;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

import net.minecraft.client.renderer.culling.Frustum;

@Mixin(Frustum.class)
public interface FrustumAccessor {
    @Accessor("intersection")
    FrustumIntersection tuxmc$intersection();
}
