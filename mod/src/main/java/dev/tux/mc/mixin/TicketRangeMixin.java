package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;

import net.minecraft.server.level.DistanceManager;

/**
 * The player ticket tracker only spreads 32 chunks out, so chunks past 32 never
 * load even with a bigger view distance. It keeps levels in a Long2ByteMap and
 * reads them back as SIGNED bytes, with range + 2 as the "not near" default:
 * that has to stay <= 127, so 125 is the hard limit. (251 wrapped to -3, every
 * chunk counted as next to the player, and chunk loading fell apart.)
 */
@Mixin(DistanceManager.class)
public class TicketRangeMixin {
    @ModifyArg(method = "<init>", index = 1, at = @At(value = "INVOKE",
            target = "Lnet/minecraft/server/level/DistanceManager$PlayerTicketTracker;<init>(Lnet/minecraft/server/level/DistanceManager;I)V"))
    private int tuxmc$ticketRange(int old) {
        return 125;
    }
}
