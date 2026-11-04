package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

import dev.tux.mc.BlockLimit;
import net.minecraft.world.level.gamerules.GameRule;
import net.minecraft.world.level.gamerules.GameRules;

/** No block limit: the max_block_modifications check reads Integer.MAX_VALUE (singleplayer / LAN only). */
@Mixin(net.minecraft.server.commands.FillCommand.class)
public class FillLimitMixin {
    @Redirect(method = "fillBlocks(Lnet/minecraft/commands/CommandSourceStack;Lnet/minecraft/world/level/levelgen/structure/BoundingBox;Lnet/minecraft/commands/arguments/blocks/BlockInput;Lnet/minecraft/server/commands/FillCommand$Mode;Ljava/util/function/Predicate;Z)I", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/gamerules/GameRules;get(Lnet/minecraft/world/level/gamerules/GameRule;)Ljava/lang/Object;"))
    private static Object tuxmc$noLimit(GameRules rules, GameRule<?> rule) {
        return BlockLimit.get(rules, rule);
    }
}
