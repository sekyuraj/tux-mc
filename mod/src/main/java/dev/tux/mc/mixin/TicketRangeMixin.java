package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;

import net.minecraft.server.level.DistanceManager;

/**
 * The player ticket tracker only spreads 32 chunks out, so chunks past 32 never
 * load even with a bigger view distance. Its levels are bytes: the level count
 * (range + 2) has to stay under 254, which makes 251 the hard limit.
 */
@Mixin(DistanceManager.class)
public class TicketRangeMixin {
    @ModifyArg(method = "<init>", index = 1, at = @At(value = "INVOKE",
            target = "Lnet/minecraft/server/level/DistanceManager$PlayerTicketTracker;<init>(Lnet/minecraft/server/level/DistanceManager;I)V"))
    private int tuxmc$ticketRange(int old) {
        return 251;
    }
}
