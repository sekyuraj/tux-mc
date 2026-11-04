# tux-mc

A Minecraft 26.3 client in two parts:

- **tuxmc**, a Fabric mod with a menu on Right Shift: network map, server tools, waypoints, emotes, capes, zoom, fullbright and a HUD.
- **tux-mc**, a GTK4 launcher written in C. [Prism Launcher](https://prismlauncher.org) does the real work behind it (accounts, downloads, starting the game).

The launcher and Prism both come with a black theme.

## Needs

- **Prism Launcher**, signed in to your Microsoft account. tux-mc uses Prism's account and instance.
- Linux on x86-64. The mod has a small native library for capes and the network map.
- To build the launcher: a C compiler, `pkg-config`, and the development packages for gtk4, json-glib and libcurl.
- `curl` and `envsubst` (from gettext).
- Building the mod yourself also needs JDK 25 (`javac`).

## Install

```sh
git clone https://github.com/sekyuraj/tux-mc
cd tux-mc
./install.sh
```

This does the following:

1. Creates a Prism instance called `26.3` with Fabric Loader, unless you already have one.
2. Downloads Fabric API from Modrinth and checks its sha512.
3. Downloads `tuxmc.jar` from the latest release.
4. Builds the launcher and installs it into `~/.local/bin`, along with a desktop entry.
5. Renders the black theme for the launcher and for Prism.

Run `tux-mc`, or "Minecraft (tux-mc)" from your app menu.

Options:

- `--build` builds the mod from source instead of downloading it. Launch the instance in Prism once first, so the game's libraries are on disk.
- `--no-theme` leaves Prism's theme alone.

## The mod

- **Right Shift menu**
  - Network: every connection the game has open, with known hosts named.
  - Server: queue position with ETA and an alert, TPS, and a log of who came into visual range.
  - Waypoints: kept per server, with death points, nether coordinates and a HUD arrow.
  - Session, Emotes, Capes, Settings.
- **Players near**: an alert and a HUD count for players within a radius you pick (16 to 256 blocks, or the whole render distance).
- **Hit delay fix**: on modern versions, a click that hits air blocks your clicks for half a second, so in 1.8-style PvP many hits are never sent. This removes that block. It's on by default; turn it off under Settings → Combat. It isn't on Hypixel's list of allowed mods, so whether you use it there is up to you.
- **Emote wheel** on `.`, client-side only.
- **Capes**: client-side HD capes painted by the native library, a cape from your own PNG, and a cape that follows your theme.
- **Zoom** on C, fullbright, an info HUD, armor and totem counts, and a streamer mode that hides coordinates.
- **Auto reconnect** after a kick.
- **No block limit** for `/fill`, `/clone` and `/fillbiome` in singleplayer and LAN.

Settings are kept in the instance's `config/tuxmc.properties`, which is re-read while the game runs.

## The launcher

- Full screen over your wallpaper, with a 3D model of your player.
- **Capes**: switches your official Mojang cape and shows the client capes on the model.
- **Skins**: upload a skin, keep a library of them, or copy one from a player by name.
- **Mods**: list, turn on and off, delete, drop in jars, and search and install from Modrinth.
- **Configs**: an editor for mod configs.
- **Settings**: RAM, JVM arguments, environment, wrapper command, and window size.
- Ctrl+1..5 switches pages.

## Themes

The theme lives in `theme/`. Each template there is plain CSS with `${bg}`, `${fg}`, `${accent}` and so on. `theme/render.sh [palette.sh]` fills them in from a palette and writes:

- `~/.config/tux-mc/style.css` for the launcher, which reloads it live;
- `~/.local/share/PrismLauncher/themes/tux/` for Prism, which it then switches to the `tux` theme.

`theme/palettes/black.sh` is the default. To make your own, copy it and change the colours. Prism only reads its theme on start, so close it before rendering.

If you use a desktop theme switcher that sets `~/.config/themes/current/theme.sh`, the mod and the launcher read their colours and wallpaper from it, and you can render these templates from the same palette.

## Building by hand

```sh
mod/build.sh               # builds mod/build/tuxmc.jar and copies it into the instance
make -C launcher install   # PREFIX=~/.local by default
```

Minecraft 26.x ships unobfuscated, so the mod builds with plain `javac` against Prism's libraries. No Gradle or Loom needed.

## Credits

The Gentoo cape uses the Gentoo logo (© Gentoo Foundation, CC BY-SA 2.5). Not affiliated with Mojang, Microsoft, Prism Launcher or Gentoo.

## License

MIT, see `LICENSE`.
