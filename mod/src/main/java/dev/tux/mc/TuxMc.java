package dev.tux.mc;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mojang.blaze3d.platform.InputConstants;

import dev.tux.mc.ui.EmoteWheel;
import dev.tux.mc.ui.TuxScreen;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.message.v1.ClientReceiveMessageEvents;
import net.fabricmc.fabric.api.client.keymapping.v1.KeyMappingHelper;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.fabric.api.client.screen.v1.ScreenEvents;
import net.fabricmc.fabric.api.client.screen.v1.Screens;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.AbstractWidget;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.DisconnectedScreen;
import net.minecraft.client.gui.screens.PauseScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

public class TuxMc implements ClientModInitializer {
    public static final Logger LOG = LoggerFactory.getLogger("tuxmc");
    public static KeyMapping zoomKey, scrollZoomKey, emoteKey, menuKey, brightKey, hudKey;

    @Override
    public void onInitializeClient() {
        Perf.modInit();
        Config.load();
        ThreadTuner.start();
        var cat = KeyMapping.Category.register(Identifier.fromNamespaceAndPath("tuxmc", "main"));
        zoomKey = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.tuxmc.zoom", InputConstants.KEY_C, cat));
        scrollZoomKey = KeyMappingHelper.registerKeyMapping(
                new KeyMapping("key.tuxmc.scrollzoom", InputConstants.KEY_GRAVE, cat));
        emoteKey = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.tuxmc.emotes", InputConstants.KEY_PERIOD, cat));
        menuKey = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.tuxmc.menu", InputConstants.KEY_RSHIFT, cat));
        brightKey = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.tuxmc.fullbright", InputConstants.UNKNOWN.getValue(), cat));
        hudKey = KeyMappingHelper.registerKeyMapping(new KeyMapping("key.tuxmc.hud", InputConstants.UNKNOWN.getValue(), cat));

        HudElementRegistry.addLast(Identifier.fromNamespaceAndPath("tuxmc", "hud"), Hud::extract);
        ClientTickEvents.END_CLIENT_TICK.register(TuxMc::tick);
        net.fabricmc.fabric.api.client.rendering.v1.LivingEntityRenderLayerRegistrationCallback.EVENT.register(
                (type, renderer, helper, ctx) -> {
                    if (renderer instanceof net.minecraft.client.renderer.entity.player.AvatarRenderer<?> r)
                        helper.register(new HatLayer(r));
                });

        ScreenEvents.AFTER_INIT.register((mc, screen, w, h) -> {
            if (screen instanceof TitleScreen || screen instanceof PauseScreen)
                addMenuButton(screen, w);
            if (screen instanceof DisconnectedScreen)
                addReconnect(screen, w, h);
        });
        net.fabricmc.fabric.api.client.networking.v1.ClientPlayConnectionEvents.INIT.register(
                (handler, client) -> DhCompat.connecting(client));
        net.fabricmc.fabric.api.client.networking.v1.ClientPlayConnectionEvents.DISCONNECT.register(
                (handler, client) -> DhCompat.disconnected());
        net.fabricmc.fabric.api.client.networking.v1.ClientPlayConnectionEvents.JOIN.register(
                (handler, sender, client) -> Perf.joined());
        ClientReceiveMessageEvents.GAME.register((msg, overlay) -> ServerTools.chat(msg));
        ClientReceiveMessageEvents.CHAT.register((msg, signed, sender, type, ts) -> ServerTools.chat(msg));
        LOG.info("tuxmc loaded");
    }

    /** the tux button: right of the first button row on the title and pause screens */
    private static void addMenuButton(net.minecraft.client.gui.screens.Screen screen, int w) {
        int y = -1, right = w / 2 + 100;
        for (AbstractWidget b : Screens.getWidgets(screen))
            if (b instanceof Button && b.getWidth() >= 150) {
                y = b.getY();
                right = b.getX() + b.getWidth();
                break;
            }
        if (y < 0)
            y = screen.height / 4 + 48;
        Button tux = Button.builder(Component.literal("Tux"), b -> Minecraft.getInstance().gui.setScreen(
                new TuxScreen(screen, screen instanceof TitleScreen ? TuxScreen.Tab.SETTINGS : TuxScreen.Tab.NETWORK)))
                .bounds(right + 4, y, 40, 20).build();
        Screens.getWidgets(screen).add(tux);     /* fabric's list also registers it as a child */
    }

    /** disconnect screen: Reconnect button, with a countdown when auto reconnect is on */
    private static void addReconnect(net.minecraft.client.gui.screens.Screen screen, int w, int h) {
        if (ServerTools.lastServer == null)
            return;
        final int[] left = {Config.autoReconnect ? Math.max(1, Config.reconnectDelay) * 20 : -1};
        Button b = Button.builder(Component.literal(label(left[0])), x -> ServerTools.reconnect())
                .bounds(w / 2 - 100, h - 34, 200, 20).build();
        Screens.getWidgets(screen).add(b);
        ScreenEvents.afterTick(screen).register(s -> {
            if (left[0] < 0)
                return;
            if (--left[0] == 0)
                ServerTools.reconnect();
            else
                b.setMessage(Component.literal(label(left[0])));
        });
    }

    private static String label(int ticks) {
        String name = ServerTools.lastServer.name;
        return ticks < 0 ? "Reconnect to " + name : "Reconnecting in " + (ticks + 19) / 20 + " s";
    }

    private static int themeTick;

    /* test hook: TUXMC_OPEN=network|session|emotes|cosmetics|settings|wheel, TUXMC_EMOTE=dance... */
    private static int testTicks = -1;

    private static void testHook(Minecraft mc) {
        String open = System.getenv("TUXMC_OPEN"), emote = System.getenv("TUXMC_EMOTE");
        if (open == null && emote == null && System.getenv("TUXMC_CAM") == null && System.getenv("TUXMC_DEMO") == null)
            return;
        boolean inWorld = mc.player != null;
        if (!inWorld && System.getenv("TUXMC_WORLD") != null)
            return;
        if (testTicks < 0)
            testTicks = 60;
        if (--testTicks != 0)
            return;
        if (System.getenv("TUXMC_DEMO") != null && inWorld) {
            ServerTools.text(Component.literal("Position in queue: 31"));
            ServerTools.text(Component.literal("Position in queue: 24"));
            Waypoints.add("Base", false);
            Notify.show("Steve", "entered visual range", Theme.urgent, false, false);
        }
        if ("back".equals(System.getenv("TUXMC_CAM")))
            mc.options.setCameraType(net.minecraft.client.CameraType.THIRD_PERSON_BACK);
        if (emote != null && inWorld && emote.equals("all"))
            cycleEmotes = 0;
        else if (emote != null && inWorld)
            Emotes.play(Emotes.Emote.valueOf(emote.toUpperCase()));
        if (open != null && mc.gui.screen() == null || open != null && !inWorld)
            mc.gui.setScreen(open.equals("wheel") ? new EmoteWheel()
                    : new TuxScreen(mc.gui.screen(), TuxScreen.Tab.valueOf(open.toUpperCase())));
        testTicks = -2;
    }

    private static int cycleEmotes = -1;
    private static boolean zoomOn;

    private static void tick(Minecraft mc) {
        if (testTicks != -2)
            testHook(mc);
        if (cycleEmotes >= 0 && cycleEmotes++ % 80 == 0) {   /* test: every 4 s the next emote */
            int i = cycleEmotes / 80;
            if (i < Emotes.Emote.values().length) {
                Emotes.stop();
                Emotes.play(Emotes.Emote.values()[i]);
            }
        }
        if (++themeTick % 40 == 0) {
            Theme.reload();
            Config.load();
        }
        if (!Config.menuKeyChecked && mc.options != null) {
            /* an older version saved the menu key as unbound; give it right shift back once */
            Config.menuKeyChecked = true;
            if (menuKey.isUnbound()) {
                menuKey.setKey(InputConstants.Type.KEYBOARD.getOrCreate(InputConstants.KEY_RSHIFT));
                KeyMapping.resetMapping();
                mc.options.save();
            }
            Config.save();
        }
        if (Config.zoomToggle) {
            while (zoomKey.consumeClick())
                zoomOn = !zoomOn;
            if (mc.gui.screen() != null)
                zoomOn = false;
            Zoom.held = zoomOn;
        } else {
            Zoom.held = zoomKey.isDown() && mc.gui.screen() == null;
        }
        Zoom.scrollHeld = scrollZoomKey.isDown() && mc.gui.screen() == null;
        while (brightKey.consumeClick()) {
            Config.fullbright = !Config.fullbright;
            Config.save();
            Notify.show("Fullbright " + (Config.fullbright ? "on" : "off"), null, Theme.accent, false, false);
        }
        while (hudKey.consumeClick()) {
            Config.hud = !Config.hud;
            Config.save();
        }
        FarView.tick();
        Hud.tick(mc);
        Capes.tick(mc);
        Emotes.tick(mc);
        ServerTools.tick(mc);
        Sync.tick(mc);
        Waypoints.tick(mc);
        while (menuKey.consumeClick())
            if (mc.gui.screen() == null)
                mc.gui.setScreen(new TuxScreen(null, TuxScreen.Tab.NETWORK));
        while (emoteKey.consumeClick())
            if (mc.gui.screen() == null && mc.player != null)
                mc.gui.setScreen(new EmoteWheel());
    }

    public static int ping() {
        Minecraft mc = Minecraft.getInstance();
        var c = mc.getConnection();
        if (c == null || mc.player == null || mc.isLocalServer())
            return -1;
        var info = c.getPlayerInfo(mc.player.getUUID());
        return info == null ? -1 : info.getLatency();
    }
}
