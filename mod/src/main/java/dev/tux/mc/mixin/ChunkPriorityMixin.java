package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

import net.minecraft.server.level.ChunkTaskPriorityQueue;

/**
 * Chunk jobs are queued by distance to the player, in PRIORITY_LEVEL_COUNT (46)
 * buckets. Vanilla's 32-chunk cap keeps distances below that; with the bigger
 * view distance (ViewDistanceMixin) a job 46+ chunks out indexed past the end
 * and every worker thread crashed. Far chunks share the last bucket instead.
 */
@Mixin(ChunkTaskPriorityQueue.class)
public class ChunkPriorityMixin {
    @ModifyVariable(method = "submit(Ljava/lang/Runnable;JI)V", at = @At("HEAD"), argsOnly = true, ordinal = 0)
    private int tuxmc$submitPriority(int level) {
        return Math.min(level, ChunkTaskPriorityQueue.PRIORITY_LEVEL_COUNT - 1);
    }

    @ModifyVariable(method = "resortChunkTasks(ILnet/minecraft/world/level/ChunkPos;I)V", at = @At("HEAD"),
            argsOnly = true, ordinal = 0)
    private int tuxmc$oldPriority(int level) {
        return Math.min(level, ChunkTaskPriorityQueue.PRIORITY_LEVEL_COUNT - 1);
    }

    @ModifyVariable(method = "resortChunkTasks(ILnet/minecraft/world/level/ChunkPos;I)V", at = @At("HEAD"),
            argsOnly = true, ordinal = 1)
    private int tuxmc$newPriority(int level) {
        return Math.min(level, ChunkTaskPriorityQueue.PRIORITY_LEVEL_COUNT - 1);
    }
}
