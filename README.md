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
- **Hit delay fix**: on modern versions, a click that hits air blocks your clicks for half a second, so in 1.8-style PvP many hits are never sent. This removes that block. It's on by default; turn it off under Settings → Combat. It isn't on Hypixel's list of allowed mods, so whether you use it there is up to you.
- **Emote wheel** on `.`: wave, dance, sit, lay, clap, spin, bow, dab.
- **Capes**: Tux, Gentoo, SteamOS, Arch, Debian, Mint, FreeBSD, OpenBSD, Linux from Scratch, CRUX, two drawn ones (Starfield, Synthwave), and a cape from your own picture.
- **Zoom** on C, fullbright, an info HUD, armor and totem counts, and a streamer mode that hides coordinates.
- **Auto reconnect** after a kick.
- **No block limit** for `/fill`, `/clone` and `/fillbiome` in singleplayer and LAN.

Settings are kept in the instance's `config/tuxmc.properties`, which is re-read while the game runs.

## Other players see your cape and emotes

Other tux-mc players on the same server see your client cape and your emotes, and you see theirs. Players without tux-mc see your normal Mojang cape.

There's no tux-mc server. The data goes through public [Nostr](https://nostr.com) relays (relay.damus.io, nos.lol and relay.primal.net by default):

- Your cape is one small message, sent when you join a server and again when you change it.
- An emote is one tiny message that says which emote you played, sent only to players on the same server. At most 6 go out every 10 seconds.
- Players never connect to each other, so nobody sees another player's IP. The relays see yours, like any website you visit.
- What goes out: your UUID, your cape, your emotes, and a hash of the server address (not the address itself).
- Every message is signed with your Mojang chat key, the one Mojang gives every Microsoft account. The receiver checks that Mojang issued the key for that UUID, so nobody can put a cape or an emote on someone else. tux-mc gets this key from Mojang itself, so this also works with No Chat Reports installed. The key is only used for these messages and never goes to a Minecraft server.
- **Picture capes** are shared too. That means uploading your picture, at most about 45 KB, and downloading the pictures of other players near you, so it uses more internet than the built-in capes. Turn it off under Settings → Sharing → Share pictures, and you'll send and receive only the built-in capes.

To turn all of it off, use Settings → Sharing → Share capes and emotes. You can change the relays with `syncRelays=` in `tuxmc.properties`. Sharing needs a Microsoft account and works on online-mode servers.

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

`mod/build.sh` also cross-compiles the Windows version of the native library when `x86_64-w64-mingw32-gcc` is installed, so one jar works on both systems. The Windows launcher is built in MSYS2 (UCRT64) with `make -C launcher` and packed by `windows/package.sh`. GitHub Actions does both for every release.

## Credits

- Tux: by Larry Ewing (lewing@isc.tamu.edu), made with The GIMP. Pixelated for the cape.
- The Gentoo logo is © Gentoo Foundation, CC BY-SA 2.5.
- The Debian swirl is the Debian Open Use Logo.
- The Arch, FreeBSD, OpenBSD and Mint marks come from [Simple Icons](https://simpleicons.org) (CC0). The Mint logo is from Wikimedia Commons.
- The SteamOS mark comes from Steam's own boot image.
- The CRUX logo comes from crux.nu. The Linux from Scratch jigsaw Tux is redrawn as a vector after the LFS logo, since no large version of it exists.

All logos and names belong to their projects and are only used here as fan capes. tux-mc isn't affiliated with Mojang, Microsoft, Prism Launcher, Valve, or any of these projects.

## License

MIT, see `LICENSE`.
