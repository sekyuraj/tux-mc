package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;

import net.minecraft.server.level.DistanceManager;

/**
 * The player ticket tracker only spreads 32 chunks out, so chunks past 32 never
 * load even with a bigger view distance. Its levels are bytes (read unsigned
 * by TicketLevelMixin/TicketViewMixin; vanilla reads them signed, which broke
 * everything past 125) and its graph takes at most 253 levels, so 251 would be
 * the hard limit. 125 is what the server can actually serve: tested at 251, it
 * fell behind generating ~250k chunks (20 GB, 12 fps) and the chunks around the
 * player never arrived. For a farther view, raise Distant Horizons' LOD radius.
 */
@Mixin(DistanceManager.class)
public class TicketRangeMixin {
    @ModifyArg(method = "<init>", index = 1, at = @At(value = "INVOKE",
            target = "Lnet/minecraft/server/level/DistanceManager$PlayerTicketTracker;<init>(Lnet/minecraft/server/level/DistanceManager;I)V"))
    private int tuxmc$ticketRange(int old) {
        return 125;
    }
}
