package dev.tux.mc;

import net.minecraft.world.level.gamerules.GameRule;
import net.minecraft.world.level.gamerules.GameRules;

/** Shared by the Fill/Clone/FillBiome limit mixins: the area check sees no limit. */
public final class BlockLimit {
    private BlockLimit() {}

    public static Object get(GameRules rules, GameRule<?> rule) {
        if (rule == GameRules.MAX_BLOCK_MODIFICATIONS)
            return Integer.MAX_VALUE;
        return rules.get(rule);
    }
}
