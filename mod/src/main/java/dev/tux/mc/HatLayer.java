package dev.tux.mc;

import com.mojang.blaze3d.vertex.PoseStack;

import net.minecraft.client.model.player.PlayerModel;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.entity.LivingEntityRenderer;
import net.minecraft.client.renderer.entity.RenderLayerParent;
import net.minecraft.client.renderer.entity.layers.RenderLayer;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.client.renderer.rendertype.RenderTypes;
import net.minecraft.resources.Identifier;

/** draws a Hats.Hat on the head, after the head turned and bobbed like the skin */
public class HatLayer extends RenderLayer<AvatarRenderState, PlayerModel> {
    private static final Identifier WHITE = Identifier.fromNamespaceAndPath("tuxmc", "textures/white.png");

    public HatLayer(RenderLayerParent<AvatarRenderState, PlayerModel> parent) {
        super(parent);
    }

    @Override
    public void submit(PoseStack ps, SubmitNodeCollector c, int light, AvatarRenderState s, float yRot, float xRot) {
        Hats.Hat h = ((Hats.Holder) s).tuxmc$hat();
        if (h == null || s.isInvisible)
            return;
        if (Config.hatHideHelmet && (!s.headItem.isEmpty() || (s.headEquipment != null && !s.headEquipment.isEmpty())))
            return;
        int overlay = LivingEntityRenderer.getOverlayCoords(s, 0f);
        ps.pushPose();
        getParentModel().root().translateAndRotate(ps);
        getParentModel().getHead().translateAndRotate(ps);
        c.submitCustomGeometry(ps, RenderTypes.entityCutout(WHITE), (pose, vc) -> h.emit(pose, vc, light, overlay));
        ps.popPose();
    }
}
