package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

import dev.tux.mc.BlockLimit;
import net.minecraft.world.level.gamerules.GameRule;
import net.minecraft.world.level.gamerules.GameRules;

/** No block limit: the max_block_modifications check reads Integer.MAX_VALUE (singleplayer / LAN only). */
@Mixin(net.minecraft.server.commands.FillBiomeCommand.class)
public class FillBiomeLimitMixin {
    @Redirect(method = "fill(Lnet/minecraft/server/level/ServerLevel;Lnet/minecraft/core/BlockPos;Lnet/minecraft/core/BlockPos;Lnet/minecraft/core/Holder;Ljava/util/function/Predicate;Ljava/util/function/Consumer;)Lcom/mojang/datafixers/util/Either;", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/gamerules/GameRules;get(Lnet/minecraft/world/level/gamerules/GameRule;)Ljava/lang/Object;"))
    private static Object tuxmc$noLimit(GameRules rules, GameRule<?> rule) {
        return BlockLimit.get(rules, rule);
    }
}
