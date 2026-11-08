package dev.tux.mc.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;

import dev.tux.mc.Emotes;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;

@Mixin(AvatarRenderState.class)
public class AvatarRenderStateMixin implements Emotes.Holder, dev.tux.mc.Hats.Holder {
    @Unique private Emotes.Emote tuxmc$emote;
    @Unique private float tuxmc$time;
    @Unique private dev.tux.mc.Hats.Hat tuxmc$hat;

    @Override public dev.tux.mc.Hats.Hat tuxmc$hat() { return tuxmc$hat; }
    @Override public void tuxmc$setHat(dev.tux.mc.Hats.Hat h) { tuxmc$hat = h; }

    @Override public Emotes.Emote tuxmc$emote() { return tuxmc$emote; }
    @Override public float tuxmc$time() { return tuxmc$time; }
    @Override public void tuxmc$set(Emotes.Emote e, float t) {
        tuxmc$emote = e;
        tuxmc$time = t;
    }
}
