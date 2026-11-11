package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import dev.tux.mc.EntityCull;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.client.renderer.entity.EntityRenderDispatcher;
import net.minecraft.world.entity.Entity;

/** off-screen entities are turned down before Sodium and vanilla build their culling boxes (see EntityCull) */
@Mixin(value = EntityRenderDispatcher.class, priority = 900)
public class EntityCullMixin {
    @Inject(method = "shouldRender(Lnet/minecraft/world/entity/Entity;Lnet/minecraft/client/renderer/culling/Frustum;DDDF)Z",
            at = @At("HEAD"), cancellable = true)
    private void tuxmc$cull(Entity e, Frustum f, double x, double y, double z, float partial,
            CallbackInfoReturnable<Boolean> cir) {
        if (EntityCull.on && EntityCull.hidden((EntityRenderDispatcher) (Object) this, e, f, partial))
            cir.setReturnValue(false);
    }
}
