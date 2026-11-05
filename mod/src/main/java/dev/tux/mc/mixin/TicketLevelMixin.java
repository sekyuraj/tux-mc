package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * The player ticket tracker keeps each chunk's distance in a Long2ByteMap and
 * reads it back signed, so distances past 127 came out negative (= "right next
 * to the player"). Read them unsigned; with that the tracker goes to 251 chunks
 * (its graph allows at most 253 levels: range + 2).
 */
@Mixin(targets = "net.minecraft.server.level.DistanceManager$FixedPlayerDistanceChunkTracker")
public class TicketLevelMixin {
    @Inject(method = "getLevel(J)I", at = @At("RETURN"), cancellable = true)
    private void tuxmc$unsignedLevel(long pos, CallbackInfoReturnable<Integer> cir) {
        cir.setReturnValue(cir.getReturnValueI() & 0xFF);
    }
}
