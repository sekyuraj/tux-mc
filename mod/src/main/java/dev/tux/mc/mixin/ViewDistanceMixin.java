package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.ModifyConstant;

import net.minecraft.server.level.ChunkMap;

/**
 * Singleplayer/LAN: the integrated server clamps view distance to 32. Raise it
 * to 125, the most the player ticket tracker can hold (see TicketRangeMixin).
 */
@Mixin(ChunkMap.class)
public class ViewDistanceMixin {
    @ModifyConstant(method = "setServerViewDistance(I)V", constant = @Constant(intValue = 32))
    private int tuxmc$maxViewDistance(int old) {
        return 125;
    }
}
