# tux-mc

A Minecraft 26.3 client for Linux and Windows, in two parts:

- **tuxmc**, a Fabric mod with a menu on Right Shift: network map, server tools, waypoints, emotes, capes, zoom, fullbright and a HUD.
- **tux-mc**, a GTK4 launcher written in C. [Prism Launcher](https://prismlauncher.org) does the real work behind it (accounts, downloads, starting the game).

The launcher and Prism both come with a black theme.

## Windows

Download `tux-mc-windows.zip` from the [latest release](https://github.com/sekyuraj/tux-mc/releases/latest), unzip it and double-click `install.bat`. It installs Prism Launcher too if you don't have it, sets everything up and walks you through signing in. The step-by-step guide is [windows/INSTALL.txt](windows/INSTALL.txt), and the same file is in the zip.

## Linux

What you need:

- Linux on x86-64, and a Microsoft account that owns Minecraft: Java Edition.
- To build the launcher: a C compiler, `pkg-config`, and the development packages for gtk4, json-glib and libcurl.
- `curl` and `envsubst` (from gettext).
- Building the mod yourself also needs JDK 25 (`javac`).

[Prism Launcher](https://prismlauncher.org) is needed too, but you don't have to install it first. If it's missing, the installer gets it.

```sh
git clone https://github.com/sekyuraj/tux-mc
cd tux-mc
./install.sh
```

The installer sets up whatever is missing:

1. **Prism Launcher**, if you don't have it: its AppImage, unpacked into `~/.local/share/tux-mc/prism` (so FUSE isn't needed), with a `prismlauncher` command in `~/.local/bin`. Prism is set to download the right Java by itself.
2. A Prism instance called `26.3` with Fabric Loader.
3. Fabric API from Modrinth (sha512-checked) and `tuxmc.jar` from the latest release.
4. The launcher, in `~/.local/bin`, with a desktop entry.
5. The black theme for the launcher and for Prism.
6. **Your Microsoft account**: if Prism has none, Prism opens. Click "Add Microsoft account" and sign in. The installer waits until you're done.

Run `tux-mc`, or "Minecraft (tux-mc)" from your app menu.

The launcher checks the same things every time it starts. If Prism, the account or the instance is missing, it shows a setup screen whose SET UP button fixes it, on Linux and on Windows.

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
- **Render distance up to 256** in Video Settings (Sodium's slider follows; the installer and the launcher's setup put Sodium in for you). In singleplayer the built-in server loads up to 125 chunks; past that it can't keep up (at 251 it used 20 GB and the chunks around the player never arrived). For a view that far, set Distant Horizons' LOD radius to 256 and keep the render distance at 12 to 32.
- **Hit delay fix**: on modern versions, a click that hits air blocks your clicks for half a second, so in 1.8-style PvP many hits are never sent. This removes that block. It's on by default; turn it off under Settings → Combat. It isn't on Hypixel's list of allowed mods, so whether you use it there is up to you.
- **Emote wheel** on `.`: wave, dance, sit, lay, clap, spin, bow, dab. Your camera stays as it is; in first person you see both arms do the emote (F5 to watch all of you). Spin turns fast and keeps turning. Wave, dance, sit, lay, clap and spin keep going until you walk, jump, sneak, sprint or press Esc; bow and dab play once.
- **Capes**: Tux, Gentoo, SteamOS, Arch, Debian, Mint, FreeBSD, OpenBSD, Linux from Scratch, CRUX, Windows 11, two drawn ones (Starfield, Synthwave), and a cape from your own picture.
- **Zoom**: hold C, or hold `` ` `` and scroll to zoom in and out (the wheel doesn't change the hotbar while you do). Fullbright, an info HUD, armor and totem counts, and a streamer mode that hides coordinates.
- **Hats**: the 3D model you made on the launcher's Hats page sits on your head, turns and bobs with it, and hides while you wear a helmet or a mob head (Settings, `hatHideHelmet`).
- **Title screen picture**: your own picture behind the menus instead of the spinning world, with blur, darkening and a slight drift with the mouse. Set it on the launcher's Settings → Title screen.
- **Auto reconnect** after a kick.
- **No block limit** for `/fill`, `/clone` and `/fillbiome` in singleplayer and LAN.

Settings are kept in the instance's `config/tuxmc.properties`, which is re-read while the game runs.

## Other players see your cape, hat and emotes

Other tux-mc players on the same server see your client cape, your hat and your emotes, and you see theirs. Players without tux-mc see your normal Mojang cape.

There's no tux-mc server. The data goes through public [Nostr](https://nostr.com) relays (relay.damus.io, nos.lol and relay.primal.net by default):

- Your cape is one small message, sent when you join a server and again when you change it.
- An emote is one tiny message that says which emote you played, sent only to players on the same server. At most 6 go out every 10 seconds.
- Players never connect to each other, so nobody sees another player's IP. The relays see yours, like any website you visit.
- What goes out: your UUID, your cape, your emotes, and a hash of the server address (not the address itself).
- Every message is signed with your Mojang chat key, the one Mojang gives every Microsoft account. The receiver checks that Mojang issued the key for that UUID, so nobody can put a cape or an emote on someone else. tux-mc gets this key from Mojang itself, so this also works with No Chat Reports installed. The key is only used for these messages and never goes to a Minecraft server.
- **Picture capes** are shared too. That means uploading your picture, at most about 45 KB, and downloading the pictures of other players near you, so it uses more internet than the built-in capes. Turn it off under Settings → Sharing → Share pictures, and you'll send and receive only the built-in capes.

- **Hats** go out as their own message, compressed. A hat with every cell filled at the fine 32-cell size can't be shared (the launcher tells you); anything normal is a few kilobytes. `syncHats=false` stops showing other people's hats.

To turn all of it off, use Settings → Sharing → Share capes and emotes. You can change the relays with `syncRelays=` in `tuxmc.properties`. Sharing needs a Microsoft account and works on online-mode servers.

## The launcher

- Full screen over your wallpaper, with a 3D model of your player.
- **Instances**: every Prism instance, and which one Start plays. Make new ones with any Minecraft version (snapshots too) and Vanilla, Fabric, Quilt, NeoForge or Forge. Fabric API is added for you if you want it. Prism downloads the game the first time you start a new instance.
- **Import**: bring worlds, schematics (.litematic, .schem, .schematic, .nbt), resource packs, shader packs and screenshots over from the Minecraft Launcher, CurseForge, the Modrinth App, other Prism instances or any folder, by ticking them; or drop world zips, schematics, packs and mods on the page. It only copies: nothing is moved or deleted where it comes from, nothing here is overwritten (a second copy becomes "Name (2)"), a world only appears once it's complete, and free space is checked first.
- When the game is up, the launcher brings its window to the front (Windows; niri, Hyprland, sway and X11 on Linux; elsewhere it minimizes itself). F11 makes the launcher fullscreen; on Windows dragging it to the top of the screen maximizes it and to the sides tiles it.
- **Capes**: switches your official Mojang cape and shows the client capes on the model. "New picture…" opens an editor: name it, drag the picture into place, zoom, stretch or squeeze it on either axis, rotate, flip, and choose what fills the empty space. Each picture cape is saved as its own chip with that name; right-click one to edit it again (everything comes back as you left it) or delete it (it goes to the trash).
- **Account**: click your name at the bottom of the sidebar for your UUID, signing in again, Prism's account manager and your NameMC page. If your skin is drawn for slim arms but your account says classic (or the other way round), it says so and fixes it with one click.
- **Skins**: upload a skin, keep a library of them, or copy one from a player by name.
- **Colours**: wherever you choose a colour (hats, packs, themes, the cape editor) there's a colour wheel: a hue ring round a shade square, a box for hex codes like `#3f86d9`, and an opacity slider where see-through makes sense.
- **Hats**: build a 3D model on your head, cell by cell. Click your head to add cells, paint, erase, fill, pick colours; mirror left and right is on by default, so you only build one side. Drag empty space to turn the view, scroll to zoom, Ctrl+Z to undo. 16 cells a side (one per skin pixel) or 32 for finer detail, over a space twice as wide as your head and six pixels above it. Start from an empty grid, from one of ten ready ones (top hat, crown, cat ears, halo, horns, party hat, headphones, sunglasses, propeller cap, flower crown), or import a MagicaVoxel `.vox` file. "Wear it" puts it on, in game too, and other tux-mc players see it.
- **Packs**: make a resource pack without an image editor. Find any of the game's textures by name or by group (blocks, items, mobs, menus …), click it, and draw on it pixel by pixel: draw, erase, fill and pick, with any colour from the colour wheel or one the texture already uses. "Change the colours of the whole texture" has hue, saturation, brightness and contrast. Every change is saved straight into a folder pack in the instance, so in game F3+T shows it; Undo and Reset take changes back. "Use in game" turns the pack on, "Share as zip" makes a file to share.
- **Mods**: list, turn on and off, delete, drop in jars, and search and install from Modrinth.
- **Configs**: an editor for mod configs.
- **Settings**, in sections: Game (RAM, window), Performance (speed switches, Java arguments, environment, wrapper), Launcher, Looks, Title screen, Themes and Updates. The launcher stays open while you play unless you turn on "Close when the game is up".
- **Themes**: each preset is shown as a small picture of the launcher in its colours. Click one and it's used at once, in the launcher, in Prism (when Prism is closed) and in the in-game right-shift menu; Undo goes back. To make your own, pick three colours (background, text, accent) or take them from a picture such as your wallpaper; the other ten are worked out from those. "Fine-tune every colour" opens all thirteen. Your own presets are saved in `~/.config/tux-mc/presets/`, and desktop themes from `~/.config/themes` show up too.
- **Updates** (in Settings): with "Update by itself" on, the launcher looks for a newer release on every start and installs it, along with the tuxmc mod in the current instance. You don't need to build anything. Every download is checked against its `.sha256`. "Restart now" switches to the new version.
- Ctrl+1..9 switches pages, and so does Shift with the mouse wheel (or a two-finger swipe), one page per notch, like scrolling workspaces in niri.
- **Launcher picture** (Settings → Looks): any picture behind Home and the other pages instead of the desktop wallpaper, with how dark the shade over it is. Blur and drift are in the card below it.

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

`mod/build.sh` also cross-compiles the Windows version of the native library when `x86_64-w64-mingw32-gcc` is installed, so one jar works on both systems. The Windows launcher is built in MSYS2 (UCRT64) with `make -C launcher` and packed by `windows/package.sh`. GitHub Actions does both for every release.

To release, bump `VERSION` and the version in `mod/src/main/resources/fabric.mod.json`, commit, and run `tools/release.sh`. It builds the mod, uploads `tuxmc.jar` and its checksum, and publishes the release. The `linux` and `windows` workflows then attach the launcher builds (`linux/package.sh`, `windows/package.sh`). That's what the updater downloads.

## Credits

- Tux: by Larry Ewing (lewing@isc.tamu.edu), made with The GIMP. Pixelated for the cape.
- The Gentoo logo is © Gentoo Foundation, CC BY-SA 2.5.
- The Debian swirl is the Debian Open Use Logo.
- The Arch, FreeBSD, OpenBSD and Mint marks come from [Simple Icons](https://simpleicons.org) (CC0). The Mint logo is from Wikimedia Commons.
- The SteamOS mark comes from Steam's own boot image.
- The CRUX logo comes from crux.nu. The Linux from Scratch logo comes from linuxfromscratch.org (CC0 on Wikimedia Commons).

All logos and names belong to their projects and are only used here as fan capes. tux-mc isn't affiliated with Mojang, Microsoft, Prism Launcher, Valve, or any of these projects.

## License

MIT, see `LICENSE`.
