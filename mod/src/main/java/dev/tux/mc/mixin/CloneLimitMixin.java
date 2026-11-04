package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

import dev.tux.mc.BlockLimit;
import net.minecraft.world.level.gamerules.GameRule;
import net.minecraft.world.level.gamerules.GameRules;

/** No block limit: the max_block_modifications check reads Integer.MAX_VALUE (singleplayer / LAN only). */
@Mixin(net.minecraft.server.commands.CloneCommands.class)
public class CloneLimitMixin {
    @Redirect(method = "clone(Lnet/minecraft/commands/CommandSourceStack;Lnet/minecraft/server/commands/CloneCommands$DimensionAndPosition;Lnet/minecraft/server/commands/CloneCommands$DimensionAndPosition;Lnet/minecraft/server/commands/CloneCommands$DimensionAndPosition;Ljava/util/function/Predicate;Lnet/minecraft/server/commands/CloneCommands$Mode;Z)I", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/gamerules/GameRules;get(Lnet/minecraft/world/level/gamerules/GameRule;)Ljava/lang/Object;"))
    private static Object tuxmc$noLimit(GameRules rules, GameRule<?> rule) {
        return BlockLimit.get(rules, rule);
    }
}
