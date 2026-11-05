package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/**
 * PlayerTicketTracker.updateViewDistance walks the same byte map and passes the
 * sign-extended level to onLevelChange(pos, level, hadTicket, hasTicket).
 * Redo that call with the level read unsigned (see TicketLevelMixin).
 */
@Mixin(targets = "net.minecraft.server.level.DistanceManager$PlayerTicketTracker")
public abstract class TicketViewMixin {
    @Shadow
    private void onLevelChange(long pos, int level, boolean hadTicket, boolean hasTicket) {}

    @Shadow
    private boolean haveTicketFor(int level) { return false; }

    @Redirect(method = "updateViewDistance(I)V", at = @At(value = "INVOKE",
            target = "Lnet/minecraft/server/level/DistanceManager$PlayerTicketTracker;onLevelChange(JIZZ)V"))
    private void tuxmc$unsignedLevel(@org.spongepowered.asm.mixin.injection.Coerce Object self, long pos, int level,
                                     boolean hadTicket, boolean hasTicket, int newViewDistance) {
        int l = level & 0xFF;
        onLevelChange(pos, l, haveTicketFor(l), l <= newViewDistance);
    }
}
