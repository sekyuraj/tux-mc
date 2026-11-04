package dev.tux.mc;

import java.util.ArrayList;
import java.util.List;

import dev.tux.mc.ui.Draw;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;

/** info box (a corner of your choice), armor/totems by the hotbar, the tracked waypoint top centre, toasts top right */
public final class Hud {
    public static void extract(GuiGraphicsExtractor g, DeltaTracker dt) {
        Notify.extract(g);
        Minecraft mc = Minecraft.getInstance();
        if (mc.player == null)
            return;
        if (Config.hud)
            info(g, mc);
        if (Config.hudArmor)
            armor(g, mc);
        if (Config.waypointArrow)
            waypoint(g, mc);
    }

    private static void info(GuiGraphicsExtractor g, Minecraft mc) {
        var p = mc.player;
        var f = mc.font;
        List<String> lines = new ArrayList<>();
        List<Integer> cols = new ArrayList<>();
        StringBuilder sb = new StringBuilder();
        sb.append(mc.getFps()).append(" fps");
        int ping = TuxMc.ping();
        if (ping >= 0)
            sb.append("  ").append(ping).append(" ms");
        if (Config.hudServer && ServerTools.tpsKnown && !mc.isLocalServer())
            sb.append(String.format("  %.1f tps", ServerTools.tps));
        lines.add(sb.toString());
        cols.add(Theme.fg);
        if (Config.hudCoords) {
            if (Config.streamer) {
                lines.add("coords hidden" + (Config.hudFacing ? "  " + p.getDirection().getName() : ""));
            } else {
                String nether = "";
                String d = Waypoints.dim();
                if (d.equals("overworld"))
                    nether = String.format("  (%d %d)", p.getBlockX() / 8, p.getBlockZ() / 8);
                else if (d.equals("nether"))
                    nether = String.format("  (%d %d)", p.getBlockX() * 8, p.getBlockZ() * 8);
                lines.add(String.format("%d %d %d%s%s", p.getBlockX(), p.getBlockY(), p.getBlockZ(), nether,
                        Config.hudFacing ? "  " + p.getDirection().getName() : ""));
            }
            cols.add(Theme.subtext);
        }
        if (Config.hudServer && ServerTools.inQueue()) {
            double eta = ServerTools.queueEta();
            lines.add("queue " + ServerTools.queuePos + (eta > 0 ? String.format("  ~%s", eta(eta)) : ""));
            cols.add(Theme.accentBright);
        }
        if (ServerTools.nearCount() > 0 && Config.visualRange) {
            lines.add(ServerTools.nearCount() + " player" + (ServerTools.nearCount() == 1 ? "" : "s") + " near");
            cols.add(Theme.urgent);
        }
        int w = 0;
        for (String l : lines)
            w = Math.max(w, f.width(l));
        w += 12;
        int h = lines.size() * 10 + 5;
        float sc = Math.max(0.5f, Math.min(2, Config.hudScale));
        boolean right = (Config.hudCorner & 1) != 0, bottom = Config.hudCorner >= 2;
        float x = right ? g.guiWidth() - 4 - w * sc : 4, y = bottom ? g.guiHeight() - 4 - h * sc - (right ? 0 : 50) : 4;
        if (bottom && right)
            y -= 24;                               /* above the armor row */
        var m = g.pose();
        m.pushMatrix();
        m.translate(x, y);
        m.scale(sc, sc);
        float bga = Math.max(0, Math.min(1, Config.hudOpacity));
        if (bga > 0) {
            Draw.panel(g, 0, 0, w, h, Theme.alpha(Theme.bg, bga), Theme.alpha(Theme.surface, Math.min(1, bga + 0.18f)));
            g.fill(0, 2, 1, h - 2, Theme.accent);
        }
        for (int i = 0; i < lines.size(); i++)
            g.text(f, lines.get(i), 6, 4 + i * 10, cols.get(i), bga == 0);
        m.popMatrix();
    }

    public static String eta(double min) {
        if (min < 1)
            return "<1 min";
        if (min < 60)
            return Math.round(min) + " min";
        return String.format("%dh %02dm", (int) (min / 60), (int) (min % 60));
    }

    /* armor pieces + elytra with durability bars, totem count, right of the hotbar */
    private static void armor(GuiGraphicsExtractor g, Minecraft mc) {
        var p = mc.player;
        int x = g.guiWidth() / 2 + 98, y = g.guiHeight() - 20;
        EquipmentSlot[] slots = {EquipmentSlot.HEAD, EquipmentSlot.CHEST, EquipmentSlot.LEGS, EquipmentSlot.FEET};
        for (EquipmentSlot s : slots) {
            ItemStack st = p.getItemBySlot(s);
            if (st.isEmpty())
                continue;
            g.item(st, x, y);
            if (st.isDamageableItem() && st.getMaxDamage() > 0) {
                float left = 1 - st.getDamageValue() / (float) st.getMaxDamage();
                int col = left > 0.5f ? Theme.accent : left > 0.2f ? Theme.warn : Theme.urgent;
                g.fill(x + 1, y + 16, x + 15, y + 18, Theme.alpha(Theme.bg, 0.8f));
                g.fill(x + 1, y + 16, x + 1 + Math.round(14 * left), y + 18, col);
            }
            x += 18;
        }
        int totems = 0;
        var inv = p.getInventory();
        for (int i = 0; i < inv.getContainerSize(); i++)
            if (inv.getItem(i).getItem() == Items.TOTEM_OF_UNDYING)
                totems += inv.getItem(i).getCount();
        if (p.getItemBySlot(EquipmentSlot.OFFHAND).getItem() == Items.TOTEM_OF_UNDYING
                && inv.getContainerSize() <= 36)
            totems += p.getItemBySlot(EquipmentSlot.OFFHAND).getCount();
        if (totems > 0) {
            g.item(new ItemStack(Items.TOTEM_OF_UNDYING), x + 2, y);
            g.text(mc.font, "" + totems, x + 18, y + 5, totems < 2 ? Theme.urgent : Theme.fgBright, true);
        }
    }

    /* tracked waypoint: name, distance, an arrow that turns with you */
    private static void waypoint(GuiGraphicsExtractor g, Minecraft mc) {
        Waypoints.Point w = Waypoints.tracked();
        if (w == null)
            return;
        double[] at = Waypoints.inMyDim(w);
        var f = mc.font;
        int cx = g.guiWidth() / 2, y = 6;
        if (at == null) {
            g.centeredText(f, w.name + " (other dimension)", cx, y, Theme.dim);
            return;
        }
        double dx = at[0] + 0.5 - mc.player.getX(), dz = at[2] + 0.5 - mc.player.getZ();
        double dist = Math.hypot(dx, dz);
        float yaw = (float) Math.toDegrees(Math.atan2(-dx, dz));
        float rel = (float) Math.toRadians(yaw - mc.player.getYRot());
        String t = w.name + "  " + (dist >= 1000 ? String.format("%.1fk", dist / 1000) : (int) dist + "") + " m";
        int tw = f.width(t) + 30;
        Draw.panel(g, cx - tw / 2, y, tw, 16, Theme.alpha(Theme.bg, 0.72f), Theme.alpha(Theme.surface, 0.9f));
        float ax = cx - tw / 2f + 11, ay = y + 8;
        float s = (float) Math.sin(rel), c = (float) Math.cos(rel);
        /* arrow pointing "forward" = up on screen */
        float[][] pts = {{0, -5}, {3.5f, 4}, {0, 2}, {-3.5f, 4}};
        for (int i = 0; i < 4; i++) {
            float[] a = pts[i], b = pts[(i + 1) % 4];
            Draw.line(g, ax + a[0] * c - a[1] * s, ay + a[0] * s + a[1] * c, ax + b[0] * c - b[1] * s,
                    ay + b[0] * s + b[1] * c, 1.4f, Theme.accentBright);
        }
        g.text(f, t, cx - tw / 2 + 22, y + 4, Theme.fgBright, false);
    }

    private Hud() {}
}
