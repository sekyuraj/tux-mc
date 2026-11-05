package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.ModifyConstant;
import org.spongepowered.asm.mixin.injection.Slice;

import net.minecraft.client.Options;

/**
 * Render distance slider up to 256 chunks instead of 32. Sodium builds its own
 * slider from this same OptionInstance range, so it follows along.
 */
@Mixin(Options.class)
public class RenderDistanceMixin {
    @ModifyConstant(method = "<init>", constant = @Constant(intValue = 32),
            slice = @Slice(
                    from = @At(value = "CONSTANT", args = "stringValue=options.renderDistance"),
                    to = @At(value = "FIELD", opcode = org.objectweb.asm.Opcodes.PUTFIELD,
                            target = "Lnet/minecraft/client/Options;renderDistance:Lnet/minecraft/client/OptionInstance;")))
    private int tuxmc$maxRenderDistance(int old) {
        return 256;
    }
}
